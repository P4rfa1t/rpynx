/* Controller settings: global and per-game mappings, button capture,
 * presets, the stick and repeat settings, saving. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "fs.h"
#include "input.h"
#include "input_config.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

/* -------------------------------------------------------------- remapping */

void open_remap(const char *path, const char *label, bool fallback_to_global, screen_id back)
{
    char err[256];
    go(SCR_REMAP);
    m.remap_back = back;
    snprintf(g.edit_path, sizeof g.edit_path, "%s", path);
    snprintf(g.edit_label, sizeof g.edit_label, "%s", label);
    struct stat st;
    bool exists = stat(path, &st) == 0;
    rsl_load_result r = rsl_config_load(path, &g.edit, err, sizeof err);
    if (r == RSL_LOAD_DEFAULTS && fallback_to_global)
        g.edit = g.in.cfg;
    if (r == RSL_LOAD_BACKUP)
        status(ST_BAD, "设置文件已损坏，已加载其备份。%s", err);
    else if (r == RSL_LOAD_DEFAULTS && exists)
        status(ST_BAD, "无法读取，改用%s：%s", fallback_to_global ? "全局控制" : "默认设置",
               err);
    g.edit_preset = 0;
    g.edit_dirty = m.discard_armed = false;
}

void game_controls(int i, screen_id back)
{
    char path[256], label[80];
    snprintf(path, sizeof path, CTRL_DIR "/games/%s.json", g.games[i]);
    snprintf(label, sizeof label, "游戏：%.60s", title(i));
    open_remap(path, label, true, back);
}

static void leave_remap(void)
{
    if (m.remap_back == SCR_DETAIL) {
        go(SCR_DETAIL);
        g.sel = DETAIL_CONTROLS;
    } else {
        go_library();
    }
}

static void save_edit(void)
{
    char err[256], dir[256];
    snprintf(dir, sizeof dir, "%s", g.edit_path);
    *strrchr(dir, '/') = '\0';
    if (!rsl_mkdirs(dir)) {
        status(ST_BAD, "无法创建 %s（SD 卡缺失或为只读？）", dir);
        return;
    }
    if (!rsl_config_save(g.edit_path, &g.edit, err, sizeof err)) {
        status(ST_BAD, "未保存：%s", err);
        return;
    }
    g.edit_dirty = false;
    if (strcmp(g.edit_path, GLOBAL_CFG) == 0) {
        /* Apply live. Held buttons (the save button itself) must be released
         * before they count under the new mapping. */
        rsl_input_init(&g.in, &g.edit);
        rsl_input_reset(&g.in, g.raw.buttons);
        status(ST_OK, "已保存并应用。");
    } else {
        status(ST_OK, "已为此游戏保存。");
    }
}

static void adjust_row(int row, int dir)
{
    rsl_input_config *c = &g.edit;
    switch (row) {
    case ROW_DEADZONE: c->deadzone += 0.01f * dir; break;
    case ROW_SPEED: c->cursor_speed += 0.05f * dir; break;
    case ROW_ACCEL: c->cursor_accel += 0.25f * dir; break;
    case ROW_DELAY: c->repeat_delay_ms = (uint32_t)((int)c->repeat_delay_ms + 25 * dir); break;
    case ROW_INTERVAL: c->repeat_interval_ms = (uint32_t)((int)c->repeat_interval_ms + 10 * dir); break;
    case ROW_STICKS: {
        rsl_stick t = c->nav_stick;
        c->nav_stick = c->cursor_stick;
        c->cursor_stick = t;
        break;
    }
    case ROW_CURSOR_VIS: c->cursor_visible = !c->cursor_visible; break;
    default: return;
    }
    rsl_config_sanitize(c);
    g.edit_dirty = true;
}

void remap_capture_tick(void)
{
    if (g.capture < 0)
        return;
    if ((int32_t)(g.now - g.capture_deadline) >= 0) {
        g.capture = -1;
        status(ST_INFO, "没有按下任何按键：未更改。");
        return;
    }
    uint32_t p = g.in.pressed_buttons;
    if (!p)
        return;
    int b = __builtin_ctz(p);
    uint32_t clash = rsl_config_bind(&g.edit, (rsl_action)g.capture, (rsl_button)b);
    g.edit_dirty = true;
    if (clash) {
        char names[160] = "";
        for (int a = 0; a < RSL_ACT_COUNT; a++)
            if (clash & (1u << a)) {
                size_t n = strlen(names);
                snprintf(names + n, sizeof names - n, "%s%s", n ? ", " : "", ui_action_name(a));
            }
        status(ST_BAD, "%s \xe2\x86\x92 %s：已被 %s 使用", ui_action_name(g.capture), ui_button_name(b), names);
    } else {
        status(ST_INFO, "%s \xe2\x86\x92 %s", ui_action_name(g.capture), ui_button_name(b));
    }
    g.capture = -1;
    /* the captured button is still down: do not let it act as a UI key */
    rsl_input_reset(&g.in, g.in.buttons);
}

void remap_action(rsl_action a)
{
    switch (a) {
    case RSL_ACT_UP: g.sel = (g.sel + ROW_COUNT - 1) % ROW_COUNT; break;
    case RSL_ACT_DOWN: g.sel = (g.sel + 1) % ROW_COUNT; break;
    case RSL_ACT_CONFIRM:
        if (g.sel < RSL_ACT_COUNT) {
            g.capture = g.sel;
            g.capture_deadline = g.now + CAPTURE_MS;
            g.status[0] = '\0';
        } else {
            adjust_row(g.sel, 1);
        }
        break;
    case RSL_ACT_LEFT: adjust_row(g.sel, -1); break;
    case RSL_ACT_RIGHT: adjust_row(g.sel, 1); break;
    case RSL_ACT_HISTORY:
        g.edit_preset = (g.edit_preset + 1) % RSL_PRESET_COUNT;
        rsl_config_preset(&g.edit, (rsl_preset)g.edit_preset);
        g.edit_dirty = true;
        status(ST_INFO, "预设：%s（尚未保存）。", ui_preset_name(g.edit_preset));
        break;
    case RSL_ACT_HIDE_UI:
        rsl_config_preset(&g.edit, RSL_PRESET_DEFAULT);
        g.edit_preset = 0;
        g.edit_dirty = true;
        status(ST_INFO, "默认控制（尚未保存）。");
        break;
    case RSL_ACT_GAME_MENU:
        save_edit();
        break;
    case RSL_ACT_BACK:
        if (g.edit_dirty && !m.discard_armed) {
            m.discard_armed = true;
            status(ST_INFO, "有未保存的更改。再按一次 %s 以放弃。",
                   ui_button_name(__builtin_ctz(g.in.cfg.bindings[RSL_ACT_BACK])));
        } else {
            leave_remap();
        }
        return;
    default:
        return;
    }
    m.discard_armed = false;
}
