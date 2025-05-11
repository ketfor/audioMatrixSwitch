#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_preferences.h"

static const char *TAG = "nvs_preferences";

BaseType_t nvsOpen(const char* name_group, nvs_open_mode_t open_mode, nvs_handle_t *pHandle)
{
    esp_err_t err = nvs_open(name_group, open_mode, pHandle); 
    if (err != ESP_OK) {
        if (!((err == ESP_ERR_NVS_NOT_FOUND) && (open_mode == NVS_READONLY))) {
            ESP_LOGE(TAG, "Error opening NVS namespace \"%s\": %d (%s)!", name_group, err, esp_err_to_name(err));
        };
        return pdFALSE;
    };
    return pdTRUE;
}

BaseType_t getStrPref(nvs_handle_t pHandle, const char *key, char *value, size_t valueSize) 
{
    size_t outValueSize;
    esp_err_t err;
    err = nvs_get_str(pHandle, key, NULL, &outValueSize);
    if(err == ESP_OK) {
        if (valueSize < outValueSize) {
            ESP_LOGW(TAG, "Failed to get preferences \"%s\": The size (%d) of the returned string is larger than the buffer size (%d)", key, outValueSize, valueSize);
            char* newValue = malloc(outValueSize);
            nvs_get_str(pHandle, key, newValue, &outValueSize);
            strlcpy(value, newValue, valueSize);
            free(newValue);
            return pdTRUE;
        }
        nvs_get_str(pHandle, key, value, &outValueSize);
        return pdTRUE;
    } 
    else {
        ESP_LOGE(TAG, "Failed to get preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t setStrPref(nvs_handle_t pHandle, const char *key, char *value) 
{
    esp_err_t err = nvs_set_str(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to set preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t getUInt8Pref(nvs_handle_t pHandle, const char *key, uint8_t *value)
{
    esp_err_t err = nvs_get_u8(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to get preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t setUInt8Pref(nvs_handle_t pHandle, const char *key, uint8_t value)
{
    esp_err_t err = nvs_set_u8(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to set preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t getUInt16Pref(nvs_handle_t pHandle, const char *key, uint16_t *value)
{
    esp_err_t err = nvs_get_u16(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to get preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t setUInt16Pref(nvs_handle_t pHandle, const char *key, uint16_t value)
{
    esp_err_t err = nvs_set_u16(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to set preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t getUInt32Pref(nvs_handle_t pHandle, const char *key, uint32_t *value)
{
    esp_err_t err = nvs_get_u32(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to get preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}

BaseType_t setUInt32Pref(nvs_handle_t pHandle, const char *key, uint32_t value)
{
    esp_err_t err = nvs_set_u32(pHandle, key, value);
    if(err == ESP_OK)
        return pdTRUE;
    else {
        ESP_LOGE(TAG, "Failed to set preferences \"%s\": %d (%s)", key, err, esp_err_to_name(err));
        return pdFALSE;
    }
}