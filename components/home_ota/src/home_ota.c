#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "events.h"
#include "home_json.h"
#include "matrix_lcd.h"
#include "audiomatrix.h"
#include "home_ota.h"

#define MAX_TLS_TASK_SIZE 5 * 1024
#define MAX_OTA_TASK_SIZE 4 * 1024
#define MAX_RELEASES 5
#define MAX_RELEASE_SIZE 3200
#define MAX_HTTP_OUTPUT_BUFFER MAX_RELEASES * MAX_RELEASE_SIZE
#define MAX_URL_SIZE 256
#define HASH_LEN 32
#define MUTEX_RI_TAKE_TICK_PERIOD portMAX_DELAY
#define GITHUB_REPO "audioMatrixSwitch"
#define GITHUB_USER "ketfor"
#define BUFFSIZE 1024
#define SKIP_VALIDATE_VERSION

static const char *TAG = "home_ota";
static const char *GITHUB_RELEASES_URL = "https://api.github.com/repos/%s/%s/releases";

extern const unsigned char api_cert_start[] asm("_binary_ca_cert_pem_start");
extern const unsigned char api_cert_end[]   asm("_binary_ca_cert_pem_end");

static releaseInfo_t releaseInfo;
SemaphoreHandle_t xMutexRI = NULL;


typedef enum {
    USER_DATA_BUF = 0,
    USER_DATA_WRITE_PARTITION
} userDataType_t;

typedef struct {
    userDataType_t dataType;
    size_t dataLength;
    size_t totalDataLength;
    char *buf;
    size_t bufSize;
    esp_partition_t *spiffs_partition;
} userData_t;

esp_err_t httpEventHandler(esp_http_client_event_t *evt)
{
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
            if (evt->user_data) {
                userData_t *userData = (userData_t*)evt->user_data;
                if (userData->dataType == USER_DATA_BUF) {
                    if (userData->totalDataLength == 0) {
                        memset(userData->buf, 0, userData->bufSize);
                    }
                    if (!esp_http_client_is_chunked_response(evt->client)) {
                        userData->dataLength = MIN(evt->data_len, (userData->bufSize - userData->totalDataLength));
                        if (userData->dataLength) {
                            memcpy(userData->buf + userData->totalDataLength, evt->data, userData->dataLength);
                        }
                        userData->totalDataLength += userData->dataLength;
                    }
                }
                else if(userData->dataType == USER_DATA_WRITE_PARTITION) {
                    esp_err_t err;
                    if (userData->totalDataLength == 0) {
                        err = esp_partition_erase_range(userData->spiffs_partition, 
                            0, userData->spiffs_partition->size);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_partition_erase error: part: %s, offset: %d, length: %lu", userData->spiffs_partition->label, 0, userData->spiffs_partition->size);
                        }
                        ESP_LOGI(TAG, "Writing to <%s> partition at offset 0x%lx", userData->spiffs_partition->label, userData->spiffs_partition->address);
                    }
                    if (!esp_http_client_is_chunked_response(evt->client)) {
                        userData->dataLength = evt->data_len;
                        err = esp_partition_write(userData->spiffs_partition, 
                            userData->totalDataLength, (const void *)evt->data, userData->dataLength);
                        if (err != ESP_OK) {
                            ESP_LOGD(TAG, "error: esp_partition_write to <%s> partition, offset: %d, length:%d", userData->spiffs_partition->label, userData->totalDataLength, userData->dataLength);
                        }
                        userData->totalDataLength += userData->dataLength;
                    }
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
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
    config->buffer_size_tx = BUFFSIZE;
    config->timeout_ms = 60000;
    config->use_global_ca_store = false;
    config->keep_alive_enable = true;
}

static release_t * getReleaseById(uint64_t releaseId)
{
    for (uint8_t r = 0; r < releaseInfo.countReleases; r++) {
        release_t *release = &(releaseInfo.releases[r]);
        if (release->id == releaseId) return release;
    }
    return NULL;
}

#ifndef SKIP_VALIDATE_VERSION
static esp_err_t validateVersion(esp_app_desc_t *new_app_info)
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
#endif

static void setOtaState(home_ota_state_t otaState){
    if(xSemaphoreTake(xMutexRI, MUTEX_RI_TAKE_TICK_PERIOD ) == pdTRUE) {
        releaseInfo.otaState = otaState;
        xSemaphoreGive(xMutexRI);
    }
    switch (otaState) {
        case UPDATE_IDLE:
        case UPDATE_OTA_SUCCESSFULLY:
        case UPDATE_WWW_SUCCESSFULLY:
            sendOutputToDispaly();
            break;
        case UPDATE_OTA_UPDATING:
            lcdClearScreen();
            lcdHome();
            lcdWriteStr("Updating OTA...");
            break;
        case UPDATE_WWW_UPDATING:
            lcdClearScreen();
            lcdHome();
            lcdWriteStr("Updating WWW...");
            break;
        default:
            lcdClearScreen();
            lcdHome();
            lcdWriteStr("Failed update");
    }
}

static BaseType_t httpsRequest(const char *url, userData_t *userData)
{
    esp_http_client_config_t config;
    fillCommonConfig(&config);
    config.url = url;
    config.user_data = userData;
    config.event_handler = httpEventHandler;

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
    return (err == ESP_OK)?pdTRUE:pdFALSE;
}  

static home_ota_state_t otaUpdate(release_t *release)
{
    ESP_LOGI(TAG, "Starting OTA upgrade to release %s", release->releaseName);
    if (strlen(release->fileOtaUrl) == 0) {
        ESP_LOGE(TAG, "Failed OTA upgrade: release file_url is empty");
        return UPDATE_OTA_FILEURL_ISEMPTY;
    }

    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config;
    fillCommonConfig(&config);
    config.url = release->fileOtaUrl;

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
        .max_http_request_size = 1024 * 128
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        return UPDATE_OTA_BEGIN_FAIL;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: cannot esp_https_ota_get_img_desc");
        esp_https_ota_abort(https_ota_handle);
        return UPDATE_OTA_CANNOT_GET_IMG_DESCR;
    }

#ifndef SKIP_VALIDATE_VERSION
    err = validateVersion(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: cannot validate_image_header");
        esp_https_ota_abort(https_ota_handle);
        return HOME_OTA_UPDATE_SAME_VERSION;
    }
#endif

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
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: complete data was not received.");
        return UPDATE_OTA_NOT_COMPLITE_DATA;
    } 
    else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err != ESP_OK) || (ota_finish_err != ESP_OK)) {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed: image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            return UPDATE_OTA_IMAGE_CORRUPTED;
        }
    }
    ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade OTA part successful");
    return UPDATE_OTA_SUCCESSFULLY;
}

static home_ota_state_t wwwUpdate(release_t *release)
{
    esp_partition_t *spiffs_partition = NULL;
    ESP_LOGI(TAG, "Starting upgrade SPIFFS WWW");
    
    if (strlen(release->fileWwwUrl) == 0) {
        ESP_LOGE(TAG, "Failed upgrade SPIFFS WWW: file_url is empty");
        return UPDATE_WWW_FAIL;
    }
    
    esp_partition_iterator_t spiffs_partition_iterator = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    while (spiffs_partition_iterator != NULL){
        spiffs_partition = (esp_partition_t *)esp_partition_get(spiffs_partition_iterator);
        spiffs_partition_iterator = esp_partition_next(spiffs_partition_iterator);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_partition_iterator_release(spiffs_partition_iterator);
    
    userData_t userData = {
        .dataType = USER_DATA_WRITE_PARTITION,
        .dataLength = 0,
        .totalDataLength = 0,
        .buf = NULL,
        .bufSize = 0,
        .spiffs_partition = spiffs_partition,
    };
    httpsRequest(release->fileWwwUrl, &userData);

    if (userData.totalDataLength < spiffs_partition->size) {
        ESP_LOGE(TAG, "Upgrade SPIFFS WWW failed: complete data was not received.");
        return UPDATE_WWW_NOT_COMPLITE_DATA;
    } 

    ESP_LOGI(TAG, "Upgrade SPIFFS WWW part successful");
    return UPDATE_WWW_SUCCESSFULLY;
}

void otaTask(void *pvParameter)
{
    uint64_t releaseId = *((uint64_t*)pvParameter);
    home_ota_state_t res = UPDATE_OTA_UPDATING;
    setOtaState(res);

    release_t *release = getReleaseById(releaseId);
    if (release == NULL) {
        ESP_LOGE(TAG, "Failed OTA upgrade: release %llu not found", releaseId);
        setOtaState(UPDATE_RELEASE_NOTFOUND);
        vTaskDelete(NULL);
    }

    res = otaUpdate(release);
    if (res != UPDATE_OTA_SUCCESSFULLY){
        setOtaState(res);
        vTaskDelete(NULL);
    }

    res = UPDATE_WWW_UPDATING;
    setOtaState(res);
    res = wwwUpdate(release);
    if (res != UPDATE_WWW_SUCCESSFULLY){
        setOtaState(res);
    }

    ESP_LOGI(TAG, "Upgrade successful. Rebooting ...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
}     

void checkReleaseTask(void *pvParameter)
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
    userData_t userData = {
        .dataType = USER_DATA_BUF,
        .dataLength = 0,
        .totalDataLength = 0,
        .buf = buf,
        .bufSize = MAX_HTTP_OUTPUT_BUFFER
    };
    httpsRequest(url, &userData);
    // TOTO parse Releases

    cJSON *root = cJSON_Parse(buf);      
    int idx = 0;
    cJSON *jsonRelease = cJSON_GetArrayItem(root, idx);
    while (jsonRelease && idx < MAX_RELEASES) {
        release_t *release = &(releaseInfo.releases[idx]);
        char published[21];
        struct tm tmPublished;
        jsonUInt64Value(jsonRelease, &(release->id), "id", 0);
        jsonStrValue(jsonRelease, release->releaseName, sizeof(release->releaseName), "name", "");
        jsonStrValue(jsonRelease, release->releaseUrl, sizeof(release->releaseUrl), "html_url", "");
        jsonStrValue(jsonRelease, release->tagName, sizeof(release->tagName), "tag_name", "");
        jsonStrValue(jsonRelease, published, sizeof(published), "published_at", "");
        strptime(published, "%Y-%m-%dT%H:%M:%SZ", &tmPublished);
        release->published = mktime(&tmPublished);
        
        cJSON *jsonAssets = cJSON_GetObjectItem(jsonRelease, "assets");
        if (jsonAssets) {
            int idxA = 0;
            cJSON *jsonAsset = cJSON_GetArrayItem(jsonAssets, idxA);
            while (jsonAsset) {
                char fileName[64] = {0};
                jsonStrValue(jsonAsset, fileName, sizeof(fileName), "name", "");
                
                if (strcmp(fileName, "audiomatrix_switch.bin") == 0) {
                    strlcpy(release->fileOtaName, fileName, sizeof(release->fileOtaName));
                    jsonStrValue(jsonAsset, release->fileOtaUrl, sizeof(release->fileOtaUrl), "browser_download_url", "");   
                }
                else if (strcmp(fileName, "www.bin") == 0) {
                    strlcpy(release->fileWwwName, fileName, sizeof(release->fileWwwName));
                    jsonStrValue(jsonAsset, release->fileWwwUrl, sizeof(release->fileWwwUrl), "browser_download_url", "");   
                } 
                idxA++;
                jsonAsset = cJSON_GetArrayItem(jsonAssets, idxA); 
            }
        }
        idx++;
        jsonRelease = cJSON_GetArrayItem(root, idx);            
    }
    cJSON_Delete(root);

    releaseInfo.countReleases = idx;
    releaseInfo.otaState = UPDATE_IDLE;
    time(&(releaseInfo.lastCheck));
    xSemaphoreGive(xMutexRI);
    vTaskDelete(NULL);
}

const char * getCurrentRelease()
{
    return releaseInfo.currentRelease;
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
        cJSON_AddNumberToObject(json_release, "published", release->published);
        cJSON_AddStringToObject(json_release, "file_ota_url", release->fileOtaUrl);
        cJSON_AddStringToObject(json_release, "file_ota_name", release->fileOtaName);
        cJSON_AddStringToObject(json_release, "file_www_url", release->fileWwwUrl);
        cJSON_AddStringToObject(json_release, "file_www_name", release->fileWwwName);
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
    xTaskCreate (&otaTask, "otaTask", MAX_OTA_TASK_SIZE + MAX_TLS_TASK_SIZE, (void*)&release, 5, NULL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}


void updateReleasesInfo()
{
    xTaskCreate(&checkReleaseTask, "checkReleaseTask", MAX_RELEASE_SIZE * MAX_RELEASES + MAX_TLS_TASK_SIZE, NULL, 5, NULL);
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