idf_component_register(SRCS "src/audiomatrix.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES home_wifi json events nvs_preferences
                    )
