/* Pages the + menu opens: how to play, Layout (library layout, accent
 * colour) and Add games over Wi-Fi. */
#include "ui_internal.h"

/* How to play: what each button does in a game, with the user's own mapping. */
static const hint help_rows[] = {
    { RSL_ACT_CONFIRM, -1, "推进剧情、进行选择" },
    { RSL_ACT_UP, RSL_ACT_DOWN, "在选项之间移动" },
    { RSL_ACT_BACK, -1, "返回、取消" },
    { RSL_ACT_HIDE_UI, -1, "隐藏对话框" },
    { RSL_ACT_HISTORY, -1, "对话历史" },
    { RSL_ACT_ROLLBACK, RSL_ACT_ROLLFORWARD, "回退 / 快进" },
    { RSL_ACT_AUTO, -1, "自动前进 开/关" },
    { RSL_ACT_SKIP, -1, "跳过（按住时）" },
    { RSL_ACT_GAME_MENU, -1, "游戏菜单：存档、读档" },
    { RSL_ACT_QUICK_MENU, -1, "快速菜单" },
    { RSL_ACT_CLICK, -1, "鼠标点击" },
    { RSL_ACT_OVERLAY, -1, "加载器覆盖层" },
};

static const char *const layout_text[LAYOUT_COUNT] = {
    "封面沿底部排成一行，类似 HOME 菜单。",
    "每行五个封面并显示标题。适合快速浏览长列表。",
};

/* ------------------------------------------------------------- how to play */

/* One card of the guide: title and text on the left, a small drawing on the
 * right. Returns the drawing's area (320 x 200). */
static lv_obj_t *guide_card(lv_obj_t *list, int index, const char *title, const char *text, lv_obj_t **col)
{
    lv_obj_t *c = flex(list, LV_FLEX_FLOW_ROW, 32);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_set_style_pad_all(c, 28, 0);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    focusable(c, NULL);
    lv_obj_t *left = flex(c, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_flex_grow(left, 1);
    focus_text(c, label(left, f_l, TEXT, title));
    if (text)
        para(left, f_m, DIM, text);
    lv_obj_t *art = box(c);
    lv_obj_set_size(art, 320, 200);
    lv_obj_set_style_bg_color(art, lv_color_hex(SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(art, 16, 0);
    lv_obj_set_style_clip_corner(art, true, 0);
    u.items[index] = c;
    if (col)
        *col = left;
    return art;
}

static void place(lv_obj_t *o, int x, int y)
{
    lv_obj_set_pos(o, x, y);
}

/* How to play: shown on the first start and from the + menu. A short guide
 * in cards, each with a drawing; the buttons follow the user's mapping. */
void build_help(lv_obj_t *s)
{
    char buf[160];
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 18, 0);
    lv_obj_t *col, *art, *o;

    /* 1. In a game: the buttons, in two columns, and a dialogue box. */
    art = guide_card(list, 0, rsl_ui_tr("游戏中"), NULL, &col);
    lv_obj_t *keys = flex(col, LV_FLEX_FLOW_ROW_WRAP, 14);
    lv_obj_set_width(keys, lv_pct(100));
    for (size_t i = 0; i < sizeof help_rows / sizeof help_rows[0]; i++) {
        lv_obj_t *row = flex(keys, LV_FLEX_FLOW_ROW, 10);
        lv_obj_set_width(row, 380);
        lv_obj_t *btns = flex(row, LV_FLEX_FLOW_ROW, 4);
        lv_obj_set_width(btns, 74);
        button_chip(btns, &g.in.cfg, key(help_rows[i].a));
        if (help_rows[i].b >= 0)
            button_chip(btns, &g.in.cfg, key(help_rows[i].b));
        /* The rest of the row, so a line the console's wider font cannot fit
         * wraps instead of being cut off (hardware run 28). */
        lv_obj_set_flex_grow(label(row, f_m, TEXT, help_rows[i].text), 1);
    }
    snprintf(buf, sizeof buf, rsl_ui_tr("%s 摇杆移动鼠标光标。按键遵循你的手柄设置。"),
             g.in.cfg.cursor_stick == RSL_STICK_RIGHT ? rsl_ui_tr("右") : rsl_ui_tr("左"));
    para(col, f_s, DIM, buf);
    shape(art, 0, 0, 320, 200, tint(0), LV_OPA_40, 0);                 /* the scene */
    shape(art, 120, 30, 80, 110, ACCENT, LV_OPA_30, 40);                /* someone */
    o = shape(art, 16, 112, 288, 72, BG, LV_OPA_80, 12);                /* the text box */
    pill(art, ACCENT, "Eileen");
    place(lv_obj_get_child(art, lv_obj_get_child_count(art) - 1), 28, 96);
    shape(o, 14, 26, 200, 8, DIM, LV_OPA_COVER, 4);
    shape(o, 14, 44, 140, 8, DIM, LV_OPA_COVER, 4);
    place(button_chip(art, &g.in.cfg, key(RSL_ACT_CONFIRM)), 266, 146);

    /* 2. Full memory: the HOME menu, hold R on a game. */
    art = guide_card(list, 1, rsl_ui_tr("以完整内存开始"),
                     "从 HOME 菜单打开任意游戏时按住 R 键，直到 Homebrew Menu 出现，"
                     "然后打开加载器。如果从相册打开，加载器内存不足，游戏无法启动。", NULL);
    for (int k = 0; k < 3; k++) {
        o = shape(art, 30 + k * 92, 34, 76, 76, k ? tint(k + 1) : tint(0), LV_OPA_COVER, 10);
        if (!k) {
            lv_obj_set_style_outline_color(o, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_outline_width(o, 3, 0);
            lv_obj_set_style_outline_pad(o, 3, 0);
        }
    }
    o = flex(art, LV_FLEX_FLOW_ROW, 10);
    place(o, 30, 136);
    button_chip(o, &g.in.cfg, RSL_BTN_R);
    label(o, f_m, TEXT, rsl_ui_tr("按住   +"));
    button_chip(o, &g.in.cfg, RSL_BTN_A);

    /* 3. Adding games: computer, arrow, SD card. */
    art = guide_card(list, 2, rsl_ui_tr("添加游戏"),
                     rsl_ui_tr("在电脑上用桌面导入器导出每个游戏：用 + 菜单中的《通过 Wi-Fi 添加游戏》发送，"
                     "或将其复制到 SD 卡的 switch/renpy-loader/games/。封面和详情会一并带来。"), NULL);
    o = shape(art, 24, 40, 130, 84, BG, LV_OPA_COVER, 8);              /* laptop screen */
    lv_obj_set_style_border_color(o, lv_color_hex(DIM), 0);
    lv_obj_set_style_border_width(o, 3, 0);
    shape(o, 14, 16, 70, 10, ACCENT, LV_OPA_COVER, 5);
    shape(o, 14, 36, 90, 8, DIM, LV_OPA_COVER, 4);
    shape(art, 10, 126, 158, 10, DIM, LV_OPA_COVER, 5);                /* its base */
    place(label(art, f_xl, ACCENT, "→"), 182, 64);
    o = shape(art, 232, 36, 66, 90, DIM, LV_OPA_COVER, 8);              /* SD card */
    shape(o, 48, -2, 20, 20, SURFACE_HI, LV_OPA_COVER, 0);              /* its notch */
    shape(o, 12, 56, 42, 22, BG, LV_OPA_60, 4);
    place(label(art, f_s, DIM, "games/"), 236, 140);

    /* 4. The + menu: layouts and colours. */
    art = guide_card(list, 3, rsl_ui_tr("个性化设置"),
                     rsl_ui_tr("在游戏库按 + 打开菜单：在《布局》中选择横排或网格以及强调色；"
                     "也可以随时调整手柄映射。"), NULL);
    for (int k = 0; k < 4; k++) {
        o = shape(art, 26 + k * 64 - (k == 1 ? 6 : 0), k == 1 ? 18 : 26, k == 1 ? 60 : 48, k == 1 ? 88 : 72,
                  tint(k), LV_OPA_COVER, 8);
        if (k == 1) {
            lv_obj_set_style_outline_color(o, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_outline_width(o, 3, 0);
            lv_obj_set_style_outline_pad(o, 2, 0);
        }
    }
    shape(art, 26, 124, 120, 12, ACCENT, LV_OPA_COVER, 6);
    for (int k = 0; k < 5; k++)
        shape(art, 26 + k * 40, 152, 26, 26, presets[k].rgb, LV_OPA_COVER, LV_RADIUS_CIRCLE);

    /* 5. Emergency combo: + and -, held for 3 s. */
    art = guide_card(list, 4, rsl_ui_tr("按键失灵了？"),
                     rsl_ui_tr("在加载器或游戏中的任意界面，同时按住 + 和 - 3 秒："
                     "默认控制即刻恢复，并会被保存。"), NULL);
    o = flex(art, LV_FLEX_FLOW_ROW, 10);
    place(o, 30, 82);
    button_chip(o, &g.in.cfg, RSL_BTN_PLUS);
    label(o, f_m, TEXT, "+");
    button_chip(o, &g.in.cfg, RSL_BTN_MINUS);
    o = shape(art, 188, 46, 108, 108, BG, LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(o, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(o, 8, 0);
    lv_obj_center(label(o, f_l, TEXT, "3 s"));
    u.count = HELP_COUNT;
}

/* ------------------------------------------------------------------ layout */

/* A wireframe of layout l in a 328 x 200 box. White, not the accent: the
 * page follows a new accent live, without being rebuilt. */
#define SK(v) ((v) * box / 328)        /* the wireframe was drawn 328 px wide */

static void skeleton(lv_obj_t *p, int l, int box)
{
    if (l == LAYOUT_ROW) {   /* the focused game is named above the covers */
        shape(p, SK(16), SK(20), SK(130), SK(14), TEXT, LV_OPA_COVER, 7);
        shape(p, SK(16), SK(42), SK(50), SK(10), AMBER, LV_OPA_70, 5);
        shape(p, SK(72), SK(42), SK(50), SK(10), DIM, LV_OPA_70, 5);
    }
    for (int k = 0; k < (l == LAYOUT_GRID ? 10 : 6); k++) {
        int x, y, w = 44, h = 66;
        if (l == LAYOUT_GRID) {
            x = 18 + k % 5 * 60;
            y = 16 + k / 5 * 92;
        } else {
            x = 16 + k * 54;
            y = 114;
        }
        if (l == LAYOUT_ROW && k == 1) {
            x -= 4;
            y -= 6;
            w += 8;
            h += 12;
        }
        lv_obj_t *o = shape(p, SK(x), SK(y), SK(w), SK(h), tint(k), LV_OPA_COVER, 6);
        if (k == (l == LAYOUT_ROW ? 1 : 0)) {
            lv_obj_set_style_outline_color(o, lv_color_hex(TEXT), 0);
            lv_obj_set_style_outline_width(o, 2, 0);
            lv_obj_set_style_outline_pad(o, 2, 0);
        }
        if (l == LAYOUT_GRID)
            shape(p, SK(x), SK(y + h + 6), SK(36), SK(6), DIM, LV_OPA_COVER, 3);
    }
}

/* One list, items in g.sel order: the two layouts, the colour presets,
 * the custom hue row. */
void build_layout(lv_obj_t *s)
{
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);

    heading(list, rsl_ui_tr("游戏库"), 0);
    /* The two layouts side by side, each card split in half: its wireframe on
     * the left, its name and what it does on the right. Full width, and the
     * colour row still fits under them. */
    lv_obj_t *cards = flex(list, LV_FLEX_FLOW_ROW, 24);
    lv_obj_set_width(cards, lv_pct(100));
    make_room(cards);
    for (int l = 0; l < LAYOUT_COUNT; l++) {
        lv_obj_t *c = flex(cards, LV_FLEX_FLOW_ROW, 20);
        lv_obj_add_style(c, &st_card, 0);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_height(c, 150 + 2 * 20);   /* the wireframe and its padding: both cards alike */
        lv_obj_set_style_pad_all(c, 20, 0);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        focusable(c, NULL);
        lv_obj_add_style(c, &st_focus_fill, LV_STATE_FOCUSED);
        lv_obj_t *p = box(c);
        lv_obj_set_size(p, 246, 150);
        lv_obj_set_style_bg_color(p, lv_color_hex(BG), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(p, 12, 0);
        lv_obj_set_style_clip_corner(p, true, 0);
        skeleton(p, l, 246);
        lv_obj_t *col = flex(c, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_t *t = flex(col, LV_FLEX_FLOW_ROW_WRAP, 10);
        lv_obj_set_width(t, lv_pct(100));
        focus_text(c, label(t, f_l, TEXT, layout_name[l]));
        if (l == g.layout)
            pill(t, TEXT, rsl_ui_tr("使用中"));
        para(col, f_s, DIM, layout_text[l]);
        u.items[l] = c;
    }

    lv_obj_t *row = flex(list, LV_FLEX_FLOW_ROW, 16);   /* the heading, and a warning beside it */
    heading(row, rsl_ui_tr("颜色"), 28);
    u.warn = label(row, f_s, AMBER, rsl_ui_tr("接近问题所用的红色：焦点与错误可能看起来相似。"));
    lv_obj_set_style_pad_top(u.warn, 28, 0);
    lv_obj_set_style_pad_bottom(u.warn, 8, 0);
    row = flex(list, LV_FLEX_FLOW_ROW, 30);
    make_room(row);
    lv_obj_set_style_pad_left(row, 8, 0);
    for (int i = 0; i < ACCENT_PRESETS; i++) {
        lv_obj_t *col = flex(row, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(col, 104);
        make_room(col);
        lv_obj_t *dot = shape(col, 0, 0, 64, 64, presets[i].rgb, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(dot, lv_color_hex(TEXT), 0);
        focusable(dot, &st_grow);
        focus_text(col, label(col, f_s, DIM, presets[i].name));
        u.swatch[i] = dot;
        u.items[SET_PRESET + i] = col;
    }

    lv_obj_t *custom = flex(list, LV_FLEX_FLOW_ROW, 28);
    lv_obj_set_size(custom, lv_pct(100), 64);
    lv_obj_add_style(custom, &st_row, 0);
    focusable(custom, NULL);
    lv_obj_add_style(custom, &st_focus_fill, LV_STATE_FOCUSED);
    focus_text(custom, label(custom, f_m, TEXT, rsl_ui_tr("自定义色相")));
    lv_obj_t *strip = box(custom);
    lv_obj_set_size(strip, 720, 24);
    lv_obj_set_overflow_visible(strip, true);
    for (int k = 0; k < 24; k++)
        shape(strip, k * 30, 0, 30, 24, ui_hue_color(k * 15), LV_OPA_COVER, k == 0 || k == 23 ? 8 : 0);
    u.knob = shape(strip, 0, -6, 12, 36, TEXT, LV_OPA_COVER, 6);
    lv_obj_set_style_border_color(u.knob, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(u.knob, 3, 0);
    u.hue_text = label(custom, f_m, TEXT, "");
    u.items[SET_CUSTOM] = custom;
    u.count = SET_COUNT;
    u.accent_drawn = ~g.accent;
    u.hue_drawn = -1;
}

void update_layout(void)
{
    if (u.hue_drawn != g.accent_hue || u.accent_drawn != g.accent) {
        u.hue_drawn = g.accent_hue;
        u.accent_drawn = g.accent;
        lv_obj_set_x(u.knob, g.accent_hue * 720 / 360 - 6);
        set_textf(u.hue_text, "%d°", g.accent_hue);
        for (int i = 0; i < ACCENT_PRESETS; i++)
            lv_obj_set_style_border_width(u.swatch[i], presets[i].rgb == g.accent ? 4 : 0, 0);
        int d = (g.accent_hue - ERROR_HUE + 540) % 360 - 180;
        lv_obj_set_hidden(u.warn, d < -25 || d > 25 || g.accent != ui_hue_color(g.accent_hue));
    }
}

/* ---------------------------------------------------- add games over Wi-Fi */

static const hint arrived_hints[] = {
    { RSL_ACT_LEFT, RSL_ACT_RIGHT, "选择" }, { RSL_ACT_CONFIRM, -1, "选择" }, { RSL_ACT_BACK, -1, "关闭" },
};

/* "Game arrived", over the page when the importer has moved a game into
 * place: go and play it, or stay for the next one. */
static void build_arrived(lv_obj_t *s)
{
    lv_obj_t *c = dialog_card(s);
    u.arrived_title = label(c, f_l, GREEN, "");
    para(c, f_m, TEXT, rsl_ui_tr("它已在 SD 卡上。前往游戏库开始游玩，或留在此处继续从导入器发送下一个游戏。"));
    lv_obj_t *row = flex(c, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.dialog_btn[ARRIVED_MORE] = button(row, rsl_ui_tr("再发一个游戏"), 300);
    u.dialog_btn[ARRIVED_LIBRARY] = button(row, rsl_ui_tr("前往游戏库"), 300);
    u.dialog_n = ARRIVED_COUNT;
    hints(flex(c, LV_FLEX_FLOW_ROW, 28), HINTS(arrived_hints));
}

/* The address to type into the importer, and what the server is doing:
 * transfer.c copies its state into g.ftp every frame, and only what changed
 * is redrawn. Without Wi-Fi the status line says why. */
void build_ftp(lv_obj_t *s)
{
    bool on = g.transfer_usb ? g.ftp.running : g.ftp_addr[0] != '\0';
    lv_obj_t *c = card(s, NULL, MARGIN, BODY_Y, W - 2 * MARGIN, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(c, 12, 0);
    if (g.transfer_usb)
        label(c, f_hero, on ? TEXT : RED, "USB MTP");
    else {
        label(c, f_m, DIM, rsl_ui_tr("导入器地址"));
        label(c, f_hero, on ? TEXT : RED, on ? g.ftp_addr : rsl_ui_tr("无 Wi-Fi"));
    }
    u.ftp_note = note(c, GREEN, "");   /* shown when a game has arrived */
    lv_obj_set_style_text_font(lv_obj_get_child(u.ftp_note, 0), f_l, 0);
    lv_obj_set_hidden(u.ftp_note, true);
    if (on) {
        const char *help = g.transfer_usb
            ? "用 USB 数据线连接电脑，在导入器中查找并选择 rpynx 设备。传输完成后再离开本页。"
            : "在电脑上打开 rpynx 导入器，在《通过 Wi-Fi 连接 Switch（FTP）》下输入此地址并发送游戏。"
              "发送完成前请保持本页面打开：离开后即可在游戏库中看到它们。";
        u.ftp_help = para(c, f_m, DIM, help);
    }
    u.ftp_state = pill(fact(c, "服务器"), DIM, "");
    u.ftp_game = label(fact(c, "游戏"), f_m, TEXT, "");
    lv_obj_t *v = fact(c, "进度");
    lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    u.ftp_bar = meter(v, 600);
    lv_bar_set_range(u.ftp_bar, 0, 1000);
    u.ftp_progress = label(v, f_m, TEXT, "");
    u.ftp_file = label(fact(c, rsl_ui_tr("文件")), f_m, TEXT, "");
    lv_obj_set_width(u.ftp_file, 900);
    lv_label_set_long_mode(u.ftp_file, LV_LABEL_LONG_MODE_DOTS);
    u.ftp_count = label(fact(c, rsl_ui_tr("合计")), f_m, TEXT, "");
    build_arrived(s);
    update_ftp();
}

void update_ftp(void)
{
    const rsl_ftpd_status *f = &g.ftp;
    char state[64];
    bool on = g.transfer_usb || g.ftp_addr[0] != '\0';
    uint32_t color = !on ? DIM : !f->running ? RED : f->connected ? GREEN : DIM;
    snprintf(state, sizeof state, "%s%s", rsl_ui_tr(!on ? "已关闭" : !f->running ? "已停止" :
             f->connected ? "已连接：" : "等待导入器"), f->running ? f->client : "");
    if (strcmp(lv_label_get_text(u.ftp_state), state)) {
        lv_label_set_text(u.ftp_state, state);
        lv_obj_set_style_text_color(u.ftp_state, lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(u.ftp_state, lv_color_hex(color), 0);
    }
    /* The game: the importer uploads into games/.uploading-<id>/ after
     * announcing the size (ftpd.h), and renames that folder when it is done. */
    const char *up = strstr(f->file, "/.uploading-");
    if (!f->connected || (!g.transfer_usb && (u.ftp_from != f->expect_from || !f->expect))) {
        u.ftp_from = f->expect_from;
        u.ftp_id[0] = '\0';
    }
    if (up) {
        up += sizeof "/.uploading-" - 1;
    } else if (g.transfer_usb) {
        up = strstr(f->file, "/games/");
        if (up) up += sizeof "/games/" - 1;
    }
    if (g.transfer_usb || up)
        snprintf(u.ftp_id, sizeof u.ftp_id, "%.*s", up ? (int)strcspn(up, "/") : 0, up ? up : "");
    if (u.ftp_id[0])
        set_text(u.ftp_game, u.ftp_id);
    else if (f->done[0])
        set_textf(u.ftp_game, rsl_ui_tr("%s，已接收"), f->done);
    else
        set_text(u.ftp_game, rsl_ui_tr("无"));
    uint64_t got = f->bytes - f->expect_from;
    int32_t permille = !f->expect ? (f->done[0] ? 1000 : 0) : got >= f->expect ? 1000 : (int32_t)(got * 1000 / f->expect);
    lv_bar_set_value(u.ftp_bar, permille, LV_ANIM_OFF);
    if (f->expect)
        set_textf(u.ftp_progress, "%.1f / %.1f MB", (double)got / 1e6, (double)f->expect / 1e6);
    else
        set_text(u.ftp_progress, f->done[0] ? rsl_ui_tr("完成") : "");

    /* Until the next upload starts (ALLO clears done). */
    bool arrived = f->done[0] && g.ftp_title[0];
    if (arrived) {
        set_textf(lv_obj_get_child(u.ftp_note, 0), rsl_ui_tr("%s 已到达。按 B 在游戏库中查看，或继续发送下一个游戏。"), g.ftp_title);
        set_textf(u.arrived_title, rsl_ui_tr("%s 已到达"), g.ftp_title);
    }
    if (arrived != u.ftp_note_shown) {
        u.ftp_note_shown = arrived;
        lv_obj_set_hidden(u.ftp_note, !arrived);
        if (u.ftp_help)
            lv_obj_set_hidden(u.ftp_help, arrived);
        if (arrived)
            lv_obj_fade_in(u.ftp_note, 200, 0);
    }

    const char *name = strrchr(f->file, '/');
    set_text(u.ftp_file, name ? name + 1 : f->file[0] ? f->file : rsl_ui_tr("无"));
    uint32_t elapsed = g.now - u.ftp_rate_at;
    if (!f->connected || !u.ftp_rate_at || f->bytes < u.ftp_rate_bytes) {
        u.ftp_rate = 0;
        u.ftp_rate_at = g.now;
        u.ftp_rate_bytes = f->bytes;
    } else if (elapsed >= 500) {
        u.ftp_rate = (double)(f->bytes - u.ftp_rate_bytes) / (1000.0 * elapsed);
        u.ftp_rate_at = g.now;
        u.ftp_rate_bytes = f->bytes;
    }
    set_textf(u.ftp_count, rsl_ui_tr("%u 个文件，%.1f MB · %.2f MB/s"),
              f->files, (double)f->bytes / 1e6, u.ftp_rate);
}

/* ----------------------------------------------------------- unpack .zip */

void build_unpack(lv_obj_t *s)
{
    lv_obj_t *c = card(s, NULL, MARGIN, BODY_Y, W - 2 * MARGIN, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(c, 14, 0);
    label(c, f_hero, TEXT, g.unpack.title[0] ? g.unpack.title : g.unpack.zip_name);
    para(c, f_m, DIM, rsl_ui_tr("正在解压游戏文件、打包非 ASCII 资源并自动识别 Ren'Py 版本。请勿关闭电源或拔出 SD 卡。"));
    u.unpack_stage = pill(fact(c, rsl_ui_tr("阶段")), AMBER, "");
    lv_obj_t *v = fact(c, rsl_ui_tr("进度"));
    lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    u.unpack_bar = meter(v, 600);
    lv_bar_set_range(u.unpack_bar, 0, 1000);
    u.unpack_progress = label(v, f_m, TEXT, "");
    u.unpack_file = label(fact(c, rsl_ui_tr("文件")), f_m, TEXT, "");
    lv_obj_set_width(u.unpack_file, 900);
    lv_label_set_long_mode(u.unpack_file, LV_LABEL_LONG_MODE_DOTS);
    u.unpack_count = label(fact(c, rsl_ui_tr("合计")), f_m, TEXT, "");
    update_unpack();
}

void update_unpack(void)
{
    const rsl_unpack_status *p = &g.unpack;
    set_text(u.unpack_stage, p->stage[0] ? p->stage : rsl_ui_tr("正在准备解压"));
    int32_t permille = p->total_bytes ? (p->done_bytes >= p->total_bytes ? 1000 :
                       (int32_t)(p->done_bytes * 1000 / p->total_bytes)) : 0;
    lv_bar_set_value(u.unpack_bar, permille, LV_ANIM_OFF);
    if (p->total_bytes)
        set_textf(u.unpack_progress, "%.1f / %.1f MB (%d%%)",
                  (double)p->done_bytes / 1e6, (double)p->total_bytes / 1e6, (int)(permille / 10));
    else
        set_text(u.unpack_progress, "...");

    const char *name = strrchr(p->file, '/');
    set_text(u.unpack_file, name ? name + 1 : p->file[0] ? p->file : rsl_ui_tr("无"));

    uint32_t elapsed = g.now - u.unpack_rate_at;
    if (!u.unpack_rate_at || p->done_bytes < u.unpack_rate_bytes) {
        u.unpack_rate = 0;
        u.unpack_rate_at = g.now;
        u.unpack_rate_bytes = p->done_bytes;
    } else if (elapsed >= 500) {
        u.unpack_rate = (double)(p->done_bytes - u.unpack_rate_bytes) / (1000.0 * elapsed);
        u.unpack_rate_at = g.now;
        u.unpack_rate_bytes = p->done_bytes;
    }
    set_textf(u.unpack_count, rsl_ui_tr("%u / %u 个文件，%.1f MB · %.2f MB/s"),
              p->done_files, p->total_files, (double)p->done_bytes / 1e6, u.unpack_rate);
}
