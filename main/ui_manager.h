#pragma once
#include "lvgl.h"
#include <stdbool.h>

/* 单个界面的统一接口:每个界面实现这些函数(可空)并以 const screen_t 暴露。*/
typedef struct {
    lv_obj_t *(*create)(void);
    void      (*update)(void);
    void      (*on_press)(void);   /* 短按 */
    void      (*on_long)(void);    /* 长按进入(如打卡调时) */
    void      (*on_release)(void); /* 长按后松开(如确认打卡) */
    void      (*on_rotate)(int);   /* 编辑中旋转 ±1 */
} screen_t;

/* 在 bsp_display_lock() 内调用:创建三个界面、绑定编码器 group、启动 10fps 刷新 */
void ui_manager_init(void);
void ui_manager_press(void);         /* 短按:当前界面 on_press */
void ui_manager_long_press(void);    /* 长按:当前界面 on_long */
void ui_manager_release(void);       /* 松开:当前界面 on_release */
void ui_manager_set_editing(bool e); /* 进入/退出编辑态(旋转改调时,不切界面) */
