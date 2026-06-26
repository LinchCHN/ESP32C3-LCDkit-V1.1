idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp32_c3_lcdkit
                    PRIV_REQUIRES esp_timer)
