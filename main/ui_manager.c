#include "ui_manager.h"
#include "screen_main.h"
#include "screen_info.h"
#include "screen_punch.h"
#include "app_state.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "UI";

/* 三界面 + 各自的 screen 对象 + 焦点标记(挂 layer_top,不随界面切换) */
static const screen_t *s_screens[3];
static lv_obj_t       *s_scr_objs[3];
static lv_obj_t       *s_markers[3];
static int             s_marker_idx[3];
static int             s_cur = 0;
static lv_group_t     *s_group;

/* 旋钮旋转 → group 焦点在 3 个 marker 间循环 → 本回调切界面 */
static void focused_cb(lv_event_t *e)
{
    int idx = *(const int *)lv_event_get_user_data(e);
    if (idx == s_cur) return;
    s_cur = idx;
    lv_scr_load(s_scr_objs[idx]);
    s_screens[idx]->update();        /* 切换后立即刷新,避免闪旧值 */
    static const char *names[] = {"主界面", "信息界面", "打卡界面"};
    ESP_LOGI(TAG, "切换到 %s", names[idx]);
}

/* 编码器按下(GPIO9 边沿,见 main.c 的 encoder_led_task):交给当前焦点界面。
 * 不走 LVGL group 的按键事件——esp_lvgl_port 的 encoder 按下不产生
 * LV_KEY_ENTER 到 marker,故用 GPIO 轮询边沿(原 s_manual 三态的可靠机制)。*/
void ui_manager_press(void)
{
    s_screens[s_cur]->on_press();
}

/* 10fps:推进状态核心 + 刷新全部界面(切回时不闪旧值) */
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    app_state_tick();
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

    /* 编码器旋转绑 group:旋转在 3 个透明 marker 间循环焦点 → 切界面 */
    s_group = lv_group_create();
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_set_group(indev, s_group);

    for (int i = 0; i < 3; i++) {
        s_markers[i] = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_markers[i]);
        lv_obj_clear_flag(s_markers[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_group_add_obj(s_group, s_markers[i]);
        lv_obj_add_event_cb(s_markers[i], focused_cb, LV_EVENT_FOCUSED, &s_marker_idx[i]);
    }

    lv_group_focus_obj(s_markers[0]);   /* 默认焦点 = 主界面 */
    lv_scr_load(s_scr_objs[0]);
    app_state_tick();
    s_screens[0]->update();
    lv_timer_create(tick_cb, 100, NULL);   /* 10fps */
    ESP_LOGI(TAG, "UI 就绪: 3 界面 + 编码器,进入主界面");
}
