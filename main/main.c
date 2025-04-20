#include <stdio.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "home_wifi.h"
#include "home_web_server.h"
#include "home_mqtt_client.h"
#include "events.h"
#include "onboardled.h"
#include "audiomatrix.h"

#define TAG "myapp"

void systemInit() {

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void) {

    systemInit();
    eventsInit();
    onboardledInit();
    webServerInit();
    mqttClientInit();
    wifiStationInit();
    audiomatrixInit();
}