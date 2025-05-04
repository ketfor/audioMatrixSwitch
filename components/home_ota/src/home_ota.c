#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "events.h"
#include "home_json.h"
#include "home_ota.h"

#define MAX_TLS_TASK_SIZE 5 * 1024
#define MAX_OTA_TASK_SIZE 4 * 1024
#define MAX_RELEASES 5
#define MAX_RELEASE_SIZE 3200
#define MAX_HTTP_OUTPUT_BUFFER MAX_RELEASES * MAX_RELEASE_SIZE
#define MAX_URL_SIZE 256
#define HASH_LEN 32
#define MUTEX_RI_TAKE_TICK_PERIOD portMAX_DELAY
#define GITHUB_REPO "audioMatrixSwitch2"
#define GITHUB_USER "ketfor"

static const char *TAG = "home_ota";
static const char *GITHUB_RELEASES_URL = "https://api.github.com/repos/%s/%s/releases";

extern const unsigned char api_cert_start[] asm("_binary_ca_cert_pem_start");
extern const unsigned char api_cert_end[]   asm("_binary_ca_cert_pem_end");

static releaseInfo_t releaseInfo;
SemaphoreHandle_t xMutexRI = NULL;

esp_err_t httpEventHandler(esp_http_client_event_t *evt)
{
    //static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }

            if (evt->user_data) {
                if (!esp_http_client_is_chunked_response(evt->client)) {
                    int copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                    output_len += copy_len;
                }
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

static void fillCommonConfig(esp_http_client_config_t *config){
    memset(config, 0, sizeof(esp_http_client_config_t));
    config->skip_cert_common_name_check = true;
    config->cert_pem = (char *)api_cert_start;
    config->cert_len = api_cert_end - api_cert_start;
    config->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3;
    config->buffer_size_tx = 1024;
    config->timeout_ms = 60000;
    config->use_global_ca_store = false;
    config->keep_alive_enable = true;
    config->event_handler = httpEventHandler;
}

static release_t * getReleaseById(uint64_t releaseId)
{
    for (uint8_t r = 0; r < releaseInfo.countReleases; r++) {
        release_t *release = &(releaseInfo.releases[r]);
        if (release->id == releaseId) return release;
    }
    return NULL;
}

static esp_err_t validate_version(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (memcmp(new_app_info->version, releaseInfo.currentRelease, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void setOtaState(home_ota_state_t otaState){
    if(xSemaphoreTake(xMutexRI, MUTEX_RI_TAKE_TICK_PERIOD ) == pdTRUE) {
        releaseInfo.otaState = otaState;
        xSemaphoreGive(xMutexRI);
    }
}

void otaTask(void *pvParameter)
{
    uint64_t releaseId = (uint64_t)pvParameter;
    setOtaState(HOME_OTA_UPDATING);

    release_t *release = getReleaseById(releaseId);
    if (release == NULL) {
        ESP_LOGE(TAG, "Failed OTA upgrade: release %llu not found", releaseId);
        setOtaState(HOME_OTA_UPDATE_RELEASE_NOTFOUND);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "Starting OTA upgrade to release %s", release->releaseName);
    if (strlen(release->fileUrl) == 0) {
        ESP_LOGE(TAG, "Failed OTA upgrade: release file_url is empty");
        setOtaState(HOME_OTA_UPDATE_RELEASE_FILEURL_ISEMPTY);
        vTaskDelete(NULL);
    }

    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config;
    fillCommonConfig(&config);
    config.url = release->fileUrl;

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
        .max_http_request_size = 1024 * 128
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        setOtaState(HOME_OTA_UPDATE_BEGIN_FAIL);
        vTaskDelete(NULL);
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: cannot esp_https_ota_get_img_desc");
        esp_https_ota_abort(https_ota_handle);
        setOtaState(HOME_OTA_UPDATE_CANNOT_GET_IMG_DESCR);
        vTaskDelete(NULL);
    }
    err = validate_version(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: cannot validate_image_header");
        esp_https_ota_abort(https_ota_handle);
        setOtaState(HOME_OTA_UPDATE_SAME_VERSION);
        vTaskDelete(NULL);
    }

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
        ESP_LOGE(TAG, "Failed OTA upgrade releases: complete data was not received.");
        setOtaState(HOME_OTA_UPDATE_NOT_COMPLITE_DATA);
        vTaskDelete(NULL);
    } 
    else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } 
        else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            setOtaState(HOME_OTA_UPDATE_IMAGE_CORRUPTED);
            vTaskDelete(NULL);
        }
    }
    setOtaState(HOME_OTA_IDLE);
    vTaskDelete(NULL);
}

static BaseType_t httpsRequest(const char *url, char *buf)
{
    esp_http_client_config_t config;
    fillCommonConfig(&config);
    config.url = url;
    config.user_data = buf;

    ESP_LOGI(TAG, "esp_http_client initializing..");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate esp_http_client handle!");
        return pdFALSE;
    }
    ESP_LOGI(TAG, "esp_http_client performing");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return pdTRUE;
}       

void httpsRequestGetReleases(void *pvParameter)
{
    if(xSemaphoreTake(xMutexRI, MUTEX_RI_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGE(TAG, "Failed request releases: mutex not taked");
        vTaskDelete(NULL);
    }
    // releses from github
    ESP_LOGI(TAG, "https_request for releases");

    char url[MAX_URL_SIZE];
    sprintf(url, GITHUB_RELEASES_URL, GITHUB_USER, GITHUB_REPO);
    char buf[MAX_HTTP_OUTPUT_BUFFER];
    httpsRequest(url, buf);
    // TOTO parse Releases

    cJSON *root = cJSON_Parse(buf);      
    int idx = 0;
    cJSON *jsonRelease = cJSON_GetArrayItem(root, idx);
    while (jsonRelease && idx < MAX_RELEASES) {
        release_t *release = &(releaseInfo.releases[idx]);
        jsonUInt64Value(jsonRelease, &(release->id), "id", 0);
        jsonStrValue(jsonRelease, release->releaseName, sizeof(release->releaseName), "name", "");
        jsonStrValue(jsonRelease, release->releaseUrl, sizeof(release->releaseUrl), "html_url", "");
        jsonStrValue(jsonRelease, release->tagName, sizeof(release->tagName), "tag_name", "");
        jsonStrValue(jsonRelease, release->published, sizeof(release->published), "published_at", "");
        
        cJSON *jsonAssets = cJSON_GetObjectItem(jsonRelease, "assets");
        if (jsonAssets) {
            cJSON *jsonAsset = cJSON_GetArrayItem(jsonAssets, 0);
            if (jsonAsset) {
                jsonStrValue(jsonAsset, release->fileName, sizeof(release->fileName), "name", "");
                jsonStrValue(jsonAsset, release->fileUrl, sizeof(release->fileUrl), "browser_download_url", "");      
            }
        }
        idx++;
        jsonRelease = cJSON_GetArrayItem(root, idx);            
    }
    cJSON_Delete(root);

    releaseInfo.countReleases = idx;
    releaseInfo.otaState = HOME_OTA_IDLE;
    time(&(releaseInfo.lastCheck));
    xSemaphoreGive(xMutexRI);
    vTaskDelete(NULL);
}

const char * getReleasesInfo()
{
    xSemaphoreTake(xMutexRI, MUTEX_RI_TAKE_TICK_PERIOD );
    ESP_LOGI(TAG, "response release info");
    cJSON *root = cJSON_CreateObject();
    cJSON *json_releases, *json_release;

    //inputs
    json_releases = cJSON_AddArrayToObject(root, "releases");
    for (uint8_t r = 0; r < releaseInfo.countReleases; r++) {
        release_t *release = &(releaseInfo.releases[r]);

        cJSON_AddItemToArray(json_releases, json_release = cJSON_CreateObject());

        cJSON_AddNumberToObject(json_release, "id", release->id);
        cJSON_AddStringToObject(json_release, "release_name", release->releaseName);
        cJSON_AddStringToObject(json_release, "release_url", release->releaseUrl);
        cJSON_AddStringToObject(json_release, "tag_name", release->tagName);
        cJSON_AddStringToObject(json_release, "published", release->published);
        cJSON_AddStringToObject(json_release, "file_url", release->fileUrl);
        cJSON_AddStringToObject(json_release, "file_name", release->fileName);
    }    

    cJSON_AddNumberToObject(root, "count_releases", releaseInfo.countReleases);
    cJSON_AddNumberToObject(root, "last_check", releaseInfo.lastCheck);
    cJSON_AddStringToObject(root, "current_release", releaseInfo.currentRelease);
    cJSON_AddNumberToObject(root, "ota_state", releaseInfo.otaState);
    xSemaphoreGive(xMutexRI);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json;
}

void doFirmwareUpgrade(uint64_t release)
{
    xTaskCreate (&otaTask, "otaTask", MAX_OTA_TASK_SIZE + MAX_TLS_TASK_SIZE, (void*)release, 5, NULL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}


void updateReleasesInfo()
{
    xTaskCreate(&httpsRequestGetReleases, "httpsRequest", MAX_RELEASE_SIZE * MAX_RELEASES + MAX_TLS_TASK_SIZE, NULL, 5, NULL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static void connectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (releaseInfo.lastCheck == 0) {
        updateReleasesInfo();
    }
}

void otaInit(void)
{   
    static StaticSemaphore_t xSemaphoreBuffer;
    xMutexRI = xSemaphoreCreateMutexStatic(&xSemaphoreBuffer);

    memset(&releaseInfo, 0, sizeof(releaseInfo));
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
        strlcpy(releaseInfo.currentRelease, running_app_info.version, sizeof(releaseInfo.currentRelease)); 
    }

#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
    }
#endif

    ESP_ERROR_CHECK(esp_event_handler_register(HOME_WIFI_EVENT, HOME_WIFI_EVENT_START, &connectHandler, NULL));
}