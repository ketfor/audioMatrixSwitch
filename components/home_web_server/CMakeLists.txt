idf_component_register(SRCS "src/home_web_server.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_http_server spiffs json events audiomatrix home_wifi home_mqtt_client
                    REQUIRES vfs)

if(CONFIG_WEB_DEPLOY_SF)
    #set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/front/default")
    set(WEB_SRC_DIR "/Users/home/projects/IdeaProjects/audiomtrx.nextjs/out")
    if(EXISTS ${WEB_SRC_DIR})
        spiffs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
    else()
        message(FATAL_ERROR "${WEB_SRC_DIR}/dist doesn't exit. Please run 'npm run build' in ${WEB_SRC_DIR}")
    endif()     
endif()
