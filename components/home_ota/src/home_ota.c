#include <string.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "home_ota.h"

static const char *TAG = "home_ota";

extern const unsigned char api_cert_start[] asm("_binary_ca_cert_pem_start");
extern const unsigned char api_cert_end[]   asm("_binary_ca_cert_pem_end");

void otaTask(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA update");

    esp_err_t ota_finish_err = ESP_OK;

    esp_http_client_config_t config = {
        .url = "https://github.com/ketfor/audioMatrixSwitch2/releases/download/v2.1.0/audiomatrix_switch-v2.1.0.bin",
        .skip_cert_common_name_check = true,
        .cert_pem = (char *)api_cert_start,
        .cert_len = api_cert_end - api_cert_start,
        .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3,
        .buffer_size_tx = 1024,
        .timeout_ms = 60000,
        .use_global_ca_store = false,
        .keep_alive_enable = true,    
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
        .max_http_request_size = 1024 * 128
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

void otaInit(void)
{
    esp_ota_mark_app_valid_cancel_rollback();
}