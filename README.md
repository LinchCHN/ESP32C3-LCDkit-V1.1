# ESP32-C3-LCDkit 开发模板

基于 esp-bsp 的 `esp32_c3_lcdkit` BSP,把板载功能整理成可直接开发的模板工程。
打开 `main/main.c`,每个功能都已写成带中文注释的函数,`app_main()` 依次演示。

## 目录结构

```
c3_lcdkit_dev/
├── CMakeLists.txt            # 顶层工程文件
├── sdkconfig.defaults        # 默认配置(选 esp32c3 + 本板 BSP + LVGL)
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml     # 引用 bsp_selector
    └── main.c                # 各功能模板(显示/音频/SPIFFS/LED/旋钮)
```

## 构建与烧录

```bash
# 1. 进入 ESP-IDF 环境(按你的安装路径)
. $IDF_PATH/export.sh

# 2. 设置目标芯片(首次)
idf.py set-target esp32c3

# 3. 编译 + 烧录 + 串口监视
idf.py build flash monitor
```

> 串口监视退出键:`Ctrl + ]`

## 引脚速查(来自 BSP)

| 外设 | 引脚 / 参数 |
|------|-------------|
| LCD(GC9A01, 240×240, SPI) | PCLK=1, DATA0=0, DC=2, CS=7, 背光=5, 80MHz |
| 音频(PDM 喇叭,仅输出) | DOUT=3,默认 22050Hz/16bit/单声道 |
| RGB LED(WS2812 ×1) | GPIO8 |
| 旋转编码器 | A=10, B=6, 按下=9 |
| Flash | 4MB |

## 启用 SPIFFS(文件系统)

⚠️ 默认分区表没有 spiffs 分区,而 `bsp_spiffs_mount()` 找不到分区时会**直接 abort 并重启**
(不是返回错误 —— 内部用的是 `ESP_ERROR_CHECK`),所以 `app_main()` 默认【不调用】SPIFFS。
本目录已提供现成的 `partitions.csv`,启用步骤:

1. 打开 `sdkconfig.defaults` 末尾那三行自定义分区表配置(去掉 `#`);
2. 在 `main/main.c` 的 `app_main()` 里取消 `init_spiffs();` 的注释;
3. 分区表变了,必须 fullclean + 擦写:
   ```
   idf.py fullclean
   idf.py erase-flash flash monitor
   ```

## 能力清单(本板)

| 功能 | 状态 | 备注 |
|------|------|------|
| 显示 + LVGL | ✅ 已封装 | `bsp_display_start()` |
| 旋转编码器 | ✅ 已封装 | 已自动接为 LVGL 输入 |
| RGB LED | ✅ 已封装 | `bsp_led_indicator_create()` |
| 音频(喇叭) | ✅ 已封装 | `bsp_audio_codec_speaker_init()` |
| SPIFFS | ✅ 已封装 | 需自配分区表(见上) |
| Wi-Fi / BLE | ⚠️ IDF 原生 | 不归 BSP 管,直接用 ESP-IDF API |
| 红外 IR_RX/IR_TX | ⚠️ 需自行实现 | 板子有硬件,BSP 未封装,用 `driver/rmt` |
| 触摸 / 按键 / SD / MIC / 摄像头 / IMU / 电池 | ❌ 板子无 | — |
