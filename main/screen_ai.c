/* === 第4界面"AI 用量"已停用 ===
 * 原本三个水平并排环显示三个 AI 的额度(剩余/总额,用越多环越少)。
 * 停用原因见 ai_usage.c 顶部注释:GLM/Kimi 无查额 API,MiniMax 在 ESP32 上
 * HTTPS 不稳,电脑端也拿不到可靠的 GLM 额度,整条 AI 用量链路移除。
 * 本界面已从 ui_manager 注销、CMakeLists 不再编译 screen_ai.c。
 * 保留此文件仅为记录停用原因。
 */
