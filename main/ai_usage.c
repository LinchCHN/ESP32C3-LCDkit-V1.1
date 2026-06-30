#include "ai_usage.h"

/* === AI 用量统计功能已停用(设备端) ===
 * 原计划:设备 HTTPS 查 AI 额度 + 电脑推送 + 第4界面显示三环。
 * 失败原因(查证结论):
 *   1. 智谱 GLM / Kimi 没有公开的"查额度"HTTP API。官方只提供 Claude Code
 *      内的 LLM 插件 glm-plan-usage(是个 agent,不是 REST 接口);ccswitch 只
 *      切换 provider(把 baseUrl/key 写进 ~/.claude/settings.json),也不查用量。
 *   2. MiniMax 虽有 /v1/token_plan/remains,但 ESP32-C3 上 HTTPS TLS 握手
 *      栈耗约 10K、证书(PSA)校验易失败(-141 signature),不稳定。
 *   3. 电脑端也无法可靠自动获取 GLM 额度,手动填进脚本又没有统计意义。
 * 结论:AI 用量整条链(设备查询 + 电脑推送 + 第4界面)全部移除。
 * 保留空函数仅为兼容旧调用,/push 不再带 AI 字段。
 */
static ai_state_t s_state[AI_COUNT];

const ai_state_t *ai_usage_state(void) { return s_state; }
void ai_usage_set_pushed(int idx, int remaining, int total) { (void)idx; (void)remaining; (void)total; }
void ai_usage_start(void) { }
void ai_usage_refresh(void) { }
