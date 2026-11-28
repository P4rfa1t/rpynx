/* Input and navigation: each frame's controller input through the mapping
 * engine, and what each action does on each screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "input.h"
#include "input_switch.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

void status(status_kind kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof g.status, rsl_ui_tr(fmt), ap);
    va_end(ap);
    g.status_kind = kind;
    rsl_log("%s", g.status);
}

/* Every screen opens with its first item focused. */
void go(screen_id s)
{
    g.screen = s;
    g.sel = 0;
    g.capture = -1;
    g.status[0] = '\0';
    g.menu_open = g.dialog_open = false;
    g.dialog_kind = DLG_NONE;
    g.dialog_sel = 0;
}

void go_library(void)
{
    go(SCR_LIBRARY);
    g.sel = m.lib_sel < g.game_count ? m.lib_sel : 0;
    if (!m.shortcut_prompted) {
        g.dialog_open = true;
        g.dialog_sel = SC_DECLINE;
    }
}

static void menu_action(rsl_action a)
{
    switch (a) {
    case RSL_ACT_UP: g.menu_sel = (g.menu_sel + MENU_COUNT - 1) % MENU_COUNT; break;
    case RSL_ACT_DOWN: g.menu_sel = (g.menu_sel + 1) % MENU_COUNT; break;
    case RSL_ACT_BACK:
    case RSL_ACT_GAME_MENU: g.menu_open = false; break;
    case RSL_ACT_CONFIRM:
        m.lib_sel = g.sel;
        switch (g.menu_sel) {
        case MENU_FTP: open_ftp(); break;
        case MENU_MTP: open_mtp(); break;
        case MENU_REMAP: open_remap(GLOBAL_CFG, rsl_ui_tr("全局"), false, SCR_LIBRARY); break;
        case MENU_LAYOUT:
            go(SCR_LAYOUT);
            g.sel = m.layout_col = g.layout;
            m.accent_col = SET_PRESET;
            break;
        case MENU_HELP: go(SCR_HELP); break;
        default: m.quit = true; break;
        }
        break;
    default:
        break;
    }
}

/* Row: one line, left/right. Grid: COLS per row. Both stop at
 * their edges. */
static void library_action(rsl_action a)
{
    if (g.dialog_open) {
        if (a == RSL_ACT_LEFT)
            g.dialog_sel = SC_DECLINE;
        else if (a == RSL_ACT_RIGHT)
            g.dialog_sel = SC_ACCEPT;
        else if (a == RSL_ACT_BACK) {
            g.dialog_open = false;
            m.shortcut_prompted = true;
            save_settings();
        } else if (a == RSL_ACT_CONFIRM) {
            int choice = g.dialog_sel;
            g.dialog_open = false;
            m.shortcut_prompted = true;
            save_settings();
            if (choice == SC_ACCEPT)
                create_self_shortcut();
        }
        return;
    }
    int n = g.game_count, step = g.layout == LAYOUT_GRID ? COLS : 0;
    if (g.menu_open) {
        menu_action(a);
        return;
    }
    if (a == RSL_ACT_GAME_MENU) {
        g.menu_open = true;
        g.menu_sel = 0;
        return;
    }
    if (!n)
        return;
    switch (a) {
    case RSL_ACT_LEFT: if (g.sel > 0) g.sel--; break;
    case RSL_ACT_RIGHT: if (g.sel < n - 1) g.sel++; break;
    case RSL_ACT_UP: if (step && g.sel >= step) g.sel -= step; break;
    case RSL_ACT_DOWN:
        if (step && g.sel / step < (n - 1) / step)
            g.sel = g.sel + step < n ? g.sel + step : n - 1;
        break;
    case RSL_ACT_CONFIRM:
        m.lib_sel = g.game = g.sel;
        go(SCR_DETAIL);
        break;
    case RSL_ACT_HIDE_UI:
        if (g.info[g.sel].is_zip) {
            status(ST_INFO, rsl_ui_tr("请先解压此游戏后再设置封面或手柄。"));
            break;
        }
        m.lib_sel = g.sel;
        game_controls(g.sel, SCR_LIBRARY);
        break;
    default:
        break;
    }
}

/* The Layout page: left/right within a line (on the hue row: the hue, live),
 * up/down between the lines, confirm uses. Layout and presets
 * are saved at once; the custom hue on confirm or when the page is left. */
static void layout_action(rsl_action a)
{
    uint32_t rgb;
    const char *name;
    int s = g.sel;
    bool layout = s < LAYOUT_COUNT, preset = s >= SET_PRESET && s < SET_CUSTOM;
    if (layout)
        m.layout_col = s;
    if (preset)
        m.accent_col = s;
    switch (a) {
    case RSL_ACT_LEFT:
    case RSL_ACT_RIGHT:
        if (s == SET_CUSTOM) {
            g.accent_hue = (g.accent_hue + (a == RSL_ACT_LEFT ? 350 : 10)) % 360;
            g.accent = ui_hue_color(g.accent_hue);
            m.accent_dirty = true;
        } else if (a == RSL_ACT_LEFT && s != 0 && s != SET_PRESET) {
            g.sel--;
        } else if (a == RSL_ACT_RIGHT && s != LAYOUT_COUNT - 1 && s != SET_CUSTOM - 1) {
            g.sel++;
        }
        break;
    case RSL_ACT_UP:
        g.sel = s == SET_CUSTOM ? m.accent_col : preset ? m.layout_col : s;
        break;
    case RSL_ACT_DOWN:
        g.sel = layout ? m.accent_col : preset ? SET_CUSTOM : s;
        break;
    case RSL_ACT_CONFIRM:
        if (layout) {
            g.layout = s;
        } else if (preset) {
            ui_accent_preset(s - SET_PRESET, &rgb, &name);
            g.accent = rgb;
            status(ST_OK, rsl_ui_tr("强调色：%s。"), name);
        } else {
            g.accent = ui_hue_color(g.accent_hue);
            m.accent_dirty = false;
            status(ST_OK, rsl_ui_tr("强调色：自定义，色相 %d。"), g.accent_hue);
        }
        save_settings();
        break;
    case RSL_ACT_BACK:
        if (m.accent_dirty)
            save_settings();
        m.accent_dirty = false;
        go_library();   /* like every page the + menu opens */
        break;
    default:
        break;
    }
}

static void on_action(rsl_action a)
{
    switch (g.screen) {
    case SCR_LIBRARY:
        library_action(a);
        break;
    case SCR_DETAIL:
        if (g.dialog_open) {
            if (g.dialog_kind == DLG_DELETE) {
                if (a == RSL_ACT_LEFT)
                    g.dialog_sel = DEL_CANCEL;
                else if (a == RSL_ACT_RIGHT)
                    g.dialog_sel = DEL_CONFIRM;
                else if (a == RSL_ACT_BACK) {
                    g.dialog_open = false;
                    g.dialog_kind = DLG_NONE;
                } else if (a == RSL_ACT_CONFIRM) {
                    int choice = g.dialog_sel;
                    g.dialog_open = false;
                    g.dialog_kind = DLG_NONE;
                    if (choice == DEL_CONFIRM)
                        delete_game(g.game);
                }
            } else if (a == RSL_ACT_CONFIRM || a == RSL_ACT_BACK) {
                g.dialog_open = false;
                g.dialog_kind = DLG_NONE;
            }
        } else if (a == RSL_ACT_LEFT || a == RSL_ACT_RIGHT) {   /* Launch is rightmost, index 0 */
            if (a == RSL_ACT_LEFT && g.sel < DETAIL_COUNT - 1)
                g.sel++;
            else if (a == RSL_ACT_RIGHT && g.sel > 0)
                g.sel--;
        } else if (a == RSL_ACT_UP || a == RSL_ACT_DOWN) {
            ui_scroll(a == RSL_ACT_DOWN ? 1 : -1);
        } else if (a == RSL_ACT_HIDE_UI) {
            g.dialog_kind = DLG_DELETE;
            g.dialog_sel = DEL_CANCEL;
            g.dialog_open = true;
        } else if (a == RSL_ACT_BACK) {
            go_library();
        } else if (a == RSL_ACT_CONFIRM) {
            if (g.sel == DETAIL_LAUNCH)
                launch(g.game);
            else if (g.info[g.game].is_zip)
                status(ST_INFO, rsl_ui_tr("请先解压此游戏后再设置封面或手柄。"));
            else if (g.sel == DETAIL_SHORTCUT)
                create_shortcut(g.game);
            else if (g.sel == DETAIL_CONTROLS)
                game_controls(g.game, SCR_DETAIL);
            else if (g.sel == DETAIL_PICTURE)
                open_picture(g.game);
        }
        break;
    case SCR_PICTURE:
        picture_action(a);
        break;
    case SCR_REMAP:
        remap_action(a);
        break;
    case SCR_LAYOUT:
        layout_action(a);
        break;
    case SCR_HELP:
        if (a == RSL_ACT_UP && g.sel > 0)
            g.sel--;
        if (a == RSL_ACT_DOWN && g.sel < HELP_COUNT - 1)
            g.sel++;
        if (a == RSL_ACT_CONFIRM || a == RSL_ACT_BACK) {
            if (!m.help_seen) {
                m.help_seen = true;
                save_settings();
            }
            go_library();
        }
        break;
    case SCR_FTP:
        if (g.dialog_open) {   /* "game arrived": left/right, A uses, B sends another */
            if (a == RSL_ACT_LEFT)
                g.dialog_sel = ARRIVED_MORE;
            else if (a == RSL_ACT_RIGHT)
                g.dialog_sel = ARRIVED_LIBRARY;
            else if (a == RSL_ACT_BACK || (a == RSL_ACT_CONFIRM && g.dialog_sel == ARRIVED_MORE))
                g.dialog_open = false;
            else if (a == RSL_ACT_CONFIRM)
                close_ftp(), go_library();
        } else if (a == RSL_ACT_BACK) {
            close_ftp();
            go_library();
        }
        break;
    case SCR_UNPACK:
        if (a == RSL_ACT_BACK)
            cancel_unpack();
        break;
    }
}

/* A tap on the touchscreen: the item is focused, then confirmed (a guide
 * card is only focused); a footer hint does its action. */
static void tap(int a, int item)
{
    if (item >= 0) {
        if (g.menu_open)
            g.menu_sel = item;
        else if (g.dialog_open)
            g.dialog_sel = item;
        else
            g.sel = item;
        if (g.screen == SCR_HELP)
            return;
    }
    if (a >= 0)
        on_action(a);
}

void frame(float dt)
{
    rsl_switch_input_read(&m.pad, &g.raw);

    uint32_t sig = rsl_switch_pad_signature(&m.pad);
    if (sig != m.pad_sig) {
        rsl_log("controller change: style 0x%x -> 0x%x", (unsigned)m.pad_sig, (unsigned)sig);
        m.pad_sig = sig;
        m.reset_pending = true;
    }
    if (m.reset_pending) {
        m.reset_pending = false;
        rsl_input_reset(&g.in, g.raw.buttons);
    }

    rsl_input_update(&g.in, &g.raw, g.now, dt);
    if (g.in.event_count || g.in.pressed_buttons || g.raw.touching)
        m.last_input = g.now;
    /* Before the event loop: a capture started by this frame's confirm press
     * must not capture that same press. A completed capture clears this
     * frame's events, so the captured button does not also act as a UI key. */
    if (g.screen == SCR_REMAP)
        remap_capture_tick();

    for (int i = 0; i < g.in.event_count; i++) {
        const rsl_event *e = &g.in.events[i];
        if (e->type == RSL_EV_EMERGENCY) {
            rsl_log("emergency combo: default controls restored");
            open_remap(GLOBAL_CFG, rsl_ui_tr("全局"), false, SCR_LIBRARY);
            g.edit = g.in.cfg;
            g.edit_dirty = true;
            status(ST_BAD, rsl_ui_tr("紧急重置：已启用默认控制。按 + 保存。"));
        } else if (e->type != RSL_EV_ACTION_UP && g.capture < 0) {
            on_action(e->action);
        }
    }
    int item, tapped = ui_tap(&item);
    if (g.capture < 0 && (tapped >= 0 || item >= 0))
        tap(tapped, item);
    picture_tick();
    ftp_tick();
    unpack_tick();
}
