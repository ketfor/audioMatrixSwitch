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
#include "home_wifi.h"
#include "home_mqtt_client.h"

#define TAG "home_web_server"

static void strcpyToChar(char *dstStr, const char *srcStr, size_t dstStrSize, char ch){
    size_t i = 0;
    while (i < dstStrSize - 1 && srcStr[i] != 0 && srcStr[i] != ch) {
        dstStr[i] = srcStr[i];
        i++;
    }
    dstStr[i] = 0;
}

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

static esp_err_t getPostContent(httpd_req_t *req, char *buf, size_t bufSize)
{
    int total_len = req->content_len;
    int cur_len = 0;
    
    int received = 0;
    if (total_len >= bufSize) {
        return -21002;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            return -21003;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "post: %s", buf);
    return ESP_OK;
}

static void jsonStrValue(cJSON *json, char *out, size_t outSize, char *param, char *def)
{
    if (cJSON_HasObjectItem(json, param)) {
        strlcpy(out, cJSON_GetObjectItem(json, param)->valuestring, outSize);
    }
    else {
        strlcpy(out, def, outSize);
    }
}

static void jsonUInt8Value(cJSON *json, uint8_t *out, char *param, int def)
{
    if (cJSON_HasObjectItem(json, param)) {
        *out = cJSON_GetObjectItem(json, param)->valueint;
    }
    else {
        *out = def;
    }
}

static void jsonUInt16Value(cJSON *json, uint16_t *out, char *param, int def)
{
    if (cJSON_HasObjectItem(json, param)) {
        *out = cJSON_GetObjectItem(json, param)->valueint;
    }
    else {
        *out = def;
    }
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t restCommonGetHandler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));

    ESP_LOGI(TAG, "uri: %s", req->uri);
    if (strcmp(req->uri, "/matrix") == 0 ||
            strcmp(req->uri, "/config") == 0 ||
            strcmp(req->uri, "/wifi") == 0 ||
            strcmp(req->uri, "/mqtt") == 0) 
    {
        strlcat(filepath, req->uri, sizeof(filepath));
        strlcat(filepath, ".html", sizeof(filepath));
    }
    else if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        char file[FILE_PATH_MAX];
        strcpyToChar(file, req->uri, sizeof(file), '?');
        strlcat(filepath, file, sizeof(filepath));
    }
    ESP_LOGI(TAG, "Filepath is %s", filepath);
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGW(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        //return ESP_FAIL;
        strlcpy(filepath, rest_context->base_path, sizeof(filepath));
        strlcat(filepath, "/404.html", sizeof(filepath));
        fd = open(filepath, O_RDONLY, 0);
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

static esp_err_t deviceStateGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    httpd_resp_set_type(req, "application/json");
    
    const char *deviceState = getDeviceState();
    httpd_resp_sendstr(req, deviceState);
    free((void *)deviceState);
    return ESP_OK;
}

static esp_err_t deviceFactoryGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    setDefaultPreferences();
    httpd_resp_set_type(req, "application/json");
    
    const char *deviceConfig = getDeviceConfig();
    httpd_resp_sendstr(req, deviceConfig);
    free((void *)deviceConfig);
    return ESP_OK;
}

static esp_err_t deviceConfigGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    httpd_resp_set_type(req, "application/json");
    
    const char *deviceConfig = getDeviceConfig();
    httpd_resp_sendstr(req, deviceConfig);
    free((void *)deviceConfig);
    return ESP_OK;
}

static esp_err_t deviceSetPostHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    esp_err_t err = getPostContent(req, buf, SCRATCH_BUFSIZE); 

    if (err == -21002) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Post content too long"));
        return ESP_FAIL;
    }
    else if (err == -21003) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Failed to post control value"));
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_Parse(buf);
    if (cJSON_HasObjectItem(root, "output_state")) {
        cJSON *jsonOutputState = cJSON_GetObjectItem(root, "output_state");
        uint8_t output = cJSON_GetObjectItem(jsonOutputState, "output")->valueint;
        uint8_t input = cJSON_GetObjectItem(jsonOutputState, "input")->valueint;
        savePort(output, input);
    }
    else if (cJSON_HasObjectItem(root, "device")) {
        cJSON *jsonDevice = cJSON_GetObjectItem(root, "device");
        device_t pdevice;
        device_t *device = getDevice();
        
        strlcpy(pdevice.identifier, "", sizeof(pdevice.identifier));
        jsonStrValue(jsonDevice, pdevice.name, sizeof(pdevice.name), "name", device->name);
        jsonStrValue(jsonDevice, pdevice.configurationUrl, sizeof(pdevice.configurationUrl), "conf_url", device->configurationUrl);
        jsonStrValue(jsonDevice, pdevice.stateTopic, sizeof(pdevice.stateTopic), "state_topic", device->stateTopic);
        jsonStrValue(jsonDevice, pdevice.hassTopic, sizeof(pdevice.hassTopic), "hass_topic", device->hassTopic);
        
        if (cJSON_HasObjectItem(root, "inputs")) {
            cJSON *jsonInputs = cJSON_GetObjectItem(root, "inputs");
            for (uint8_t num = 0; num < IN_PORTS; num++) {
                cJSON *jsonInput = cJSON_GetArrayItem(jsonInputs, num);
                if (jsonInput != NULL) {
                    input_t *pinput = &(pdevice.inputs[num]);
                    input_t *input = &(device->inputs[num]);
                    jsonStrValue(jsonInput, pinput->name, sizeof(pinput->name), "name", input->name);
                    jsonStrValue(jsonInput, pinput->shortName, sizeof(pinput->shortName), "short_name", input->shortName);
                    jsonStrValue(jsonInput, pinput->longName, sizeof(pinput->longName), "long_name", input->longName);
                }
            }
        }
        if (cJSON_HasObjectItem(root, "outputs")) {
            cJSON *jsonOutputs = cJSON_GetObjectItem(root, "outputs");
            for (uint8_t num = 0; num < OUT_PORTS; num++) {
                cJSON *jsonOutput = cJSON_GetArrayItem(jsonOutputs, num);
                if (jsonOutput != NULL) {
                    output_t *poutput = &(pdevice.outputs[num]);
                    output_t *output = &(device->outputs[num]);
                    jsonUInt8Value(jsonOutput, &(poutput->class), "class", output->class);
                    output->class = cJSON_GetObjectItem(jsonOutput, "class")->valueint;
                    output->num = num;
                    jsonStrValue(jsonOutput, poutput->name, sizeof(poutput->name), "name", output->name);
                    jsonStrValue(jsonOutput, poutput->shortName, sizeof(poutput->shortName), "short_name", output->shortName);
                    jsonStrValue(jsonOutput, poutput->longName, sizeof(poutput->longName), "long_name", output->longName);
                    jsonUInt8Value(jsonOutput, &(poutput->inputPort), "input", output->inputPort);
                }
            }
        }
        saveConfig(&pdevice);
    }
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    
    const char *deviceState = getDeviceState();
    httpd_resp_sendstr(req, deviceState);
    free((void *)deviceState);
    return ESP_OK;
}

static esp_err_t wifiConfigGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    httpd_resp_set_type(req, "application/json");
    
    const char *jsonWifiConfig = getJsonWifiConfig();
    httpd_resp_sendstr(req, jsonWifiConfig);
    free((void *)jsonWifiConfig);
    return ESP_OK;
}

static esp_err_t wifiSetPostHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    esp_err_t err = getPostContent(req, buf, SCRATCH_BUFSIZE); 

    if (err == -21002) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Post content too long"));
        return ESP_FAIL;
    }
    else if (err == -21003) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Failed to post control value"));
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_Parse(buf);
    cJSON *jsonWifiConfig = cJSON_GetObjectItem(root, "wifi_config");
    wifiConfig_t pWifiConfig;
    wifiConfig_t *wifiConfig = getWifiConfig();
    
    jsonUInt8Value(jsonWifiConfig, &(pWifiConfig.type), "type", wifiConfig->type);
    jsonStrValue(jsonWifiConfig, pWifiConfig.ip, sizeof(pWifiConfig.ip), "ip", wifiConfig->ip);
    jsonStrValue(jsonWifiConfig, pWifiConfig.hostname, sizeof(pWifiConfig.hostname), "hostname", wifiConfig->hostname);
    jsonStrValue(jsonWifiConfig, pWifiConfig.ssid, sizeof(pWifiConfig.ssid), "ssid", wifiConfig->ssid);
    jsonStrValue(jsonWifiConfig, pWifiConfig.password, sizeof(pWifiConfig.password), "password", wifiConfig->password);
    
    saveWifiConfig(&pWifiConfig);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    
    const char *jsonNWifiConfig = getJsonWifiConfig();
    httpd_resp_sendstr(req, jsonNWifiConfig);
    free((void *)jsonNWifiConfig);
    return ESP_OK;
}

static esp_err_t mqttConfigGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    httpd_resp_set_type(req, "application/json");
    
    const char *jsonMqttConfig = getJsonMqttConfig();
    httpd_resp_sendstr(req, jsonMqttConfig);
    free((void *)jsonMqttConfig);
    return ESP_OK;
}

static esp_err_t mqttSetPostHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    esp_err_t err = getPostContent(req, buf, SCRATCH_BUFSIZE); 

    if (err == -21002) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Post content too long"));
        return ESP_FAIL;
    }
    else if (err == -21003) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, JSON_Message("Failed to post control value"));
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_Parse(buf);
    mqttConfig_t pMqttConfig;
    mqttConfig_t *mqttConfig = getMqttConfig();
    
    jsonStrValue(root, pMqttConfig.protocol, sizeof(pMqttConfig.protocol), "protocol", mqttConfig->protocol);
    jsonStrValue(root, pMqttConfig.host, sizeof(pMqttConfig.host), "host", mqttConfig->host);
    jsonUInt16Value(root, &(pMqttConfig.port), "port", mqttConfig->port);
    jsonStrValue(root, pMqttConfig.username, sizeof(pMqttConfig.username), "username", mqttConfig->username);
    jsonStrValue(root, pMqttConfig.password, sizeof(pMqttConfig.password), "password", mqttConfig->password);
    
    saveMqttConfig(&pMqttConfig);
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    
    const char *jsonNMqttConfig = getJsonMqttConfig();
    httpd_resp_sendstr(req, jsonNMqttConfig);
    free((void *)jsonNMqttConfig);
    return ESP_OK;
}

static esp_err_t systemInfoGetHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: %s", req->uri);
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chipInfo.cores);
    const char *sysInfo = cJSON_Print(root);
    httpd_resp_sendstr(req, sysInfo);
    free((void *)sysInfo);
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
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK){
        ESP_LOGE(TAG, "Start HTTP server failed");
        free(rest_context);
        return NULL;
    }
    ESP_LOGI(TAG, "Start HTTP server OK");

    httpd_uri_t deviceStateGetUri = {
        .uri = "/api/v1/device/state",
        .method = HTTP_GET,
        .handler = deviceStateGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &deviceStateGetUri);

    httpd_uri_t deviceFactoryGetUri = {
        .uri = "/api/v1/device/factory",
        .method = HTTP_GET,
        .handler = deviceFactoryGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &deviceFactoryGetUri);

    httpd_uri_t deviceConfigGetUri = {
        .uri = "/api/v1/device/config",
        .method = HTTP_GET,
        .handler = deviceConfigGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &deviceConfigGetUri);

    httpd_uri_t deviceSetPostUri = {
        .uri = "/api/v1/device/set",
        .method = HTTP_POST,
        .handler = deviceSetPostHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &deviceSetPostUri);

    httpd_uri_t wifiConfigGetUri = {
        .uri = "/api/v1/wifi/config",
        .method = HTTP_GET,
        .handler = wifiConfigGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &wifiConfigGetUri);

    httpd_uri_t wifiSetPostUri = {
        .uri = "/api/v1/wifi/set",
        .method = HTTP_POST,
        .handler = wifiSetPostHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &wifiSetPostUri);
    
    httpd_uri_t mqttConfigGetUri = {
        .uri = "/api/v1/mqtt/config",
        .method = HTTP_GET,
        .handler = mqttConfigGetHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mqttConfigGetUri);

    httpd_uri_t mqttSetPostUri = {
        .uri = "/api/v1/mqtt/set",
        .method = HTTP_POST,
        .handler = mqttSetPostHandler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &mqttSetPostUri);

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

#if CONFIG_WEB_DEPLOY_SF
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