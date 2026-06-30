#include "ui_manager.h"
#include "screen_main.h"
#include "screen_info.h"
#include "screen_punch.h"
#include "screen_pc.h"
/* screen_ai(AI 用量界面)已停用,见 screen_ai.c 注释 */
#include "app_state.h"
#include "bsp/esp-bsp.h"
#include "iot_knob.h"
#include "esp_log.h"

static const char *TAG = "UI";

#define SCREEN_COUNT 4

static const screen_t *s_screens[SCREEN_COUNT];
static lv_obj_t       *s_scr_objs[SCREEN_COUNT];
static lv_obj_t       *s_markers[SCREEN_COUNT];
static int             s_marker_idx[SCREEN_COUNT];
static int             s_cur = 0;
static lv_group_t     *s_group;
static bool            s_editing = false;
static knob_handle_t   s_knob = NULL;
static int             s_knob_last = 0;
static volatile int    s_action = 0;

static void focused_cb(lv_event_t *e)
{
    int idx = *(const int *)lv_event_get_user_data(e);
    if (s_editing) return;
    if (idx == s_cur) return;
    s_cur = idx;
    lv_scr_load(s_scr_objs[idx]);
    s_screens[idx]->update();
    static const char *names[] = {"主界面", "信息界面", "打卡界面", "电脑占用界面"};
    ESP_LOGI(TAG, "切换到 %s", names[idx]);
}

void ui_manager_press(void)       { s_action = 1; }
void ui_manager_long_press(void)  { s_action = 2; }
void ui_manager_release(void)     { s_action = 3; }

void ui_manager_set_editing(bool e) {
    s_editing = e;
    if (e && s_knob) s_knob_last = iot_knob_get_count_value(s_knob);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    app_state_tick();

    int act = s_action;
    s_action = 0;
    if (act == 1 && s_screens[s_cur]->on_press)        s_screens[s_cur]->on_press();
    else if (act == 2 && s_screens[s_cur]->on_long)    s_screens[s_cur]->on_long();
    else if (act == 3) {
        if (s_screens[s_cur]->on_release) s_screens[s_cur]->on_release();
        s_editing = false;   /* 退出编辑态,恢复旋转切界面 */
        lv_group_focus_obj(s_markers[s_cur]);
    }

    if (s_knob) {
        int c = iot_knob_get_count_value(s_knob);
        if (s_editing && s_screens[s_cur]->on_rotate) {
            int diff = c - s_knob_last;
            if (diff > 0)      for (int i = 0; i < diff; i++)  s_screens[s_cur]->on_rotate(1);
            else if (diff < 0) for (int i = 0; i < -diff; i++) s_screens[s_cur]->on_rotate(-1);
        }
        s_knob_last = c;
    }

    for (int i = 0; i < SCREEN_COUNT; i++) s_screens[i]->update();
}

void ui_manager_init(void)
{
    s_screens[0] = &screen_main;
    s_screens[1] = &screen_info;
    s_screens[2] = &screen_punch;
    s_screens[3] = &screen_pc;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        s_scr_objs[i]   = s_screens[i]->create();
        s_marker_idx[i] = i;
    }

    s_group = lv_group_create();
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) {
        lv_indev_set_group(indev, s_group);
        s_knob = *(knob_handle_t *)lv_indev_get_driver_data(indev);
        if (s_knob) s_knob_last = iot_knob_get_count_value(s_knob);
    }

    for (int i = 0; i < SCREEN_COUNT; i++) {
        s_markers[i] = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_markers[i]);
        lv_obj_clear_flag(s_markers[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_group_add_obj(s_group, s_markers[i]);
        lv_obj_add_event_cb(s_markers[i], focused_cb, LV_EVENT_FOCUSED, &s_marker_idx[i]);
    }

    lv_group_focus_obj(s_markers[0]);
    lv_scr_load(s_scr_objs[0]);
    app_state_tick();
    s_screens[0]->update();
    lv_timer_create(tick_cb, 100, NULL);
    ESP_LOGI(TAG, "UI 就绪: 4 界面 + 编码器,进入主界面");
}
