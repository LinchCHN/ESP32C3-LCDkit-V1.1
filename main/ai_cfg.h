#pragma once

#define AI_COUNT 3

/* 单个 AI 的配置(存 NVS) */
typedef struct {
    char name[16];      /* 显示名,空=未配(实心圆) */
    char url[96];       /* 额度查询端点,空=不自动查 */
    char key[64];       /* Bearer API key */
    int  total;         /* 5h 总额度(用户填) */
    int  manual_used;   /* 手动填的"已用"(无 url 时用),-1=没填 */
} ai_cfg_t;

void ai_cfg_load(void);                       /* 启动时从 NVS 读 */
void ai_cfg_set(const ai_cfg_t ai[AI_COUNT]); /* 保存到 NVS */
const ai_cfg_t *ai_cfg_get(void);             /* 取 ai[3](只读) */
