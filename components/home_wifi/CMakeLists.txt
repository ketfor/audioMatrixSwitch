idf_component_register(SRCS "src/home_wifi.c" "src/time_sync.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_wifi esp_netif esp_timer lwip home_json events nvs_preferences)
                    