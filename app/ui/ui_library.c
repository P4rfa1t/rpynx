/* The library in its two layouts (Row, Grid) and the + menu,
 * whose sliding panel the picture page reuses. */
#include "ui_internal.h"

static const hint menu_hints[] = { { RSL_ACT_CONFIRM, -1, "选择" }, { RSL_ACT_BACK, -1, "关闭" } };

static const char *const menu_text[MENU_COUNT][2] = {
    { "通过 Wi-Fi 添加游戏", "从导入器接收游戏" },
    { "通过 USB 添加游戏", "使用 MTP 从导入器接收游戏" },
    { "手柄设置", "按键、摇杆、重复速度" },
    { "手柄测试", "实时查看按键、摇杆和操作" },
    { "布局", NULL },   /* subtitle: the current settings */
    { "游戏指南", "按键、完整内存、添加游戏" },
    { "诊断", "内存、文件、日志、Python 自检" },
    { "退出到 Homebrew Menu", "关闭加载器" },
};

static int banner(lv_obj_t *s, int y, uint32_t color, const char *text)
{
    lv_obj_set_pos(pill(s, color, text), MARGIN, y + 14);
    return y + 14 + lv_font_get_line_height(f_s) + 22;
}

static const char *accent_name(void)
{
    for (int i = 0; i < ACCENT_PRESETS; i++)
        if (presets[i].rgb == g.accent)
            return presets[i].name;
    return rsl_ui_tr("自定义");
}

/* A panel that slides in from the right over s: a title, n entries of a name
 * and a line under it (entry dyn's line is dyn_sub), the help at the bottom.
 * g.menu_open shows it, g.menu_sel is the focused entry. */
void side_menu(lv_obj_t *s, const char *title, const char *const (*text)[2], int n, int dyn,
                      const char *dyn_sub)
{
    u.menu = overlay(s);
    lv_obj_t *p = flex(u.menu, LV_FLEX_FLOW_COLUMN, 4);
    u.menu_panel = p;
    lv_obj_add_style(p, &st_card, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_hor(p, 28 - ROOM, 0);
    lv_obj_set_style_pad_ver(p, 24, 0);
    lv_obj_set_size(p, 460, H);
    lv_obj_align(p, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_clickable(p, true);   /* a tap on the panel is not one beside it */
    lv_obj_set_click_focusable(p, false);
    lv_obj_set_style_pad_hor(label(p, f_l, TEXT, title), ROOM, 0);
    /* The items scroll when they do not fit (the console's font is taller);
     * the help stays at the bottom under a rule. */
    lv_obj_t *list = flex(p, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    scrolls(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, ROOM, 0);   /* the focus outline */
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = flex(list, LV_FLEX_FLOW_COLUMN, 4);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_set_style_pad_ver(row, 7, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, text[i][0]));
        /* Full width, so the subtitle wraps instead of running past the
         * panel with the console's wider font (hardware run 28). */
        lv_obj_set_width(label(row, f_s, DIM, i == dyn ? dyn_sub : text[i][1]), lv_pct(100));
        u.menu_items[i] = row;
    }
    lv_obj_t *foot = box(p);
    lv_obj_set_width(foot, lv_pct(100));
    lv_obj_set_style_pad_hor(foot, ROOM, 0);
    lv_obj_t *bar = flex(foot, LV_FLEX_FLOW_ROW, 28);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(LINE), 0);
    lv_obj_set_style_pad_top(bar, 20, 0);
    hints(bar, HINTS(menu_hints));
}

static void build_menu(lv_obj_t *s)
{
    char sub[80];
    snprintf(sub, sizeof sub, "%s, %s", rsl_ui_tr(layout_name[g.layout]), rsl_ui_tr(accent_name()));
    side_menu(s, rsl_ui_tr("菜单"), menu_text, MENU_COUNT, MENU_LAYOUT, sub);
}

static void build_grid(lv_obj_t *s, int y)
{
    lv_obj_t *grid = scroller(s, y);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, GAP, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    for (int i = 0; i < g.game_count; i++) {
        lv_obj_t *tile = flex(grid, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_width(tile, COVER_W);
        make_room(tile);
        lv_obj_set_state_trickle(tile, true);
        u.covers[i] = cover(tile, i, COVER_W, COVER_H, thumb[i]);
        focusable(u.covers[i], &st_grow);
        lv_obj_t *t = label(tile, f_s, DIM, title_of(i));
        lv_obj_set_width(t, COVER_W);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_add_style(t, &st_title_focus, LV_STATE_FOCUSED);
        u.items[i] = tile;
    }
    u.count = g.game_count;
}

/* The manifest status as a coloured pill. */
void status_pill(lv_obj_t *parent, const struct game_info *gi)
{
    if (gi->is_zip)
        pill(parent, AMBER, rsl_ui_tr("ZIP 待解压"));
    else if (!gi->state[0])
        pill(parent, DIM, rsl_ui_tr("无清单：手动复制"));
    else if (!strcmp(gi->state, "experimental"))
        pill(parent, AMBER, rsl_ui_tr("实验性"));
    else if (!strcmp(gi->state, "verified"))
        pill(parent, GREEN, rsl_ui_tr("已验证"));
    else
        pill(parent, RED, !strcmp(gi->state, "unsupported") ? "不支持" : gi->state);
}

/* Row layout: what the focused game is. Rebuilt when the
 * focus moves, never on other frames. */
void row_info(void)
{
    int i = g.sel;
    const struct game_info *gi = &g.info[i];
    char buf[200];
    u.info_sel = i;
    lv_obj_clean(u.info);
    lv_obj_t *t = label(u.info, f_hero, TEXT, title_of(i));
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *meta = flex(u.info, LV_FLEX_FLOW_ROW, 10);
    status_pill(meta, gi);
    if (gi->is_zip) {
        snprintf(buf, sizeof buf, "%.1f MB", (double)gi->zip_size / 1e6);
        pill(meta, DIM, buf);
    } else {
        snprintf(buf, sizeof buf, "Ren'Py %s", gi->renpy[0] ? gi->renpy : rsl_ui_tr("未知"));
        pill(meta, DIM, buf);
    }
    if (gi->runtime[0]) {
        snprintf(buf, sizeof buf, "%s %s", gi->runtime, gi->runtime_ok ? rsl_ui_tr("已安装") : rsl_ui_tr("缺失"));
        pill(meta, gi->runtime_ok ? GREEN : RED, buf);
    }
    const char *note = NULL;
    uint32_t color = AMBER;
    if (gi->is_zip) {
        note = rsl_ui_tr("首次运行将自动解压、处理中文路径并识别版本（完成后自动清理原压缩包）。");
        color = DIM;
    } else if (!g.game_ok[i] || gi->problem[0]) {
        snprintf(buf, sizeof buf, rsl_ui_tr("无法启动：%s"), g.game_ok[i] ? gi->problem : rsl_ui_tr("内部没有 game/ 文件夹"));
        note = buf;
        color = RED;
    } else if (gi->issues) {
        snprintf(buf, sizeof buf, gi->issues == 1 ? rsl_ui_tr("1 条性能问题：详见详情") :
                 rsl_ui_tr("%d 条性能问题：详见详情"), gi->issues);
        note = buf;
    } else if (gi->recompile) {
        note = "Ren'Py 7 游戏，已为 Ren'Py 8 重新编译。实验性。";
    }
    if (note) {
        lv_obj_t *n = label(u.info, f_m, color, note);
        lv_obj_set_width(n, lv_pct(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    }
}

/* One line of covers scrolling sideways, top at y; the focused game's
 * details go in u.info, which the caller places. */
static void build_row(lv_obj_t *s, int y, lv_style_t *grow)
{
    lv_obj_t *row = lv_obj_create(s);
    scrolls(row, LV_DIR_HOR);
    lv_obj_set_pos(row, MARGIN - ROW_ROOM, y);
    lv_obj_set_size(row, W - 2 * (MARGIN - ROW_ROOM), COVER_H + 2 * ROW_ROOM);
    lv_obj_set_style_pad_all(row, ROW_ROOM, 0);
    lv_obj_set_style_pad_column(row, ROW_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < g.game_count; i++) {
        u.covers[i] = u.items[i] = cover(row, i, COVER_W, COVER_H, thumb[i]);
        focusable(u.covers[i], grow);
    }
    u.count = g.game_count;
    u.info = flex(s, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_set_width(u.info, W - 2 * MARGIN);
}

void build_library(lv_obj_t *s)
{
    char buf[32];
    snprintf(buf, sizeof buf, rsl_ui_tr("%d 个游戏"), g.game_count);
    if (g.layout != LAYOUT_ROW || !g.game_count)
        label(u.head, f_m, DIM, buf);
    pill(u.chips, DIM, g.mode);
    if (!g.full_memory)
        pill(u.chips, AMBER, rsl_ui_tr("小程序模式：内存有限"));

    int y = HEADER_H;
    if (!g.sd_ok)
        y = banner(s, y, RED, rsl_ui_tr("SD 卡文件夹不可写：设置和日志将不会被保存。"));
    if (g.global_load == RSL_LOAD_BACKUP)
        y = banner(s, y, AMBER, rsl_ui_tr("手柄设置文件已损坏，已加载其备份副本。"));

    if (!g.game_count) {
        lv_obj_t *col = flex(s, LV_FLEX_FLOW_COLUMN, 12);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(col, f_l, TEXT, rsl_ui_tr("还没有游戏"));
        lv_obj_t *t = label(col, f_m, DIM, rsl_ui_tr("从导入器发送游戏：按 + 并选择“通过 Wi-Fi 添加游戏”。\n"
                                           "或将游戏文件夹或 .zip 压缩包复制到 " GAMES_DIR "/"));
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(t, 8, 0);
        lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    } else if (g.layout == LAYOUT_ROW) {
        /* One line of covers along the bottom, the focused game named above them. */
        build_row(s, H - FOOTER_H - COVER_H - 2 * ROW_ROOM, &st_grow_row);
        lv_obj_set_pos(u.info, MARGIN, HEADER_H + 16);
        lv_obj_set_width(u.info, 760);
    } else {
        build_grid(s, y);
    }
    build_menu(s);
}
