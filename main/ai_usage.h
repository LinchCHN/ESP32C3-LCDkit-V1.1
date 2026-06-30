#pragma once
#include <stdbool.h>
#include "ai_cfg.h"

/* 单个 AI 的实时状态 */
typedef struct {
    int  remaining;   /* 剩余额度 */
    int  total;       /* 总额度(环 = remaining/total) */
    bool ok;          /* true=有数据(显环),false=实心圆 */
} ai_state_t;

const ai_state_t *ai_usage_state(void);   /* 返回 ai_state[3](只读) */
void ai_usage_start(void);                 /* 启动后台查询任务 */
void ai_usage_refresh(void);               /* 立即查一次 */
void ai_usage_set_pushed(int idx, int remaining, int total);  /* 电脑推送覆盖 */
