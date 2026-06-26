#pragma once
#include "lvgl.h"

/* 单个界面的统一接口:每个界面实现这三个函数并以 const screen_t 暴露。
 *  - create:   建 screen(lv_obj_create(NULL)) 并返回,内部把控件都挂上去
 *  - update:   10fps 刷新,内部读 app_state_get() 更新控件
 *  - on_press: 编码器按下、且本界面处于焦点时调用 */
typedef struct {
    lv_obj_t *(*create)(void);
    void      (*update)(void);
    void      (*on_press)(void);
} screen_t;

/* 在 bsp_display_lock() 内调用:创建三个界面、绑定编码器 group、启动 10fps 刷新 */
void ui_manager_init(void);
void ui_manager_press(void);   /* 编码器按下边沿时调:执行当前界面的 on_press */
