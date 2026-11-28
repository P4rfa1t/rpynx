/* Controller settings. */
#include "ui_internal.h"

static const char *const setting_label[ROW_COUNT - RSL_ACT_COUNT] = {
    "摇杆死区", "光标速度", "光标加速度", "重复延迟",
    "重复间隔", "摇杆", "游戏中显示光标",
};


void build_remap(lv_obj_t *s)
{
    u.profile = pill(u.chips, ACCENT, "");
    u.preset = pill(u.chips, DIM, "");
    u.unsaved = pill(u.chips, AMBER, rsl_ui_tr("未保存"));
    u.note = rsl_ui_tr("任意界面按住 + 和 - 3 秒：恢复默认控制。");
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    for (int r = 0; r < ROW_COUNT; r++) {
        if (r == 0 || r == ROW_DEADZONE)
            heading(list, r ? rsl_ui_tr("摇杆与光标") : rsl_ui_tr("按键"), r ? 28 : 0);
        lv_obj_t *row = box(list);
        lv_obj_set_size(row, lv_pct(100), 54);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_style(row, &st_row, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, r < RSL_ACT_COUNT ? action_label[r] : setting_label[r - RSL_ACT_COUNT]));
        u.values[r] = r < RSL_ACT_COUNT ? flex(row, LV_FLEX_FLOW_ROW, 10) : label(row, f_m, TEXT, "");
        u.items[r] = row;
    }
    u.count = ROW_COUNT;
    memset(u.row_sig, 0xff, sizeof u.row_sig);
}

void update_remap(void)
{
    const rsl_input_config *c = &g.edit;
    uint32_t clash = rsl_config_conflicts(c);
    char v[48];
    set_text(u.profile, g.edit_label);
    set_textf(u.preset, rsl_ui_tr("预设：%s"), rsl_ui_tr(preset_label[g.edit_preset]));
    lv_obj_set_hidden(u.unsaved, !g.edit_dirty);
    for (int r = 0; r < RSL_ACT_COUNT; r++) {
        uint32_t sig = c->bindings[r] | (clash >> r & 1) << 29 | (uint32_t)(g.capture == r) << 30 |
                       (uint32_t)c->nav_stick << 31;
        if (sig != u.row_sig[r]) {
            u.row_sig[r] = sig;
            lv_obj_clean(u.values[r]);
            if (g.capture == r) {
                u.capture = label(u.values[r], f_m, ACCENT, "");
            } else {
                if (clash >> r & 1)   /* before the buttons, so the buttons line up */
                    pill(u.values[r], RED, rsl_ui_tr("冲突"));
                for (int b = 0; b < RSL_BTN_COUNT; b++)
                    if (c->bindings[r] >> b & 1)
                        button_chip(u.values[r], c, b);
            }
        }
        if (g.capture == r) {
            int32_t left = (int32_t)(g.capture_deadline - g.now);
            set_textf(u.capture, rsl_ui_tr("请按下一个按键……%d 秒"), left > 0 ? (int)(left + 999) / 1000 : 0);
        }
    }
    for (int r = ROW_DEADZONE; r < ROW_COUNT; r++) {
        switch (r) {
        case ROW_DEADZONE: snprintf(v, sizeof v, "%.2f", c->deadzone); break;
        case ROW_SPEED: snprintf(v, sizeof v, rsl_ui_tr("%.2f 屏/秒"), c->cursor_speed); break;
        case ROW_ACCEL: snprintf(v, sizeof v, "%.2f", c->cursor_accel); break;
        case ROW_DELAY: snprintf(v, sizeof v, "%u ms", (unsigned)c->repeat_delay_ms); break;
        case ROW_INTERVAL: snprintf(v, sizeof v, "%u ms", (unsigned)c->repeat_interval_ms); break;
        case ROW_STICKS:
            snprintf(v, sizeof v, rsl_ui_tr("导航摇杆：%s，光标摇杆：%s"), c->nav_stick ? rsl_ui_tr("右") : rsl_ui_tr("左"),
                     c->cursor_stick ? rsl_ui_tr("右") : rsl_ui_tr("左"));
            break;
        default: snprintf(v, sizeof v, "%s", c->cursor_visible ? rsl_ui_tr("是") : rsl_ui_tr("否")); break;
        }
        set_textf(u.values[r], "‹   %s   ›", v);
    }
}
