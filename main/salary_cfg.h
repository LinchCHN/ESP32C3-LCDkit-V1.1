#pragma once
#include <stdbool.h>

/* 薪资参数(存 NVS,断电不丢;可用手机 Web 页改) */
typedef struct {
    float salary_month;     /* 月薪(元) */
    int   work_days;        /* 月工作天数 */
    float hours_per_day;    /* 每天工作时长(小时) */
    int   work_start_min;   /* 上班时间,当天分钟数(9:00=540) */
    int   work_end_min;     /* 下班时间,当天分钟数(18:00=1080) */
} salary_cfg_t;

void cfg_load(void);                  /* 启动时从 NVS 读(无则用默认) */
void cfg_set(const salary_cfg_t *c);  /* 写内存 + 保存到 NVS */
const salary_cfg_t *cfg_get(void);    /* 取当前参数(只读指针) */

/* 基于"工作时长"的赚钱速率(工作时每小时赚这些) */
float cfg_per_hour(void);
float cfg_per_min(void);
float cfg_per_sec(void);
