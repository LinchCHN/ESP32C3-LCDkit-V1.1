idf_component_register(SRCS "ui_manager.c" "screen_punch.c" "screen_info.c" "screen_main.c" "app_state.c" "ui_common.c" "main.c" "salary_cfg.c" "web_server.c" "cn_font.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp32_c3_lcdkit esp_wifi esp_netif esp_event nvs_flash lwip esp_http_server
                    PRIV_REQUIRES esp_timer)
