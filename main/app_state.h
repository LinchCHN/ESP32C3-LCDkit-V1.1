#pragma once
#include <stdbool.h>

/* 番茄钟阶段 */
typedef enum { POMO_IDLE, POMO_WORK, POMO_BREAK } pomo_phase_t;

/* 只读视图:app_state_tick() 每 100ms 更新,各界面读它刷新 UI */
typedef struct {
    int   hh, mm, ss;          /* 当前时间 */
    const char *state_text;    /* "工作中/已下班/未到上班..." */
    int   worked_sec;          /* 今日已工作秒 */
    bool  punched;             /* 是否已打卡 */
    int   punch_sh, punch_sm;  /* 上班 HH:MM */
    int   punch_eh, punch_em;  /* 下班 HH:MM */
    pomo_phase_t pomo_phase;
    int   pomo_remain_sec;     /* 番茄钟剩余 */
    int   pomo_total_sec;      /* 当前阶段总长 */
    float earned_today;        /* 今日已赚 */
    float earned_days;         /* 本月已赚几天 */
    int   ring_work;           /* 外圈 0..1000 工作进度 */
    int   ring_pomo;           /* 中圈 0..1000 番茄钟剩余 */
    int   ring_month;          /* 内圈 0..1000 本月进度 */
    float per_hour, per_min, per_sec;
    float salary_month;
    int   work_days;
    float hours_per_day;
    char  ip[16];              /* 本机 IP(手机配参用),空串=未连 */
} app_view_t;

const app_view_t *app_state_get(void);
void app_state_tick(void);             /* 10fps:更新全部字段 + 提醒边沿 */
void app_state_pomo_toggle(void);      /* 主界面按键:番茄钟 启动/暂停/恢复 */
void app_state_punch(void);            /* 打卡界面按键:确认上班 */
bool app_state_remind_active(void);    /* 提醒是否进行中(下班/番茄钟到点) */
void app_state_set_ip(const char *ip); /* 网络层连上后回填 IP */
