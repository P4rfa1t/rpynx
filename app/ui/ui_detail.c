/* Game details: the facts beside the big cover, the buttons that stay at
 * the bottom right, and the applet-mode notice. */
#include "ui_internal.h"

static const hint dialog_hints[] = { { RSL_ACT_CONFIRM, -1, "确定" }, { RSL_ACT_BACK, -1, "返回" } };

static const hint detail_scroll_hints[] = {
    { RSL_ACT_HIDE_UI, -1, "删除游戏" }, { RSL_ACT_UP, RSL_ACT_DOWN, "滚动" },
    { RSL_ACT_CONFIRM, -1, "选择" }, { RSL_ACT_BACK, -1, "返回" },
};

static void sync_detail_dialog(int kind)
{
    int i = g.game;
    char text[512];
    if (kind == DLG_DELETE && i >= 0 && i < g.game_count) {
        const struct game_info *gi = &g.info[i];
        set_text(u.dialog_title, rsl_ui_tr("删除游戏"));
        if (gi->is_zip) {
            snprintf(text, sizeof text, rsl_ui_tr("确定要删除压缩包“%s”吗？\n\n将会从 SD 卡删除 games/%s（%.1f MB）。"),
                     title_of(i), g.games[i], (double)gi->zip_size / 1e6);
        } else {
            snprintf(text, sizeof text, rsl_ui_tr("确定要删除游戏“%s”吗？\n\n将会从 SD 卡删除 games/%s 文件夹及内部资源；你的存档（saves/）会被保留。"),
                     title_of(i), g.games[i]);
        }
        lv_label_set_text(u.dialog_body, text);
        set_text(u.dialog_btn[DEL_CANCEL], "取消");
        lv_obj_set_width(u.dialog_btn[DEL_CANCEL], 180);
        lv_obj_set_hidden(u.dialog_btn[DEL_CANCEL], false);
        set_text(u.dialog_btn[DEL_CONFIRM], "确认删除");
        lv_obj_set_width(u.dialog_btn[DEL_CONFIRM], 200);
        lv_obj_set_style_text_color(u.dialog_btn[DEL_CONFIRM], lv_color_hex(RED), 0);
        lv_obj_set_style_text_color(u.dialog_btn[DEL_CONFIRM], lv_color_hex(TEXT), LV_STATE_FOCUSED);
        u.dialog_n = DEL_COUNT;
    } else {
        set_text(u.dialog_title, rsl_ui_tr("游戏需要完整内存"));
        snprintf(text, sizeof text, rsl_ui_tr("加载器是从相册打开的小程序，只有 %llu MiB 内存。Ren'Py 游戏在这种模式下会导致整台主机崩溃，因此这里不会启动。\n\n"
                 "要游玩：请关闭加载器，然后在从 HOME 菜单打开任意游戏时按住 R 键，加载器将以完整内存启动。"),
                 (unsigned long long)(g.mem_total >> 20));
        lv_label_set_text(u.dialog_body, text);
        lv_obj_set_hidden(u.dialog_btn[DEL_CANCEL], true);
        set_text(u.dialog_btn[0], "确定");
        lv_obj_set_width(u.dialog_btn[0], 240);
        lv_obj_set_style_text_color(u.dialog_btn[0], lv_color_hex(TEXT), 0);
        lv_obj_remove_local_style_prop(u.dialog_btn[0], LV_STYLE_TEXT_COLOR, LV_STATE_FOCUSED);
        u.dialog_n = 1;
    }
    u.dialog_focused = -1;
    u.dialog_kind_drawn = kind;
}

/* Applet-mode notice and delete-game confirmation share the modal dialog on the detail page. */
static void build_dialog(lv_obj_t *s)
{
    lv_obj_t *c = dialog_card(s);
    u.dialog_title = label(c, f_l, RED, "");
    u.dialog_body = para(c, f_m, TEXT, "");
    lv_obj_t *row = flex(c, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.dialog_btn[DEL_CANCEL] = button(row, rsl_ui_tr("取消"), 180);
    u.dialog_btn[DEL_CONFIRM] = button(row, rsl_ui_tr("确定"), 240);
    hints(flex(c, LV_FLEX_FLOW_ROW, 28), HINTS(dialog_hints));
    sync_detail_dialog(g.dialog_open ? g.dialog_kind : DLG_APPLET);
}

/* A play triangle filling the object, in its (inherited) text colour: the
 * console's text font has no ▶. */
static void draw_play(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_draw_triangle_dsc_t d;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_obj_get_style_text_color(o, 0);
    d.p[0].x = d.p[2].x = a.x1;
    d.p[0].y = a.y1;
    d.p[1].x = a.x2;
    d.p[1].y = (a.y1 + a.y2) / 2;
    d.p[2].y = a.y2;
    lv_draw_triangle(lv_event_get_layer(e), &d);
}

/* button() with a play icon before the text; both follow the focus colour. */
static lv_obj_t *play_button(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *b = flex(parent, LV_FLEX_FLOW_ROW, 14);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_color(b, lv_color_hex(TEXT), 0);
    lv_obj_add_style(b, &st_button, 0);
    focusable(b, NULL);
    lv_obj_add_style(b, &st_focus_fill, LV_STATE_FOCUSED);
    lv_obj_set_width(b, width);
    lv_obj_t *ic = box(b);
    lv_obj_set_size(ic, 16, 18);
    lv_obj_add_event_cb(ic, draw_play, LV_EVENT_DRAW_MAIN, NULL);
    lv_label_set_text(lv_label_create(b), text);
    return b;
}

/* Up and down on the detail page: the facts can run longer than the room
 * above the buttons. */
void ui_scroll(int dir)
{
    if (u.screen == SCR_DETAIL && u.info)
        lv_obj_scroll_by_bounded(u.info, 0, -dir * 160, LV_ANIM_ON);
}

void build_detail(lv_obj_t *s)
{
    int i = g.game;
    const struct game_info *gi = &g.info[i];
    const char *problem = !g.game_ok[i] ? rsl_ui_tr("此文件夹中没有 game/ 子文件夹。请复制完整的导出游戏文件夹。") : gi->problem;
    char buf[300];

    lv_obj_t *c = cover(s, i, BIG_COVER_W, BIG_COVER_H, detail_cover(i));
    lv_obj_set_style_clip_corner(c, true, 0);   /* the backdrop shows around the corners */
    lv_obj_set_pos(c, MARGIN, BODY_Y);

    /* Bottom right, whatever the length of the facts: the buttons, Launch last. */
    lv_obj_t *row = flex(s, LV_FLEX_FLOW_ROW, 14);
    make_room(row);
    u.items[DETAIL_POWER] = button(row, gi->power_saver ? rsl_ui_tr("30fps：开") : rsl_ui_tr("30fps：关"), 176);
    u.power_btn_label = lv_obj_get_child(u.items[DETAIL_POWER], 0);
    u.power_drawn = gi->power_saver;
    u.items[DETAIL_PICTURE] = button(row, rsl_ui_tr("更换图片"), 176);
    u.items[DETAIL_CONTROLS] = button(row, rsl_ui_tr("手柄设置"), 190);
    u.items[DETAIL_LAUNCH] = play_button(row, gi->is_zip ? rsl_ui_tr("解压游戏") :
                                         problem[0] ? rsl_ui_tr("无法启动") : rsl_ui_tr("启动"), 220);
    if (!gi->is_zip && problem[0]) {   /* dim even when focused */
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), 0);
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), LV_STATE_FOCUSED);
    }
    if (gi->is_zip) {
        for (int k = DETAIL_CONTROLS; k < DETAIL_COUNT; k++) {
            lv_obj_set_style_text_color(u.items[k], lv_color_hex(DIM), 0);
            lv_obj_set_style_text_color(u.items[k], lv_color_hex(DIM), LV_STATE_FOCUSED);
        }
    }
    u.count = DETAIL_COUNT;
    lv_obj_align(row, LV_ALIGN_BOTTOM_RIGHT, -MARGIN, -(H - BODY_Y - BODY_H));
    lv_obj_update_layout(row);

    /* Right of the cover, above the buttons: what the manifest says. */
    int x = MARGIN + BIG_COVER_W + 40;
    lv_obj_t *col = u.info = flex(s, LV_FLEX_FLOW_COLUMN, 14);
    scrolls(col, LV_DIR_VER);
    lv_obj_set_pos(col, x, BODY_Y - 6);
    lv_obj_set_size(col, W - MARGIN - x, lv_obj_get_y(row) - 24 - (BODY_Y - 6));
    para(col, f_xl, TEXT, title_of(i));
    snprintf(buf, sizeof buf, "games/%s", g.games[i]);
    lv_obj_set_style_pad_bottom(label(col, f_s, DIM, buf), 6, 0);

    if (gi->is_zip) {
        snprintf(buf, sizeof buf, "%.1f MB", (double)gi->zip_size / 1e6);
        label(fact(col, rsl_ui_tr("大小")), f_m, TEXT, buf);
    } else {
        lv_obj_t *v = fact(col, "Ren'Py");
        label(v, f_m, TEXT, gi->renpy[0] ? gi->renpy : rsl_ui_tr("未知"));
    }
    status_pill(fact(col, rsl_ui_tr("状态")), gi);
    lv_obj_t *v = fact(col, rsl_ui_tr("运行时"));
    label(v, f_m, TEXT, gi->runtime[0] ? gi->runtime : rsl_ui_tr("无"));
    if (gi->runtime[0])
        pill(v, gi->runtime_ok ? GREEN : RED, gi->runtime_ok ? rsl_ui_tr("已安装") : rsl_ui_tr("缺失"));

    if (gi->is_zip) {
        note(col, AMBER, rsl_ui_tr("点击“解压游戏”将自动提取游戏文件、打包中文路径资源并识别 Ren'Py 7/8 运行时；成功后会自动清理原 .zip 文件。"));
    }
    if (gi->recompile)
        note(col, AMBER, "Ren'Py 7 游戏，已从 .rpy 源码为 Ren'Py 8 重新编译。实验性。");
    if (!gi->is_zip && problem[0]) {
        snprintf(buf, sizeof buf, rsl_ui_tr("无法启动：%s"), problem);
        note(col, RED, buf);
    }
    if (gi->issues) {
        snprintf(buf, sizeof buf, gi->issues == 1 ? rsl_ui_tr("导入器发现 %d 条性能问题：") :
                 rsl_ui_tr("导入器发现 %d 条性能问题："), gi->issues);
        lv_obj_t *n = note(col, AMBER, buf);
        for (int k = 0; k < gi->issues && k < 3; k++) {
            snprintf(buf, sizeof buf, "-  %s", gi->issue[k]);
            para(n, f_s, DIM, buf);
        }
        if (gi->issues > 3)
            label(n, f_s, DIM, rsl_ui_tr("-  ……详见导入器的报告"));
    }
    lv_obj_update_layout(col);
    if (lv_obj_get_scroll_bottom(col) > 0) {   /* longer than the room: say how to see the rest */
        u.hint = detail_scroll_hints;
        u.hint_n = (int)(sizeof detail_scroll_hints / sizeof detail_scroll_hints[0]);
        hints(u.hints, u.hint, u.hint_n);
    }
    build_dialog(s);
}

void update_detail(void)
{
    int i = g.game;
    if (g.dialog_open && u.dialog_kind_drawn != g.dialog_kind)
        sync_detail_dialog(g.dialog_kind);
    if (i < 0 || i >= g.game_count || !u.power_btn_label)
        return;
    bool cur = g.info[i].power_saver;
    if (cur != u.power_drawn) {
        u.power_drawn = cur;
        set_text(u.power_btn_label, cur ? rsl_ui_tr("30fps：开") : rsl_ui_tr("30fps：关"));
    }
}
