/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * 薪资可视化固件 (ESP32-C3-LCDkit)
 * ====================================================================
 * 三个嵌套圆环:
 *   外环(金) = 今日工作进度(上班→下班,越接近下班越圆满)
 *   中环(青) = 番茄钟倒计时(25 分钟工作 / 5 分钟休息,主界面按下启动)
 *   内环(蓝) = 本月已赚几天(按月薪 + 今天日期推算)
 * 主界面中央:时间 / 状态 / 已工作时长 / 今日已赚。
 * 旋转编码器:旋转在 主/信息/打卡 三界面间切换;按下按界面执行
 *   (主界面=番茄钟启停,打卡界面=确认上班,信息界面=无)。
 * 连 WiFi 后 SNTP 对时;手机访问 http://设备IP/ 可改薪资参数。
 *
 * 本文件只剩硬件初始化 + 启动;界面逻辑见 screen_*.c,状态见 app_state.c。
 *
 * 编译烧录: idf.py build flash monitor
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "led_indicator.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include <time.h>
#include <string.h>
#include "wifi_config.h"
#include "salary_cfg.h"
#include "web_server.h"
#include "app_state.h"
#include "ui_manager.h"

static const char *TAG = "C3_LCDKIT";

static esp_codec_dev_handle_t s_speaker = NULL;
static led_indicator_handle_t  s_leds[BSP_LED_NUM];


/* ====================================================================
 * 显示 + LVGL
 * ==================================================================== */
static lv_display_t *init_display(void)
{
    lv_display_t *disp = bsp_display_start();
    assert(disp);
    bsp_display_backlight_on();
    return disp;
}


/* ====================================================================
 * 音频(板载 PDM 喇叭):生成正弦提示音
 * ==================================================================== */
static void init_audio(void)
{
    s_speaker = bsp_audio_codec_speaker_init();
    assert(s_speaker);
}

static void audio_play_tone(uint32_t freq_hz, int duration_ms, int volume)
{
    const int    sample_rate = 22050;
    const size_t samples     = (size_t)sample_rate * duration_ms / 1000;
    const size_t buf_size    = samples * sizeof(int16_t);
    int16_t     *buf         = malloc(buf_size);
    if (!buf) {
        ESP_LOGE(TAG, "音频缓冲区分配失败");
        return;
    }
    for (size_t i = 0; i < samples; i++) {
        buf[i] = (int16_t)(32767.0f * sinf(2.0f * (float)M_PI * freq_hz * i / sample_rate));
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = sample_rate,
        .channel         = 1,
        .bits_per_sample = 16,
    };
    esp_codec_dev_set_out_vol(s_speaker, volume);
    esp_codec_dev_open(s_speaker, &fs);
    esp_codec_dev_write(s_speaker, (uint8_t *)buf, buf_size);
    esp_codec_dev_close(s_speaker);
    free(buf);
}


/* ====================================================================
 * RGB LED:开机不亮;编码器按下微亮;下班/番茄钟到点时彩虹变色 + 响铃。
 * 按键动作(番茄钟/打卡)由 LVGL 在 ui_manager 里处理,这里只管 RGB。
 * ==================================================================== */
static void init_led(void)
{
    ESP_ERROR_CHECK(bsp_led_indicator_create(s_leds, NULL, BSP_LED_NUM));
    led_indicator_set_rgb(s_leds[0], SET_IRGB(0, 0x00, 0x00, 0x00));   /* 开机不亮 */
}

#define ENC_LED_DIM_LEVEL 0xFF   /* 按下微亮的亮度(0x00~0xFF) */

static void hsv_to_rgb(int h, int s, int v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int rem = (h % 60) * 255 / 60;
    int p = v * (255 - s) / 255;
    int q = v * (255 - s * rem / 255) / 255;
    int t = v * (255 - s * (255 - rem) / 255) / 255;
    switch (h / 60) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static void encoder_led_task(void *arg)
{
    static int  hue = 0;
    static bool last_pressed = false;
    static bool last_remind = false;
    bool        lit = false;
    while (1) {
        bool pressed = (gpio_get_level(BSP_ENCODER_PRESS) == 0);
        bool remind  = app_state_remind_active();

        if (pressed && !last_pressed) {
            ui_manager_press();              /* 按下边沿:交给当前界面(番茄钟/打卡) */
        }
        last_pressed = pressed;

        if (remind && !last_remind) {
            audio_play_tone(1000, 150, 60);     /* 提醒开始响一声 */
        }
        last_remind = remind;

        if (remind) {
            hue = (hue + 8) % 360;
            uint8_t r, g, b;
            hsv_to_rgb(hue, 255, 60, &r, &g, &b);
            led_indicator_set_rgb(s_leds[0], SET_IRGB(0, r, g, b));
            lit = true;
        } else if (pressed != lit) {
            lit = pressed;
            led_indicator_set_rgb(s_leds[0], pressed
                ? SET_IRGB(0, ENC_LED_DIM_LEVEL, ENC_LED_DIM_LEVEL, ENC_LED_DIM_LEVEL)
                : SET_IRGB(0, 0x00, 0x00, 0x00));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


/* ====================================================================
 * WiFi 连接 + SNTP 对时
 * ==================================================================== */
static int  s_wifi_idx = 0;
static bool s_web_started = false;

static void wifi_connect_index(int idx)
{
    s_wifi_idx = idx;
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     s_wifi_list[idx].ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, s_wifi_list[idx].pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
    ESP_LOGI(TAG, "尝试连接 WiFi[%d]: %s", idx, s_wifi_list[idx].ssid);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connect_index((s_wifi_idx + 1) % WIFI_LIST_LEN);   /* 换下一个 */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
        app_state_set_ip(ip);
        ESP_LOGI(TAG, "WiFi 已连上: %s  IP=%s  SNTP 对时中...", s_wifi_list[s_wifi_idx].ssid, ip);
        if (!s_web_started) { web_server_start(); s_web_started = true; }
    }
}

static void sntp_sync_cb(struct timeval *tv)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "SNTP 对时成功: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

static void network_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    wifi_connect_index(0);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    setenv("TZ", "CST-8", 1);
    tzset();
}


/* ====================================================================
 * 入口
 * ==================================================================== */
void app_main(void)
{
    ESP_LOGI(TAG, "薪资可视化固件启动");

    lv_display_t *disp = init_display();
    ESP_LOGI(TAG, "显示初始化 OK");

    network_start();          /* WiFi + SNTP(连上后圆环按真实时间走) */
    cfg_load();               /* 薪资参数(NVS,可用手机 Web 页改) */

    init_led();
    xTaskCreate(encoder_led_task, "enc_led", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "RGB 任务已启动(按下微亮 / 提醒彩虹)");

    init_audio();
    audio_play_tone(1000, 300, 50);      /* 开机提示音 */

    /* 界面(必须在 bsp_display_lock 内调用 LVGL) */
    bsp_display_lock(0);
    ui_manager_init();
    bsp_display_unlock();

    ESP_LOGI(TAG, "初始化完成");
}
