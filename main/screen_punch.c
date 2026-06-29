#include "screen_punch.h"
#include "ui_common.h"
#include "app_state.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "PUNCH";

static lv_obj_t *s_lbl;
static bool      s_editing = false;   /* 本界面是否在调时 */
static int       s_edit_min = 0;      /* 调时中的分钟数 0-1439 */

#define EDIT_STEP 5                  /* 旋转一格 5 分钟 */

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
    if (s_editing) {
        /* 调时中:大字显示 HH:MM,随旋转滚动变化 */
        lv_label_set_text_fmt(s_lbl,
            "#00E0C0 打卡时间#\n"
            "\n"
            "#FFD700  %02d:%02d#",
            s_edit_min / 60, s_edit_min % 60);
        return;
    }
    const app_view_t *v = app_state_get();
    if (!v->punched) {
        lv_label_set_text_fmt(s_lbl,
            "#FFD700 非标准时间打卡#\n"
            "\n"
            "#AAAAAA 长按确认#");
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

/* 长按:进入调时,初始 = 当前时间 */
static void screen_punch_on_long(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    s_edit_min = t.tm_hour * 60 + t.tm_min;
    s_editing = true;
    ui_manager_set_editing(true);
    screen_punch_update();          /* 立即显示调时 */
    ESP_LOGI(TAG, "调时开始 %02d:%02d,旋转调节,松开确认", s_edit_min / 60, s_edit_min % 60);
}

/* 编辑中旋转:±5 分钟,循环 */
static void screen_punch_on_rotate(int dir)
{
    s_edit_min += dir * EDIT_STEP;
    if (s_edit_min < 0)        s_edit_min += 1440;
    if (s_edit_min >= 1440)    s_edit_min -= 1440;
    screen_punch_update();          /* 滚动刷新 */
}

/* 松开:用调好的时间打卡 */
static void screen_punch_on_release(void)
{
    int h = s_edit_min / 60, m = s_edit_min % 60;
    s_editing = false;
    app_state_punch_at(h, m);
    ESP_LOGI(TAG, "调时确认,打卡 %02d:%02d", h, m);
}

const screen_t screen_punch = {
    .create     = screen_punch_create,
    .update     = screen_punch_update,
    .on_press   = NULL,                 /* 短按无操作 */
    .on_long    = screen_punch_on_long,
    .on_release = screen_punch_on_release,
    .on_rotate  = screen_punch_on_rotate,
};
