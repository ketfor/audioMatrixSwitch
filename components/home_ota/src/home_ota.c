#include <string.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
//#include "esp_crt_bundle.h"
//#include "events_types.h"
#include "home_ota.h"

static const char *TAG = "home_ota";

extern const char api_cert_start[] asm("_binary_ca_cert_pem_start");
extern const char api_cert_end[]   asm("_binary_ca_cert_pem_end");
//extern const char api_global_sign_start[] asm("_binary_globalsign_pem_start");
//extern const char api_global_sign_end[]   asm("_binary_globalsign_pem_end");

void otaTask(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA update");

    esp_err_t ota_finish_err = ESP_OK;

    esp_http_client_config_t config = {
        //.url = "https://objects.githubusercontent.com/github-production-release-asset-2e65be/962203917/e622b7f2-3246-4aa5-8d33-27be7a3df1da?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=releaseassetproduction%2F20250428%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20250428T172127Z&X-Amz-Expires=300&X-Amz-Signature=fec90fbce6d25673d678907f4fae0b2110de77a881c77a2ee7a7fbbdc7345564&X-Amz-SignedHeaders=host&response-content-disposition=attachment%3B%20filename%3Daudiomatrix_switch-v2.1.0.bin&response-content-type=application%2Foctet-stream",
        .url = "https://github.com/ketfor/audioMatrixSwitch2/releases/download/v2.1.0/audiomatrix_switch-v2.1.0.bin",
        //.url="https://s1166sas.storage.yandex.net/rdisk/79ca3059a53e406242878a74ca0ad0f35dfd2b0fdbc6755db092b8f06c57b368/680ea569/V1HNym5RHVX5ksF9OmBXSu7qknmEyse8GMIwqGu0JdsWrAQN1ULW_AIKALohPwFZtTt4hJvqjorUoVQjTzyZiQ==?uid=0&filename=audiomatrix_switch.bin&disposition=attachment&hash=Z7o2tSJpF%2BxcI9heNUsAjcUmMXEM5pw8DQXTP6lp4lYQLAuNUJaW%2BGZkRZWbEthDq/J6bpmRyOJonT3VoXnDag%3D%3D&limit=0&content_type=application%2Fx-dosexec&owner_uid=367858930&fsize=1067600&hid=0e4cd773ff745fcced9d92495ab08228&media_type=encoded&tknv=v2&ts=633c97b756c40&s=aeac877694688c1fe027c017bdc17cb20e3b1e2d1c0ff7158c374b6224ac201b&pb=U2FsdGVkX18qjKoHEkvKb6m3qoQzqBZTJVEGjrztwT33NqCS-nQ6Jmu03fzE22SoxHUl7GYgg8JyUnPgijJW9VHqPLhgbBVBh9Z0YX1ZsSk",
        //.url = "https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_https_ota.html",
        //.method = HTTP_METHOD_GET,
        //.is_async = false,
        .timeout_ms = 60000,
        //.disable_auto_redirect = false,
        //.max_redirection_count = 0,
        .skip_cert_common_name_check = true,
        .cert_pem = (char *)api_cert_start,
        //.cert_len = api_cert_end - api_cert_start,
        .use_global_ca_store = false,
        //.crt_bundle_attach = esp_crt_bundle_attach,
        //.transport_type = HTTP_TRANSPORT_OVER_SSL,
        .keep_alive_enable = true,
        
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
        .max_http_request_size = 1024 * 16
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        vTaskDelete(NULL);
    }

    /*
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        goto ota_end;
    }
    */

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            vTaskDelete(NULL);
        }
    }

    /*
ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    vTaskDelete(NULL);
*/
    /*
    ESP_LOGW(TAG, "Min free heap: %lu, curr free heap: %lu", esp_get_minimum_free_heap_size(), esp_get_free_heap_size());
    esp_err_t ret = esp_https_ota(&ota_config);
    ESP_LOGW(TAG, "Min free heap: %lu, curr free heap: %lu", esp_get_minimum_free_heap_size(), esp_get_free_heap_size());

    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware successfully upgraded");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        return pdFALSE;
    }

    return pdTRUE;
    */

}

BaseType_t doFirmwareUpgrade()
{
    xTaskCreate(&otaTask, "otaTask", 1024 * 8, NULL, 5, NULL);
    return pdTRUE;
}

/*

static void otaEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    doFirmwareUpgrade();
}
*/

void otaInit(void)
{
    //esp_ota_mark_app_valid_cancel_rollback();
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    //doFirmwareUpgrade();
    //ESP_ERROR_CHECK(esp_event_handler_register(HOME_WIFI_EVENT, HOME_WIFI_EVENT_NTP, &otaEventHandler, NULL));
}