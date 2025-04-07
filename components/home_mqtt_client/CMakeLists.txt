idf_component_register(SRCS "src/home_mqtt_client.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES mqtt json events audiomatrix
                    )
