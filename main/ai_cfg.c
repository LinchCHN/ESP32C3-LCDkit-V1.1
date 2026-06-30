#include "ai_cfg.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "AICFG";
static ai_cfg_t s_ai[AI_COUNT];

static void set_default(void)
{
    for (int i = 0; i < AI_COUNT; i++) {
        s_ai[i].name[0] = 0;
        s_ai[i].url[0]  = 0;
        s_ai[i].key[0]  = 0;
        s_ai[i].total       = 0;
        s_ai[i].manual_used = -1;
    }
}

void ai_cfg_load(void)
{
    set_default();
    bool from_nvs = false;
    nvs_handle_t h;
    if (nvs_open("ai", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_ai);
        if (nvs_get_blob(h, "cfg", s_ai, &len) == ESP_OK) from_nvs = true;
        nvs_close(h);
    }
    int n = 0;
    for (int i = 0; i < AI_COUNT; i++) if (s_ai[i].name[0]) n++;
    ESP_LOGI(TAG, "加载 AI 配置(%s),已配 %d 个", from_nvs ? "NVS" : "默认", n);
}

void ai_cfg_set(const ai_cfg_t ai[AI_COUNT])
{
    memcpy(s_ai, ai, sizeof(s_ai));
    nvs_handle_t h;
    if (nvs_open("ai", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", s_ai, sizeof(s_ai));
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "AI 配置已保存到 NVS");
}

const ai_cfg_t *ai_cfg_get(void) { return s_ai; }
