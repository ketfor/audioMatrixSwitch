#include <stdio.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "home_wifi.h"
#include "home_web_server.h"
#include "home_mqtt_client.h"
#include "events.h"
#include "audiomatrix.h"
#include "matrix_lcd.h"
#include "home_ota.h"

//static const char *TAG = "myapp";

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
    matrixLcdInit();
    lcdWriteStr("Initializing...");
    ESP_ERROR_CHECK(esp_netif_init());
    eventsInit();
    webServerInit();
    mqttClientInit();
    wifiInit();
    audiomatrixInit();
    otaInit();
}