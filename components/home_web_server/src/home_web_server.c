#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_vfs_semihost.h"
#include "esp_spiffs.h"
#include "esp_chip_info.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "home_web_server.h"
#include "events_types.h"
#include "audiomatrix.h"

#define TAG "home_web_server"

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

static const char * JSON_Message(const char *mes )
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", mes);
    const char *jmes = cJSON_Print(root);
    cJSON_Delete(root);
    return jmes;
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t restCommonGetHandler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));

    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t onboardLedSetPostHandler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("content too long"));
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Failed to post control value"));
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "data: %s", buf);
    cJSON *root = cJSON_Parse(buf);
    int type = cJSON_GetObjectItem(root, "type")->valueint;
    led_strip_collor_t color = {
        .red = cJSON_GetObjectItem(root, "red")->valueint,
        .green = cJSON_GetObjectItem(root, "green")->valueint,
        .blue = cJSON_GetObjectItem(root, "blue")->valueint
    };
    //ESP_LOGI(TAG, "Light control: red = %d, green = %d, blue = %d", color.red, color.green, color.blue);
    cJSON_Delete(root);

    esp_err_t err;
    switch (type){
        case 0:
            err = esp_event_post(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &color, sizeof(color), portMAX_DELAY);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post event to \"%s\" #%d: %d (%s)", ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, err, esp_err_to_name(err));
            };
            break;
        case 1:
            err = eventsCallbackExec(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &color);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to exec callback to \"%s\" #%d: %d (%s)", ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, err, esp_err_to_name(err));
            };
            break;
        case 2:
            BaseType_t result = eventsDataQueuePost(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &color, sizeof(color), 0);
            if (result != pdTRUE) {
                ESP_LOGE(TAG, "Failed to post queue event to \"%s\" #%d", ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR);
            };
            break;
        default:
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, JSON_Message("Post control value successfully"));
    return ESP_OK;
}

static esp_err_t systemInfoGetHandler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

static httpd_handle_t startWebServer(const char *base_path) 
{
    httpd_handle_t server;

    if (!(base_path)) {
        ESP_LOGE(TAG, "Wrong base path");
        return NULL;
    }
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    if (!(rest_context)){
        ESP_LOGE(TAG, "No memory for rest context");
        return NULL;
    }

    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK){
        ESP_LOGE(TAG, "Start HTTP server failed");
        free(rest_context);
        return NULL;
    }
    ESP_LOGI(TAG, "Start HTTP server OK");

    httpd_uri_t boardLightSetPostUri = {
        .uri = "/api/v1/onboardled/set",
        .method = HTTP_POST,
        .handler = onboardLedSetPostHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &boardLightSetPostUri);

    httpd_uri_t systemInfoGetUri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = systemInfoGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &systemInfoGetUri);

    httpd_uri_t commonGetUri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = restCommonGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &commonGetUri);

    return server;
}

static esp_err_t stopWebServer(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Stoping HTTP Server");
    if (httpd_stop(server) != ESP_OK) {
        ESP_LOGE(TAG, "Stop HTTP server failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Stop HTTP server OK");
    return ESP_OK;
}

static void connectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        *server = startWebServer(CONFIG_WEB_MOUNT_POINT);
    }
}

static void disconnectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        if (stopWebServer(*server) == ESP_OK) *server = NULL;
    }
}

#if CONFIG_WEB_DEPLOY_SEMIHOST
static esp_err_t fsInit(void)
{
    esp_err_t ret = esp_vfs_semihost_register(CONFIG_WEB_MOUNT_POINT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register semihost driver (%s)!", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    //dir();
    return ESP_OK;
}
#endif

#if CONFIG_WEB_DEPLOY_SD
esp_err_t fsInit(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_WEB_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}
#endif

void webServerInit(void)
{
    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(fsInit());
    server = startWebServer(CONFIG_WEB_MOUNT_POINT);
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connectHandler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnectHandler, &server));
    ESP_LOGI(TAG, "Webserver init finished.");
}