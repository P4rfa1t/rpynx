/* The picture page (Change picture): zoom and pan the game's cover artwork
 * inside a fixed 2:3 frame centered on screen. */
#include "ui_internal.h"

#define PIC_FRAME_W 360
#define PIC_FRAME_H 540
#define PIC_FRAME_X ((W - PIC_FRAME_W) / 2)
#define PIC_FRAME_Y (HEADER_H + (H - HEADER_H - FOOTER_H - PIC_FRAME_H) / 2)

static lv_draw_buf_t shot;
static bool dragging;
static lv_point_t last_pt;
static int drag_dx, drag_dy;

static void on_touch_press(lv_event_t *e)
{
    (void)e;
    lv_indev_get_point(lv_indev_active(), &last_pt);
    dragging = true;
}

static void on_touch_drag(lv_event_t *e)
{
    lv_point_t p;
    (void)e;
    lv_indev_get_point(lv_indev_active(), &p);
    if (!dragging) {
        last_pt = p;
        dragging = true;
        return;
    }
    drag_dx += p.x - last_pt.x;
    drag_dy += p.y - last_pt.y;
    last_pt = p;
}

static void on_touch_release(lv_event_t *e)
{
    (void)e;
    dragging = false;
}

void build_picture(lv_obj_t *s)
{
    dragging = false;
    drag_dx = drag_dy = 0;

    lv_draw_buf_init(&shot, g.shot_w, g.shot_h, LV_COLOR_FORMAT_XRGB8888, g.shot_w * 4, g.shot,
                     (uint32_t)(g.shot_w * g.shot_h * 4));
    u.backdrop = lv_image_create(s);
    lv_image_set_src(u.backdrop, &shot);
    lv_obj_set_clickable(u.backdrop, true);
    lv_obj_set_click_focusable(u.backdrop, false);
    lv_obj_add_event_cb(u.backdrop, on_touch_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(u.backdrop, on_touch_drag, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(u.backdrop, on_touch_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(u.backdrop, on_touch_release, LV_EVENT_PRESS_LOST, NULL);

    u.shade[0] = shape(s, 0, 0, W, PIC_FRAME_Y, BG, LV_OPA_60, 0);
    u.shade[1] = shape(s, 0, PIC_FRAME_Y + PIC_FRAME_H, W, H - (PIC_FRAME_Y + PIC_FRAME_H), BG, LV_OPA_60, 0);
    u.shade[2] = shape(s, 0, PIC_FRAME_Y, PIC_FRAME_X, PIC_FRAME_H, BG, LV_OPA_60, 0);
    u.shade[3] = shape(s, PIC_FRAME_X + PIC_FRAME_W, PIC_FRAME_Y,
                       W - (PIC_FRAME_X + PIC_FRAME_W), PIC_FRAME_H, BG, LV_OPA_60, 0);

    u.frame = box(s);
    lv_obj_set_pos(u.frame, PIC_FRAME_X, PIC_FRAME_Y);
    lv_obj_set_size(u.frame, PIC_FRAME_W, PIC_FRAME_H);
    lv_obj_set_style_border_color(u.frame, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(u.frame, 3, 0);
    lv_obj_set_style_radius(u.frame, RADIUS, 0);

    lv_obj_t *top = shape(s, 0, 0, W, HEADER_H, BG, LV_OPA_70, 0);
    lv_obj_t *bottom = shape(s, 0, H - FOOTER_H, W, FOOTER_H, BG, LV_OPA_70, 0);
    lv_obj_t *under[] = { u.backdrop, u.shade[0], u.shade[1], u.shade[2], u.shade[3], top, bottom, u.frame };
    for (int k = 0; k < 8; k++)   /* under the header and the footer screen() made */
        lv_obj_move_to_index(under[k], k);
    u.shot_drawn = g.shot_gen;

    char buf[120];
    snprintf(buf, sizeof buf, rsl_ui_tr("%s 的图片"), title_of(g.game));
    lv_obj_t *t = label(u.head, f_l, TEXT, buf);
    lv_obj_set_width(t, 760);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    u.shot_text = label(u.chips, f_m, TEXT, "");
    u.note = rsl_ui_tr("方向键/触屏拖动调整位置 · L/R 缩放 · X 重置 · A 保存");
    update_picture();
}

void update_picture(void)
{
    if (u.shot_drawn != g.shot_gen) {
        u.shot_drawn = g.shot_gen;
        lv_obj_invalidate(u.backdrop);
    }
    set_textf(u.shot_text, "%s   %d%%", g.pic_label, g.pic_zoom_pct);
}

bool ui_picture_drag(int *dx, int *dy)
{
    if (!drag_dx && !drag_dy)
        return false;
    *dx = drag_dx;
    *dy = drag_dy;
    drag_dx = drag_dy = 0;
    return true;
}
