idf_component_register(SRCS "src/home_wifi.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_wifi esp_netif lwip time_sync home_json events nvs_preferences)
                    