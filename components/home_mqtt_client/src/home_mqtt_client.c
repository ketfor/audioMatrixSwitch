#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_preferences.h"
#include "home_json.h"
#include "home_mqtt_client.h"
#include "events_types.h"
#include "audiomatrix.h"

#define TAG "home_mqtt_client"

#define NVSGROUP "mqtt"

#define PUBLISH_STATE_BIT       BIT0
#define PUBLISH_CONFIG_BIT      BIT1
#define SUBSCRIBE_STATE_BIT     BIT2
#define MUTEX_TAKE_TICK_PERIOD 1000 / portTICK_PERIOD_MS
#define STACK_SIZE 5120
#define MQTT_MAXIMUM_RETRY 5

static EventGroupHandle_t xEventGroup;
static bool mqttConnected = false;
static mqttConfig_t mqttConfig;
static esp_mqtt_client_handle_t client = NULL;
static SemaphoreHandle_t xMutex;
static nvs_handle_t pHandle = 0;
static int s_retry_num = 0;

static void subscribeState()
{
    char topic[64];
    getHaMQTTStateTopic(topic, sizeof(topic));
    strlcat(topic, "/set/#", sizeof(topic));
    ESP_LOGI(TAG, "Subscribe to a topic \"%s\"", topic);
    esp_mqtt_client_subscribe(client, topic, 0);
}

static void publishState()
{
    char topic[64], payload[1024];
    if(getHaMQTTDeviceState(topic, sizeof(topic), payload, sizeof(payload)) == pdTRUE){
        ESP_LOGI(TAG, "Publish a topic \"%s\"", topic);
        esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
    }
}

static void publishConfig()
{
    char topic[64], payload[1024];
    for (uint8_t num = 0; num < OUT_PORTS; num++) {
        if(getHaMQTTOutputConfig(num, CLASS_SWITCH, topic, sizeof(topic), payload, sizeof(payload)) == pdTRUE){
            ESP_LOGI(TAG, "Publish a topic \"%s\"", topic);
            esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
        }
        if(getHaMQTTOutputConfig(num, CLASS_SELECT, topic, sizeof(topic), payload, sizeof(payload)) == pdTRUE){
            ESP_LOGI(TAG, "Publish a topic \"%s\"", topic);
            esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
        }
    }
    publishState(client);
}

static void audiomatrixEventTask(void *pvParameters) 
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(xEventGroup,
            SUBSCRIBE_STATE_BIT | PUBLISH_STATE_BIT | PUBLISH_CONFIG_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
            
        if (bits & SUBSCRIBE_STATE_BIT) subscribeState();
        if (bits & PUBLISH_STATE_BIT) publishState();
        if (bits & PUBLISH_CONFIG_BIT) publishConfig();
    }
}

static void audiomatrixEventHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (!mqttConnected) {
        ESP_LOGW(TAG, "audiomatrixEventHandler: MQTT not connected");
        return;
    }

    esp_mqtt_event_handle_t event = event_data;
    switch ((audiomatrix_event_t)event_id) {
        case AUDIOMATRIX_EVENT_PORT_CHANGED:
            xEventGroupSetBits(xEventGroup, PUBLISH_STATE_BIT);
            break;
        case AUDIOMATRIX_EVENT_CONFIG_CHANGED:
            xEventGroupSetBits(xEventGroup, PUBLISH_CONFIG_BIT);
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqttConnected = true;
        s_retry_num = 0;
        xEventGroupSetBits(xEventGroup, SUBSCRIBE_STATE_BIT|PUBLISH_CONFIG_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqttConnected = false;
        /*
        if (s_retry_num < MQTT_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "retry connect to %s%s:%lu", mqttConfig.protocol, mqttConfig.host, mqttConfig.port);
        } else {
            ESP_LOGI(TAG, "failed connect to %s%s:%lu", mqttConfig.protocol, mqttConfig.host, mqttConfig.port);
        }
        */
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        setHaMQTTOutput(event->topic, event->topic_len, event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            if (event->error_handle->esp_tls_last_esp_err !=0) {
                ESP_LOGE(TAG, "Last error %s: 0x%x", "reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            }
            if (event->error_handle->esp_tls_stack_err !=0)
                ESP_LOGE(TAG, "Last error %s: 0x%x", "reported from tls stack", event->error_handle->esp_tls_stack_err);
            if (event->error_handle->esp_transport_sock_errno !=0)
                ESP_LOGE(TAG, "Last error %s: 0x%x", "captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static esp_mqtt_client_handle_t startMqttClient(void)
{
    ESP_LOGI(TAG, "Starting MQTT Client");
    esp_mqtt_client_config_t config = {
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .broker.address.hostname = mqttConfig.host,
        .broker.address.port = mqttConfig.port,
        .credentials.username = mqttConfig.username,
        .credentials.authentication.password = mqttConfig.password,
        .session.last_will.qos = 0,
        .session.last_will.retain = 1,
    };
    ESP_LOGI(TAG, "Mqtt connecting to %s%s:%lu", mqttConfig.protocol, mqttConfig.host, mqttConfig.port);
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
    if (esp_mqtt_client_start(client) != ESP_OK){
        ESP_LOGE(TAG, "Start MQTT Client failed");
        return NULL;
    }
    ESP_LOGI(TAG, "Start MQTT Client OK");
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqttEventHandler, NULL);
    return client;
}

static esp_err_t stopMqttClient(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "Stoping MQTT Client");
    if (esp_mqtt_client_stop(client) != ESP_OK) {
        ESP_LOGE(TAG, "Stop MQTT Client failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Stop MQTT Client OK");
    return ESP_OK;
}

static void connectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (client == NULL) {
        client = startMqttClient();
    }
}

static void disconnectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (client) {
        if (stopMqttClient(client) == ESP_OK) client = NULL;
    }
}

mqttConfig_t * getMqttConfig()
{
    return &mqttConfig;
}
const char * getJsonMqttConfig()
{
    // payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "protocol", mqttConfig.protocol);
    cJSON_AddStringToObject(root, "host", mqttConfig.host);
    cJSON_AddNumberToObject(root, "port", mqttConfig.port);
    cJSON_AddStringToObject(root, "username", mqttConfig.username); 
    cJSON_AddStringToObject(root, "password", mqttConfig.password); 

    char *jsonConfig = cJSON_Print(root);
    cJSON_Delete(root);
    return jsonConfig;
}

static BaseType_t mqttConfigure()
{    
    ESP_LOGI(TAG, "Setting mqtt config...");
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed setting mqtt config");
        return pdFALSE;
    }
    nvsOpen(NVSGROUP, NVS_READONLY, &pHandle);
    getStrPref(pHandle, "mqtt.protocol", mqttConfig.protocol, sizeof(mqttConfig.protocol));
    getStrPref(pHandle, "mqtt.host", mqttConfig.host, sizeof(mqttConfig.host));
    getUInt32Pref(pHandle, "mqtt.port", &(mqttConfig.port));
    getStrPref(pHandle, "mqtt.username", mqttConfig.username, sizeof(mqttConfig.username));
    getStrPref(pHandle, "mqtt.password", mqttConfig.password, sizeof(mqttConfig.password));
    
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);
    ESP_LOGI(TAG, "Mqtt config complite");

    return pdTRUE;
} 

BaseType_t saveMqttConfig(mqttConfig_t *pMqttConfig)
{
    ESP_LOGI(TAG, "Saving mqtt config...");
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed saving mqtt config");
        return pdFALSE;
    }
    nvsOpen(NVSGROUP, NVS_READWRITE, &pHandle);
    setStrPref(pHandle, "mqtt.protocol", pMqttConfig->protocol);
    setStrPref(pHandle, "mqtt.host", pMqttConfig->host);
    setUInt32Pref(pHandle, "mqtt.port", pMqttConfig->port);
    setStrPref(pHandle, "mqtt.username", pMqttConfig->username);
    setStrPref(pHandle, "mqtt.password", pMqttConfig->password);
    
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);
    ESP_LOGI(TAG, "Mqtt config saved");
    mqttConfigure();
    stopMqttClient(client);
    client = startMqttClient();
    return pdTRUE;
}

BaseType_t setMqttDefaultPreferences() 
{
    ESP_LOGI(TAG, "Setting default preference of mqtt");
    mqttConfig_t *pMqttConfig = malloc(sizeof(mqttConfig_t));
 
    strlcpy(mqttConfig.protocol, "mqtt://", sizeof(mqttConfig.protocol));
    strlcpy(mqttConfig.host, "mqtt.local", sizeof(mqttConfig.host));
    mqttConfig.port = 1883;
    strlcpy(mqttConfig.username, "mqtt", sizeof(mqttConfig.username));
    strlcpy(mqttConfig.password, "mqtt", sizeof(mqttConfig.password));

    saveMqttConfig(pMqttConfig);
    free(pMqttConfig);
    return pdTRUE; 
}

void mqttClientInit(void)
{

    static StaticSemaphore_t xSemaphoreBuffer;
    xMutex = xSemaphoreCreateMutexStatic(&xSemaphoreBuffer);

    if(nvsOpen(NVSGROUP, NVS_READONLY, &pHandle) != pdTRUE ) {
        ESP_LOGW(TAG, "Namespace 'mqtt' notfound");
        setMqttDefaultPreferences();
    }
    else {
        nvs_close(pHandle);
        mqttConfigure();
    }

    static StaticEventGroup_t xEventGroupBuffer;
    xEventGroup = xEventGroupCreateStatic(&xEventGroupBuffer);

    static StaticTask_t xTaskBuffer;
    static StackType_t xStack[STACK_SIZE];
    TaskHandle_t xHandle = xTaskCreateStatic(audiomatrixEventTask, "audiomatrixEventTask", STACK_SIZE, NULL, 5, xStack, &xTaskBuffer);
    if (xHandle == NULL){
        ESP_LOGE(TAG, "Task \"audiomatrixEventTask\" not created");
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connectHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnectHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_PORT_CHANGED, &audiomatrixEventHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_CONFIG_CHANGED, &audiomatrixEventHandler, NULL));

    ESP_LOGI(TAG, "MQTT init finished.");
}
