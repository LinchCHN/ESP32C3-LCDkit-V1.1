/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * ESP32-C3-LCDkit 开发模板
 * ====================================================================
 * 本文件把 BSP(bsp/esp32_c3_lcdkit)封装的各硬件功能整理成可直接调用的模板,
 * 方便在这块开发板上做二次开发。app_main() 已依次演示:
 *
 *   1. 显示 + LVGL(含背光/亮度)
 *   2. SPIFFS 文件系统(默认未启用,需先配分区表)
 *   3. RGB LED(板载 1 颗 WS2812)
 *   4. 音频(板载 PDM 喇叭,播放一段提示音)
 *   5. 旋钮(旋转编码器)—— bsp_display_start() 已自动接为 LVGL 输入设备
 *
 * 编译烧录:
 *   idf.py set-target esp32c3
 *   idf.py build flash monitor
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "C3_LCDKIT";

/* 板载外设句柄(供各处使用) */
static esp_codec_dev_handle_t s_speaker = NULL;
static led_indicator_handle_t  s_leds[BSP_LED_NUM];


/* ====================================================================
 * 1. 显示 + LVGL
 * ====================================================================
 * bsp_display_start() 一键完成:SPI 总线、GC9A01 屏驱、LVGL 任务,
 * 并把旋转编码器(A/B + 按下)注册为 LVGL encoder 输入设备。
 * 之后所有 lv_* 调用都必须夹在 bsp_display_lock()/bsp_display_unlock() 之间。
 */
static lv_display_t *init_display(void)
{
    lv_display_t *disp = bsp_display_start();
    assert(disp);

    bsp_display_backlight_on();           // 开背光(默认全亮)
    // bsp_display_brightness_set(50);    // 需要调亮度时用,范围 0~100

    // 旋转屏幕(如需要):
    // bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);

    return disp;
}


/* ====================================================================
 * 2. SPIFFS 文件系统(默认未启用)
 * ====================================================================
 * ⚠️ 重要:bsp_spiffs_mount() 在【找不到 spiffs 分区时会直接 abort 并重启】,
 *    它不会返回错误码给我判断 —— 因为内部用的是 BSP_ERROR_CHECK(=ESP_ERROR_CHECK)。
 *    而默认分区表(只有 nvs/phy_init/factory)没有 spiffs 分区,
 *    所以 app_main() 默认【不调用】本函数。
 *
 * 启用步骤(见 README,本目录已提供 partitions.csv):
 *   1) 打开 sdkconfig.defaults 末尾那三行自定义分区表配置;
 *   2) 在 app_main() 里取消下面 init_spiffs() 的注释;
 *   3) idf.py fullclean && idf.py erase-flash flash
 * 挂载成功后,可用标准 fopen/fread/fwrite 读写,路径前缀为 BSP_SPIFFS_MOUNT_POINT。
 */
static void init_spiffs(void)
{
    /* 走到这里说明 spiffs 分区已存在;否则下面这句会 abort */
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_LOGI(TAG, "SPIFFS 已挂载: %s", BSP_SPIFFS_MOUNT_POINT);

    /* 示例:写文件
     * FILE *f = fopen(BSP_SPIFFS_MOUNT_POINT"/hello.txt", "w");
     * fprintf(f, "hello\n");
     * fclose(f);
     */
}


/* ====================================================================
 * 3. RGB LED(板载 1 颗 WS2812,GPIO8)
 * ====================================================================
 */
static void init_led(void)
{
    ESP_ERROR_CHECK(bsp_led_indicator_create(s_leds, NULL, BSP_LED_NUM));

    /* 开机不点亮 RGB(只创建句柄);编码器按下时由 encoder_led_task 微微点亮 */
    led_indicator_set_rgb(s_leds[0], SET_IRGB(0, 0x00, 0x00, 0x00));

    /* 也可使用内置效果(枚举见 BSP 头文件):
     *   BSP_LED_ON / BSP_LED_OFF
     *   BSP_LED_BLINK_FAST / BSP_LED_BLINK_SLOW
     *   BSP_LED_BREATHE_FAST / BSP_LED_BREATHE_SLOW
     * led_indicator_start(s_leds[0], BSP_LED_BREATHE_SLOW);
     */
}

/* "微微亮"的亮度(0x00~0xFF),值越小越暗,按需调整 */
#define ENC_LED_DIM_LEVEL   0xFF

/* 轮询编码器按下电平:按下时 RGB 微微亮,松开熄灭。
 * 说明:bsp_display_start() 已把编码器按键(GPIO9)注册为 LVGL encoder
 *       输入设备并占用了该 GPIO 的中断,所以这里用周期轮询 gpio_get_level()
 *       来联动灯,避免再次注册中断造成冲突;也因此与任何 LVGL 功能都解耦。 */
static void encoder_led_task(void *arg)
{
    bool lit = false;
    while (1) {
        /* 编码器按下时为低电平(BSP 配置 active_level=0) */
        bool pressed = (gpio_get_level(BSP_ENCODER_PRESS) == 0);
        if (pressed != lit) {
            lit = pressed;
            led_indicator_set_rgb(s_leds[0], pressed
                ? SET_IRGB(0, ENC_LED_DIM_LEVEL, ENC_LED_DIM_LEVEL, ENC_LED_DIM_LEVEL)
                : SET_IRGB(0, 0x00, 0x00, 0x00));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


/* ====================================================================
 * 4. 音频(板载 PDM 喇叭,GPIO3)
 * ====================================================================
 * 无外置 codec,默认 22050Hz / 16bit / 单声道。
 * esp_codec_dev_write() 只接收原始 PCM;要播放 WAV/MP3 需自行接解码器。
 */
static void init_audio(void)
{
    /* 内部会自动调用 bsp_audio_init(NULL) 完成 I2S/PDM 初始化 */
    s_speaker = bsp_audio_codec_speaker_init();
    assert(s_speaker);
}

/* 演示:生成并播放一段正弦提示音(不依赖任何外部文件,可直接验证音频链路) */
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
    esp_codec_dev_set_out_vol(s_speaker, volume);   // 0~100
    esp_codec_dev_open(s_speaker, &fs);
    esp_codec_dev_write(s_speaker, (uint8_t *)buf, buf_size);
    esp_codec_dev_close(s_speaker);
    free(buf);
}


/* ====================================================================
 * 5. 旋钮(旋转编码器:A=GPIO10, B=GPIO6, 按下=GPIO9)
 * ====================================================================
 * 说明:bsp_display_start() 已自动把编码器注册为 LVGL encoder 输入设备,
 *       LVGL 控件获得焦点后:旋转 = 切换焦点 / 调整数值,按下 = 确认。
 *       获取输入设备句柄:bsp_display_get_input_dev();
 *
 * 若要在 LVGL 之外独立读取旋钮(计数 / 方向事件),使用 iot_knob 组件:
 *   #include "iot_knob.h"
 *   static knob_config_t kcfg = {
 *       .default_direction = 0,
 *       .gpio_encoder_a    = BSP_ENCODER_A,
 *       .gpio_encoder_b    = BSP_ENCODER_B,
 *   };
 *   knob_handle_t knob = iot_knob_create(&kcfg);
 *   iot_knob_add_cb(knob, KNOB_LEFT,  my_left_cb,  NULL);
 *   iot_knob_add_cb(knob, KNOB_RIGHT, my_right_cb, NULL);
 */


/* ====================================================================
 * 5.5 WiFi 连接 + SNTP 对时
 * ====================================================================
 * WiFi 凭证在 wifi_config.h(已加入 .gitignore,不会提交 git)。
 * 设备依次尝试里面列出的 WiFi,连上一个就用哪个,断了自动换下一个。
 * 连上后 SNTP 同步到北京时间,圆环按真实时/分/秒走。
 * 第 2 步会加 Web 配置页,届时可手机改 WiFi 和薪资参数(存 NVS)。
 */
static int s_wifi_idx = 0;
static char s_ip_str[16] = "";        /* 连上 WiFi 后的 IP,屏幕显示供手机访问 */
static bool s_web_started = false;

static void wifi_connect_index(int idx)
{
    s_wifi_idx = idx;
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, s_wifi_list[idx].ssid, sizeof(wc.sta.ssid));
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
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "WiFi 已连上: %s  IP=%s  SNTP 对时中...", s_wifi_list[s_wifi_idx].ssid, s_ip_str);
        if (!s_web_started) { web_server_start(); s_web_started = true; }
    }
}

static void network_start(void)
{
    /* NVS:WiFi 驱动需要,后续也存薪资参数 */
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
    wifi_connect_index(0);                   /* 从第一个开始尝试 */

    /* SNTP:同步后 time() 返回真实 epoch;时区设为中国(CST-8) */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "CST-8", 1);
    tzset();
}


/* ====================================================================
 * 6. LCD 薪资可视化:三个嵌套圆环 + 中央赚钱速率
 * ====================================================================
 * 日薪 SALARY_DAY 按 24 小时均摊,得到时/分/秒的赚钱速率。
 * 三个同心圆环像三层时钟:
 *   外环(金) = 当前小时内的进度(0~60分)
 *   中环(青) = 当前分钟内的进度(0~60秒)
 *   内环(蓝) = 当前秒钟内的进度(0~1000ms,毫秒级平滑转)
 * 中央显示每小时/每分/每秒加的金额,以及今日累计。
 * 改 SALARY_DAY 即可调整薪资;时间从开机起用 esp_timer 计时。
 */
#define SALARY_DAY       400.0f                 /* 日薪(元/天)。400/24≈16.67元/时 */
#define SALARY_PER_SEC   (SALARY_DAY / 86400.0f)
#define SALARY_PER_MIN   (SALARY_PER_SEC * 60.0f)
#define SALARY_PER_HOUR  (SALARY_PER_SEC * 3600.0f)

static lv_obj_t *s_arc_hour, *s_arc_min, *s_arc_sec, *s_lbl_salary;

/* 创建一个圆环(arc)。size=直径,width=线宽,color=进度弧颜色 */
static lv_obj_t *salary_arc_create(lv_obj_t *parent, lv_coord_t size,
                                   lv_coord_t width, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);              /* 起点转到正上方(12点) */
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);             /* 高分辨率,毫秒级平滑 */
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);       /* 去掉旋钮圆点 */
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x202838), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

/* 20fps 刷新:更新三环进度 + 中央金额 */
static void salary_timer_cb(lv_timer_t *timer)
{
    /* 真实时间(SNTP 同步后)。同步前 time()≈0,圆环停在 0,连上网后自动走对 */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int ms = (int)((esp_timer_get_time() / 1000) % 1000);  /* 秒内毫秒,平滑过渡 */

    /* 三环:秒环(毫秒)/ 分环(秒)/ 时环(分),按真实时间走 */
    lv_arc_set_value(s_arc_sec,   ms);
    lv_arc_set_value(s_arc_min,   t.tm_sec * 1000 / 60);
    lv_arc_set_value(s_arc_hour,  t.tm_min * 1000 / 60);

    /* 中央:真实时间 + 速率(基于工作时长,第 3 步按工作时段累计) + IP */
    lv_label_set_text_fmt(s_lbl_salary,
        "#FFFFFF %02d:%02d:%02d#\n"
        "#FFD700 ¥%.2f/h#\n"
        "#00E0C0 ¥%.2f/m#\n"
        "#7FB2FF ¥%.4f/s#\n"
        "#556677 %s#",
        t.tm_hour, t.tm_min, t.tm_sec,
        cfg_per_hour(), cfg_per_min(), cfg_per_sec(),
        s_ip_str[0] ? s_ip_str : "WiFi 连接中");
}

static void create_salary_ui(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 外环=时(金),中环=分(青),内环=秒(蓝) —— 同心嵌套 */
    s_arc_hour = salary_arc_create(scr, 220, 9, lv_color_hex(0xFFD700));
    s_arc_min  = salary_arc_create(scr, 170, 9, lv_color_hex(0x00E0C0));
    s_arc_sec  = salary_arc_create(scr, 120, 9, lv_color_hex(0x7FB2FF));

    /* 中央金额文字 */
    s_lbl_salary = lv_label_create(scr);
    lv_label_set_recolor(s_lbl_salary, true);
    lv_obj_set_style_text_align(s_lbl_salary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_salary, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_line_space(s_lbl_salary, 3, 0);
    lv_obj_center(s_lbl_salary);

    salary_timer_cb(NULL);                      /* 先填一次,避免首帧空白 */
    lv_timer_create(salary_timer_cb, 50, NULL); /* 20fps 刷新 */
}


/* ====================================================================
 * 入口
 * ==================================================================== */
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C3-LCDkit 开发模板启动");

    /* 1) 显示 */
    lv_display_t *disp = init_display();

    /* 2) 联网 + SNTP 对时(WiFi 连上后自动校准北京时间,圆环按真实时间走) */
    network_start();

    /* 载入薪资参数(NVS,断电不丢;连上 WiFi 后可用手机 Web 页改) */
    cfg_load();

    /* 文件系统(默认未启用:无 spiffs 分区时调用会 abort,见 init_spiffs() 说明) */
    // init_spiffs();

    /* 3) LED(开机不亮,仅当编码器按下时微微亮) */
    init_led();
    xTaskCreate(encoder_led_task, "enc_led", 2048, NULL, 5, NULL);

    /* 4) 音频:播一声开机提示音 */
    init_audio();
    audio_play_tone(1000, 300, 50);

    /* 5) 薪资可视化:三个嵌套圆环(时/分/秒进度)+ 中央赚钱速率 */
    bsp_display_lock(0);
    create_salary_ui();
    bsp_display_unlock();

    ESP_LOGI(TAG, "初始化完成,开始你的开发");
}
