#include "screen_main.h"
#include "ui_common.h"
#include "app_state.h"
#include "esp_timer.h"
#include <stdio.h>
#include <math.h>

static lv_obj_t *s_arc_work, *s_arc_pomo, *s_arc_month, *s_lbl;

static lv_obj_t *screen_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_SCR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 三个嵌套圆环:外=工作进度(金),中=番茄钟(青),内=本月已赚几天(蓝) */
    s_arc_work  = ui_arc_create(scr, ARC_SIZE_OUT, ARC_W, lv_color_hex(COL_GOLD));
    s_arc_pomo  = ui_arc_create(scr, ARC_SIZE_MID, ARC_W, lv_color_hex(COL_CYAN));
    s_arc_month = ui_arc_create(scr, ARC_SIZE_IN,  ARC_W, lv_color_hex(COL_BLUE));

    s_lbl = lv_label_create(scr);
    lv_label_set_recolor(s_lbl, true);
    lv_obj_set_style_text_align(s_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl, &cn_font, 0);
    lv_obj_set_style_text_line_space(s_lbl, 4, 0);
    lv_obj_center(s_lbl);
    return scr;
}

static void screen_main_update(void)
{
    const app_view_t *v = app_state_get();
    lv_arc_set_value(s_arc_work,  v->ring_work);
    lv_arc_set_value(s_arc_pomo,  v->ring_pomo);
    lv_arc_set_value(s_arc_month, v->ring_month);

    /* 番茄钟运行时,中圈缓慢"呼吸"(亮度起伏 0.4~1.0,周期 3 秒),提示在走;
     * 25 分钟整体进度本身位移很慢,呼吸让圈看着是活的。IDLE 时静态青色。 */
    if (v->pomo_phase != POMO_IDLE) {
        int ms = (int)((esp_timer_get_time() / 1000) % 3000);
        float f = 0.70f + 0.30f * sinf(2.0f * 3.14159265f * ms / 3000.0f);
        lv_color_t c = lv_color_make(
            (uint8_t)(0x00 * f), (uint8_t)(0xE0 * f), (uint8_t)(0xC0 * f));
        lv_obj_set_style_arc_color(s_arc_pomo, c, LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_arc_color(s_arc_pomo, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    }

    /* 第二行:番茄钟运行时显示倒计时,否则显示工作状态 */
    char line2[40];
    int  pr = v->pomo_remain_sec;
    if (v->pomo_phase == POMO_WORK) {
        snprintf(line2, sizeof(line2), "#00E0C0 番茄 %02d:%02d#", pr / 60, pr % 60);
    } else if (v->pomo_phase == POMO_BREAK) {
        snprintf(line2, sizeof(line2), "#7FB2FF 休息 %02d:%02d#", pr / 60, pr % 60);
    } else {
        bool remind = app_state_remind_active();
        snprintf(line2, sizeof(line2), "%s%s#",
                 remind ? "#FF6600 " : "#00E0C0 ", v->state_text);
    }

    int wh = v->worked_sec / 3600, wm = (v->worked_sec % 3600) / 60;
    lv_label_set_text_fmt(s_lbl,
        "#FFFFFF %02d:%02d:%02d#\n"
        "%s\n"
        "#AAAAAA 已工作 %d时%d分#\n"
        "#FFD700 今日 ¥%.2f#",
        v->hh, v->mm, v->ss, line2, wh, wm, v->earned_today);
}

static void screen_main_on_press(void)
{
    app_state_pomo_toggle();   /* 主界面按下 = 番茄钟 启动/暂停/恢复 */
}

const screen_t screen_main = {
    .create   = screen_main_create,
    .update   = screen_main_update,
    .on_press = screen_main_on_press,
};
