#include "salary_cfg.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CFG";
static salary_cfg_t s_cfg;

static const salary_cfg_t CFG_DEFAULT = {
    .salary_month   = 8000.0f,    /* 默认月薪 8000 */
    .work_days      = 22,         /* 月工作 22 天 */
    .hours_per_day  = 8.0f,       /* 每天 8 小时 */
    .work_start_min = 9 * 60,     /* 9:00 上班 */
    .work_end_min   = 18 * 60,    /* 18:00 下班 */
};

void cfg_load(void)
{
    s_cfg = CFG_DEFAULT;
    bool from_nvs = false;
    nvs_handle_t h;
    if (nvs_open("salary", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_cfg);
        if (nvs_get_blob(h, "cfg", &s_cfg, &len) == ESP_OK) {
            from_nvs = true;
        } else {
            s_cfg = CFG_DEFAULT;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "加载参数(%s): 月薪%.0f %d天 %.1fh 上班%02d:%02d 下班%02d:%02d",
             from_nvs ? "NVS" : "默认",
             s_cfg.salary_month, s_cfg.work_days, s_cfg.hours_per_day,
             s_cfg.work_start_min / 60, s_cfg.work_start_min % 60,
             s_cfg.work_end_min / 60,   s_cfg.work_end_min % 60);
}

void cfg_set(const salary_cfg_t *c)
{
    memcpy(&s_cfg, c, sizeof(s_cfg));
    nvs_handle_t h;
    if (nvs_open("salary", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "参数已保存: 月薪%.0f %d天 %.1fh 上班%02d:%02d 下班%02d:%02d",
             s_cfg.salary_month, s_cfg.work_days, s_cfg.hours_per_day,
             s_cfg.work_start_min / 60, s_cfg.work_start_min % 60,
             s_cfg.work_end_min / 60,   s_cfg.work_end_min % 60);
}

const salary_cfg_t *cfg_get(void) { return &s_cfg; }

float cfg_per_hour(void)
{
    if (s_cfg.work_days <= 0 || s_cfg.hours_per_day <= 0) {
        return 0;
    }
    float day_salary = s_cfg.salary_month / s_cfg.work_days;   /* 日薪 */
    return day_salary / s_cfg.hours_per_day;                    /* 时薪 */
}

float cfg_per_min(void) { return cfg_per_hour() / 60.0f; }
float cfg_per_sec(void) { return cfg_per_hour() / 3600.0f; }
