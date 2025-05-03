idf_component_register(SRCS "src/home_ota.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_https_ota app_update esp_netif esp-tls events home_json
                    EMBED_TXTFILES ${project_dir}/certs/ca_cert.pem)
                    