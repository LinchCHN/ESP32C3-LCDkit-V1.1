#include "ui_manager.h"
#include "screen_main.h"
#include "screen_info.h"
#include "screen_punch.h"
#include "app_state.h"
#include "bsp/esp-bsp.h"
#include "iot_knob.h"
#include "esp_log.h"

static const char *TAG = "UI";

/* 三界面 + 各自的 screen 对象 + 焦点标记(挂 layer_top,不随界面切换) */
static const screen_t *s_screens[3];
static lv_obj_t       *s_scr_objs[3];
static lv_obj_t       *s_markers[3];
static int             s_marker_idx[3];
static int             s_cur = 0;
static lv_group_t     *s_group;
static bool            s_editing = false;   /* 编辑态:旋转改调时,不切界面 */
static knob_handle_t   s_knob = NULL;       /* 旋转编码器底层 handle */
static int             s_knob_last = 0;     /* 上次 knob 计数,差分用 */
static volatile int    s_action = 0;        /* 待处理按键:1=短按 2=长按 3=松开 */

/* 未按旋转 → group 焦点移动 → 本回调 → 切界面(编辑态不切) */
static void focused_cb(lv_event_t *e)
{
    int idx = *(const int *)lv_event_get_user_data(e);
    if (s_editing) return;               /* 编辑态:不切界面 */
    if (idx == s_cur) return;
    s_cur = idx;
    lv_scr_load(s_scr_objs[idx]);
    s_screens[idx]->update();
    static const char *names[] = {"主界面", "信息界面", "打卡界面"};
    ESP_LOGI(TAG, "切换到 %s", names[idx]);
}

/* 以下三个由 main 的 encoder_led_task(非 LVGL 线程)调用:只设标志。
 * 真正的 LVGL 操作(on_*)在 tick_cb(LVGL 线程,已持锁)里执行,避免竞争/卡死。*/
void ui_manager_press(void)       { s_action = 1; }
void ui_manager_long_press(void)  { s_action = 2; }
void ui_manager_release(void)     { s_action = 3; }

void ui_manager_set_editing(bool e) {
    s_editing = e;
    if (e && s_knob) s_knob_last = iot_knob_get_count_value(s_knob);  /* 进入编辑时同步基准 */
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    app_state_tick();

    /* 1) 处理按键事件(LVGL 线程,持锁) */
    int act = s_action;
    s_action = 0;
    if (act == 1 && s_screens[s_cur]->on_press)        s_screens[s_cur]->on_press();
    else if (act == 2 && s_screens[s_cur]->on_long)    s_screens[s_cur]->on_long();
    else if (act == 3 && s_screens[s_cur]->on_release) {
        s_screens[s_cur]->on_release();
        lv_group_focus_obj(s_markers[s_cur]);   /* 拉回焦点 */
    }

    /* 2) 编辑态:读 knob 旋转差分调时(LVGL 线程,持锁) */
    if (s_knob) {
        int c = iot_knob_get_count_value(s_knob);
        if (s_editing && s_screens[s_cur]->on_rotate) {
            int diff = c - s_knob_last;
            if (diff > 0)      for (int i = 0; i < diff; i++)  s_screens[s_cur]->on_rotate(1);
            else if (diff < 0) for (int i = 0; i < -diff; i++) s_screens[s_cur]->on_rotate(-1);
        }
        s_knob_last = c;
    }

    /* 3) 刷新全部界面 */
    for (int i = 0; i < 3; i++) s_screens[i]->update();
}

void ui_manager_init(void)
{
    s_screens[0] = &screen_main;
    s_screens[1] = &screen_info;
    s_screens[2] = &screen_punch;
    for (int i = 0; i < 3; i++) {
        s_scr_objs[i]   = s_screens[i]->create();
        s_marker_idx[i] = i;
    }

    s_group = lv_group_create();
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) {
        lv_indev_set_group(indev, s_group);
        /* esp_lvgl_port 把 encoder ctx 存在 indev driver 的 user_data,
         * 其首字段就是 iot_knob handle —— 取出用于编辑态直接读旋转计数。*/
        s_knob = *(knob_handle_t *)lv_indev_get_driver_data(indev);
        if (s_knob) s_knob_last = iot_knob_get_count_value(s_knob);
    }

    for (int i = 0; i < 3; i++) {
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
    lv_timer_create(tick_cb, 100, NULL);   /* 10fps */
    ESP_LOGI(TAG, "UI 就绪: 3 界面 + 编码器,进入主界面");
}
