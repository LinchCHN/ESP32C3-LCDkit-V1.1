idf_component_register(SRCS "main.c" "salary_cfg.c" "web_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp32_c3_lcdkit esp_wifi esp_netif esp_event nvs_flash lwip esp_http_server
                    PRIV_REQUIRES esp_timer)
