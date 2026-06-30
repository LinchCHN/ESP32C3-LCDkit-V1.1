#include "app_state.h"
#include "salary_cfg.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <time.h>
#include <string.h>

static const char *TAG = "APP";

/* 番茄钟:25 分钟工作 + 5 分钟休息,循环。到点各提醒一下。 */
#define POMO_WORK_SEC   (25 * 60)
#define POMO_BREAK_SEC  (5  * 60)
#define REMIND_SEC      2          /* 提醒持续秒数(RGB 闪 + 响铃) */

/* 打卡持久化(断电/重启不丢,跨天自动失效) */
#define PUNCH_NVS       "punch"
#define PUNCH_KEY_START "start"
#define PUNCH_KEY_DATE  "date"

static app_view_t s_view;

/* —— 计时/状态 —— */
static float   s_earned = 0;          /* 今日已赚(累积) */
/* 今日已赚改用 worked_sec × per_sec 每 tick 重算,不再需要累积时刻 */
static bool    s_last_working = false;

/* —— 打卡 —— */
static time_t  s_punch_start = 0;     /* 0 = 未打卡;>0 = 上班打卡时刻 */
static int     s_punch_date  = 0;     /* 打卡那天 ymd,用于跨天失效判断 */

/* —— 番茄钟 —— */
static pomo_phase_t s_pomo = POMO_IDLE;
static time_t s_pomo_end = 0;
static bool   s_pomo_paused = false;
static int    s_pomo_pause_remain = 0;

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

void app_state_set_pc(int cpu, int gpu, int mem)
{
    s_view.pc_cpu = cpu;
    s_view.pc_gpu = gpu;
    s_view.pc_mem = mem;
    s_view.pc_ok = true;
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

/* 今天 ymd(如 20260626),用于判断打卡是否跨天 */
static int today_key(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

/* 把打卡(上班时刻 + 那天 ymd)存进 NVS */
static void save_punch(void)
{
    nvs_handle_t h;
    if (nvs_open(PUNCH_NVS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, PUNCH_KEY_START, &s_punch_start, sizeof(s_punch_start));
        nvs_set_blob(h, PUNCH_KEY_DATE,  &s_punch_date,  sizeof(s_punch_date));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* 启动时从 NVS 恢复打卡;跨天的丢弃 */
void app_state_init(void)
{
    nvs_handle_t h;
    if (nvs_open(PUNCH_NVS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_punch_start);
        nvs_get_blob(h, PUNCH_KEY_START, &s_punch_start, &len);
        len = sizeof(s_punch_date);
        nvs_get_blob(h, PUNCH_KEY_DATE,  &s_punch_date,  &len);
        nvs_close(h);
    }
    if (s_punch_start > 0 && s_punch_date != today_key()) {
        ESP_LOGI(TAG, "上次打卡在 %d,今天 %d,已跨天失效", s_punch_date, today_key());
        s_punch_start = 0;
        s_punch_date  = 0;
    } else if (s_punch_start > 0) {
        ESP_LOGI(TAG, "恢复今日(%d)的打卡", s_punch_date);
    }
}

void app_state_tick(void)
{
    const salary_cfg_t *c = cfg_get();
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    /* 跨天:昨天的打卡自动失效 */
    if (s_punch_start > 0 && s_punch_date != today_key()) {
        s_punch_start = 0;
        s_punch_date  = 0;
    }

    /* —— 工作窗口 / 已工作秒 / 是否工作 —— */
    time_t wstart = 0, wend = 0;
    int   worked_sec = 0;
    bool  working = false;

    if (s_punch_start > 0) {
        /* 已打卡:窗口 = 打卡时刻 .. 打卡时刻 + 工时 (午休不算入工时则顺延) */
        wstart = s_punch_start;
        int lunch_extra = c->lunch_counts ? 0 : c->lunch_min;
        wend   = s_punch_start + (time_t)(c->hours_per_day * 3600.0f)
                 + (time_t)(lunch_extra * 60);
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

    /* —— 今日已赚 = 从上班到现在的秒数 × 每秒薪资(每 tick 重算,从上班算) —— */
    s_earned = worked_sec * cfg_per_sec();

    /* —— 下班提醒边沿(working -> off) —— */
    if (s_last_working && !working) {
        trigger_remind(now);
        ESP_LOGI(TAG, "到下班时间,触发提醒");
    }
    s_last_working = working;

    /* —— 番茄钟推进 —— */
    if ((s_pomo == POMO_WORK || s_pomo == POMO_BREAK) && !s_pomo_paused) {
        if (now >= s_pomo_end) {
            trigger_remind(now);
            if (s_pomo == POMO_WORK) {
                s_pomo = POMO_BREAK;
                s_pomo_end = now + POMO_BREAK_SEC;
                ESP_LOGI(TAG, "番茄钟: 工作段结束 → 休息 %d 分钟", POMO_BREAK_SEC / 60);
            } else {
                s_pomo = POMO_IDLE;          /* 休息结束,等下次按 */
                ESP_LOGI(TAG, "番茄钟: 休息结束,等待下次启动");
            }
        }
    }

    /* —— 填视图 —— */
    s_view.hh = t.tm_hour; s_view.mm = t.tm_min; s_view.ss = t.tm_sec;
    s_view.worked_sec = worked_sec;
    s_view.punched = (s_punch_start > 0);

    struct tm ws2, we2;
    localtime_r(&s_punch_start, &ws2);
    localtime_r(&wend,          &we2);
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
        ESP_LOGI(TAG, "番茄钟: 启动工作段 %d 分钟", POMO_WORK_SEC / 60);
    } else if (!s_pomo_paused) {
        s_pomo_pause_remain = (int)difftime(s_pomo_end, now);
        if (s_pomo_pause_remain < 0) s_pomo_pause_remain = 0;
        s_pomo_paused = true;
        ESP_LOGI(TAG, "番茄钟: 暂停(剩余 %ds)", s_pomo_pause_remain);
    } else {
        s_pomo_end = now + s_pomo_pause_remain;
        s_pomo_paused = false;
        ESP_LOGI(TAG, "番茄钟: 恢复(剩余 %ds)", s_pomo_pause_remain);
    }
}

/* 按指定上班时间打卡(调时确认用);下班 = 上班 + 工时,存 NVS */
void app_state_punch_at(int hour, int min)
{
    const salary_cfg_t *c = cfg_get();
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    t.tm_hour = hour; t.tm_min = min; t.tm_sec = 0;
    s_punch_start = mktime(&t);
    s_punch_date  = today_key();
    /* s_earned 由 tick 按 worked_sec × per_sec 自动重算,无需重置 */
    save_punch();
    int lunch_extra = c->lunch_counts ? 0 : c->lunch_min;
    time_t pend = s_punch_start + (time_t)(c->hours_per_day * 3600.0f)
                  + (time_t)(lunch_extra * 60);
    struct tm ts, te;
    localtime_r(&s_punch_start, &ts);
    localtime_r(&pend,          &te);
    ESP_LOGI(TAG, "打卡上班 %02d:%02d → 预计下班 %02d:%02d (工时 %.1fh + 午休%d分)",
             ts.tm_hour, ts.tm_min, te.tm_hour, te.tm_min, c->hours_per_day, lunch_extra);
}

/* 用当前时间打卡 */
void app_state_punch(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    app_state_punch_at(t.tm_hour, t.tm_min);
}
