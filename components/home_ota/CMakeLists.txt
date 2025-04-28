idf_component_register(SRCS "src/home_ota.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_https_ota app_update esp_netif mbedtls
                    EMBED_TXTFILES ${project_dir}/certs/ca_cert.pem)

#target_add_binary_data(${COMPONENT_TARGET} ${CMAKE_SOURCE_DIR}/certs/ca_cert.pem TEXT)
#target_add_binary_data(${COMPONENT_TARGET} ${CMAKE_SOURCE_DIR}/certs/globalsign.pem TEXT)