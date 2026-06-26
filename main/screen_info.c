#include "screen_info.h"
#include "ui_common.h"
#include "app_state.h"
#include <stdio.h>

static lv_obj_t *s_lbl;

static lv_obj_t *screen_info_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_SCR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl = lv_label_create(scr);
    lv_label_set_recolor(s_lbl, true);
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl, &cn_font, 0);
    lv_obj_set_style_text_line_space(s_lbl, 4, 0);
    lv_obj_center(s_lbl);
    return scr;
}

static void screen_info_update(void)
{
    const app_view_t *v = app_state_get();
    lv_label_set_text_fmt(s_lbl,
        "#FFD700 ¥%.2f/时#\n"
        "#00E0C0 ¥%.3f/分#\n"
        "#7FB2FF ¥%.5f/秒#\n"
        "\n"
        "#888888 月薪%.0f %d天 %.1f时#\n"
        "#888888 已赚%.1f天#\n"
        "#666666 配参:%s#",
        v->per_hour, v->per_min, v->per_sec,
        v->salary_month, v->work_days, v->hours_per_day,
        v->earned_days,
        v->ip[0] ? v->ip : "连接中");
}

static void screen_info_on_press(void) { /* 信息界面按键无操作 */ }

const screen_t screen_info = {
    .create   = screen_info_create,
    .update   = screen_info_update,
    .on_press = screen_info_on_press,
};
