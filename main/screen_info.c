#include "screen_info.h"
#include "ui_common.h"
#include "app_state.h"
#include <stdio.h>
#include <string.h>

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

    /* 电脑占用(由 /push 推送);没推过/查不到显示 -- */
    char cpu_s[16], gpu_s[16], mem_s[16];
    if (v->pc_ok && v->pc_cpu >= 0) snprintf(cpu_s, sizeof(cpu_s), "%d%%", v->pc_cpu); else strcpy(cpu_s, "--");
    if (v->pc_ok && v->pc_gpu >= 0) snprintf(gpu_s, sizeof(gpu_s), "%d%%", v->pc_gpu); else strcpy(gpu_s, "--");
    if (v->pc_ok && v->pc_mem >= 0) snprintf(mem_s, sizeof(mem_s), "%d%%", v->pc_mem); else strcpy(mem_s, "--");

    lv_label_set_text_fmt(s_lbl,
        "#00E0C0 CPU %s#\n"
        "#7FB2FF GPU %s#\n"
        "#FFD700 RAM %s#\n"
        "\n"
        "#888888 月薪%.0f %d天 %.1f时#\n"
        "#888888 已赚%.1f天#\n"
        "#666666 IP %s#",
        cpu_s, gpu_s, mem_s,
        v->salary_month, v->work_days, v->hours_per_day,
        v->earned_days,
        v->ip[0] ? v->ip : "连接中");
}

static void screen_info_on_press(void) { /* 信息界面按键无操作 */ }

const screen_t screen_info = {
    .create   = screen_info_create,
    .update   = screen_info_update,
    .on_press = screen_info_on_press,
    .on_long  = NULL,
};
