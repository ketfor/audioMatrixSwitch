idf_component_register(SRCS "src/events.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_event 
                    REQUIRES esp_netif esp_wifi onboardled home_web_server home_wifi home_mqtt_client audiomatrix)