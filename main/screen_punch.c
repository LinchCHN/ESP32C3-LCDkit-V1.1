#include "screen_punch.h"
#include "ui_common.h"
#include "app_state.h"
#include <stdio.h>

static lv_obj_t *s_lbl;

static lv_obj_t *screen_punch_create(void)
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

static void screen_punch_update(void)
{
    const app_view_t *v = app_state_get();
    if (!v->punched) {
        lv_label_set_text_fmt(s_lbl,
            "#FFD700 打卡上班#\n"
            "\n"
            "#AAAAAA 按下确认#");
    } else {
        lv_label_set_text_fmt(s_lbl,
            "#00E0C0 已打卡#\n"
            "\n"
            "#AAAAAA 上班 %02d:%02d#\n"
            "#AAAAAA 下班 %02d:%02d#\n"
            "#666666 工时 %.1f时#",
            v->punch_sh, v->punch_sm, v->punch_eh, v->punch_em,
            v->hours_per_day);
    }
}

static void screen_punch_on_press(void)
{
    app_state_punch();   /* 打卡界面按下 = 确认上班(下班 = 上班 + 工时) */
}

const screen_t screen_punch = {
    .create   = screen_punch_create,
    .update   = screen_punch_update,
    .on_press = screen_punch_on_press,
};
