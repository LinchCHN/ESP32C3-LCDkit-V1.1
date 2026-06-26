#include "app_state.h"
#include "salary_cfg.h"
#include "esp_timer.h"
#include <time.h>
#include <string.h>

/* 番茄钟:25 分钟工作 + 5 分钟休息,循环。到点各提醒一下。 */
#define POMO_WORK_SEC   (25 * 60)
#define POMO_BREAK_SEC  (5  * 60)
#define REMIND_SEC      2          /* 提醒持续秒数(RGB 闪 + 响铃) */

static app_view_t s_view;

/* —— 计时/状态 —— */
static float   s_earned = 0;          /* 今日已赚(累积) */
static int64_t s_last_us = 0;         /* 上次累积时刻(微秒) */
static bool    s_last_working = false;

/* —— 打卡 —— */
static time_t  s_punch_start = 0;     /* 0 = 未打卡;>0 = 上班打卡时刻 */

/* —— 番茄钟 —— */
static pomo_phase_t s_pomo = POMO_IDLE;
static time_t s_pomo_end = 0;         /* 当前阶段结束时刻 */
static bool   s_pomo_paused = false;
static int    s_pomo_pause_remain = 0;/* 暂停时记下的剩余秒 */

/* —— 提醒 —— */
static volatile time_t s_remind_until = 0;

const app_view_t *app_state_get(void) { return &s_view; }

bool app_state_remind_active(void)
{
    return s_remind_until && time(NULL) < s_remind_until;
}

void app_state_set_ip(const char *ip)
{
    if (ip) {
        strncpy(s_view.ip, ip, sizeof(s_view.ip) - 1);
        s_view.ip[sizeof(s_view.ip) - 1] = 0;
    } else {
        s_view.ip[0] = 0;
    }
}

static void trigger_remind(time_t now)
{
    s_remind_until = now + REMIND_SEC;
}

/* 当月总天数:把"下个月 0 号"交给 mktime 规范化,即得当月最后一天 */
static int month_days(const struct tm *t)
{
    struct tm eot = *t;
    eot.tm_mon += 1;
    eot.tm_mday = 0;
    eot.tm_hour = 12;                 /* 避开夏令时边界 */
    mktime(&eot);
    return eot.tm_mday;
}

void app_state_tick(void)
{
    const salary_cfg_t *c = cfg_get();
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    /* —— 工作窗口 / 已工作秒 / 是否工作 —— */
    time_t wstart = 0, wend = 0;
    int   worked_sec = 0;
    bool  working = false;

    if (s_punch_start > 0) {
        /* 已打卡:窗口 = 打卡时刻 .. 打卡时刻 + 工时 */
        wstart = s_punch_start;
        wend   = s_punch_start + (time_t)(c->hours_per_day * 3600.0f);
        if (now < wstart)        { worked_sec = 0;                          working = false; }
        else if (now < wend)     { worked_sec = (int)difftime(now, wstart); working = true;  }
        else                     { worked_sec = (int)difftime(wend, wstart);working = false; }
    } else {
        /* 未打卡:回退到配置的固定上下班时间(当天) */
        struct tm ws = t, we = t;
        ws.tm_hour = c->work_start_min / 60; ws.tm_min = c->work_start_min % 60; ws.tm_sec = 0;
        we.tm_hour = c->work_end_min   / 60; we.tm_min = c->work_end_min   % 60; we.tm_sec = 0;
        wstart = mktime(&ws);
        wend   = mktime(&we);
        if (wend <= wstart) wend = wstart + 1;

        int now_min = t.tm_hour * 60 + t.tm_min;
        int ws_min = c->work_start_min, we_min = c->work_end_min;
        if (we_min <= ws_min) we_min = ws_min + 1;
        if      (now_min >= ws_min && now_min < we_min) { worked_sec = (int)difftime(now, wstart); working = true;  }
        else if (now_min >= we_min)                     { worked_sec = (we_min - ws_min) * 60;     working = false; }
        else                                            { worked_sec = 0;                          working = false; }
    }

    /* —— 累积今日薪资(working 时按 dt 累加) —— */
    int64_t now_us = esp_timer_get_time();
    if (s_last_us == 0) {
        s_earned = worked_sec * cfg_per_sec();
        s_last_us = now_us;
    } else {
        float dt = (now_us - s_last_us) / 1000000.0f;
        s_last_us = now_us;
        if (working && dt > 0 && dt < 10.0f) {
            s_earned += cfg_per_sec() * dt;
        }
    }

    /* —— 下班提醒边沿(working -> off) —— */
    if (s_last_working && !working) trigger_remind(now);
    s_last_working = working;

    /* —— 番茄钟推进 —— */
    if ((s_pomo == POMO_WORK || s_pomo == POMO_BREAK) && !s_pomo_paused) {
        if (now >= s_pomo_end) {
            trigger_remind(now);
            if (s_pomo == POMO_WORK) {
                s_pomo = POMO_BREAK;
                s_pomo_end = now + POMO_BREAK_SEC;
            } else {
                s_pomo = POMO_IDLE;          /* 休息结束,等下次按 */
            }
        }
    }

    /* —— 填视图 —— */
    s_view.hh = t.tm_hour; s_view.mm = t.tm_min; s_view.ss = t.tm_sec;
    s_view.worked_sec = worked_sec;
    s_view.punched = (s_punch_start > 0);

    struct tm ws2, we2;
    time_t s_for_view = (s_punch_start > 0) ? s_punch_start : wstart;
    localtime_r(&s_for_view, &ws2);
    localtime_r(&wend,        &we2);
    s_view.punch_sh = ws2.tm_hour; s_view.punch_sm = ws2.tm_min;
    s_view.punch_eh = we2.tm_hour; s_view.punch_em = we2.tm_min;

    s_view.state_text = working ? "工作中" : (worked_sec > 0 ? "已下班" : "未到上班");

    /* 番茄钟视图 */
    s_view.pomo_phase = s_pomo;
    if (s_pomo == POMO_IDLE) {
        s_view.pomo_remain_sec = 0;
        s_view.pomo_total_sec  = 0;
    } else {
        int total  = (s_pomo == POMO_WORK) ? POMO_WORK_SEC : POMO_BREAK_SEC;
        int remain = s_pomo_paused ? s_pomo_pause_remain
                                   : (int)difftime(s_pomo_end, now);
        if (remain < 0) remain = 0;
        s_view.pomo_remain_sec = remain;
        s_view.pomo_total_sec  = total;
    }

    /* 外圈:今日工作进度(满 = 下班) */
    int total_work = (int)difftime(wend, wstart);
    int ring_w = total_work > 0 ? worked_sec * 1000 / total_work : 0;
    if (ring_w > 1000) ring_w = 1000;
    if (ring_w < 0)    ring_w = 0;
    s_view.ring_work = ring_w;

    /* 中圈:番茄钟剩余整体占比(25 分钟一圈,本身很慢);
     * 运行时由 screen_main 给中圈加"呼吸"亮度起伏,提示在走。 */
    if (s_pomo == POMO_IDLE || s_view.pomo_total_sec == 0) {
        s_view.ring_pomo = 0;
    } else {
        s_view.ring_pomo = s_view.pomo_remain_sec * 1000 / s_view.pomo_total_sec;
    }

    /* 内圈:本月已过比例(满 = 月末) */
    int D = month_days(&t);
    double day_of_month = (t.tm_mday - 1)
        + (t.tm_hour * 3600.0 + t.tm_min * 60.0 + t.tm_sec) / 86400.0;
    double r = D > 0 ? day_of_month / D : 0;
    if (r > 1) r = 1;
    s_view.ring_month  = (int)(r * 1000);
    s_view.earned_days = c->work_days * (float)r;

    /* 薪资速率 / 参数快照 */
    s_view.earned_today = s_earned;
    s_view.per_hour = cfg_per_hour();
    s_view.per_min  = cfg_per_min();
    s_view.per_sec  = cfg_per_sec();
    s_view.salary_month  = c->salary_month;
    s_view.work_days     = c->work_days;
    s_view.hours_per_day = c->hours_per_day;
}

/* 主界面按键:IDLE→启动工作段;运行中→暂停/恢复 */
void app_state_pomo_toggle(void)
{
    time_t now = time(NULL);
    if (s_pomo == POMO_IDLE) {
        s_pomo = POMO_WORK;
        s_pomo_end = now + POMO_WORK_SEC;
        s_pomo_paused = false;
    } else if (!s_pomo_paused) {
        s_pomo_pause_remain = (int)difftime(s_pomo_end, now);
        if (s_pomo_pause_remain < 0) s_pomo_pause_remain = 0;
        s_pomo_paused = true;
    } else {
        s_pomo_end = now + s_pomo_pause_remain;
        s_pomo_paused = false;
    }
}

/* 打卡界面按键:记录上班时刻,下班 = 上班 + 工时 */
void app_state_punch(void)
{
    s_punch_start = time(NULL);
    s_earned  = 0;        /* 以打卡为今日计时的零点 */
    s_last_us = 0;
}
