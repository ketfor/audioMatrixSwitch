idf_component_register(SRCS "src/onboardled.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_event esp_driver_gpio events)