idf_component_register(SRCS "src/audiomatrix.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES home_wifi home_json events nvs_preferences
                    )
