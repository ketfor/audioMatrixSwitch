#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "home_mqtt_client.h"
#include "events_types.h"
#include "audiomatrix.h"

#define TAG "home_mqtt_client"

static void subscribeState(esp_mqtt_client_handle_t client)
{
    char topic[64];
    getHaMQTTStateTopic(topic, sizeof(topic));
    strlcat(topic, "/set/#", sizeof(topic));
    esp_mqtt_client_subscribe(client, topic, 0);
}

static void publishState(esp_mqtt_client_handle_t client)
{
    char topic[64], payload[1024];
    if(getHaMQTTDeviceState(topic, sizeof(topic), payload, sizeof(payload)) == pdTRUE){
        esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
    }
}

static void publishConfig(esp_mqtt_client_handle_t client)
{
    char topic[64], payload[1024];
    for (uint8_t num = 0; num < OUT_PORTS; num++) {
        if(getHaMQTTOutputConfig(num, topic, sizeof(topic), payload, sizeof(payload)) == pdTRUE){
            esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
        }
    }
    publishState(client);
}

static void audiomatrixEventHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    esp_mqtt_client_handle_t* client = (esp_mqtt_client_handle_t*) arg;
    if (*client == NULL) {
        ESP_LOGW(TAG, "audiomatrixEventHandler: No MQTT client");
        return;
    }

    esp_mqtt_event_handle_t event = event_data;
    switch ((audiomatrix_event_t)event_id) {
        case AUDIOMATRIX_EVENT_PORT_CHANGED:
            publishState(*client);
            break;
        case AUDIOMATRIX_EVENT_CONFIG_CHANGED:
            publishConfig(*client);
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
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        subscribeState(client);
        publishConfig(client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
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
        //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            if (event->error_handle->esp_tls_last_esp_err !=0)
                ESP_LOGE(TAG, "Last error %s: 0x%x", "reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
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
        .broker.address.uri = CONFIG_BROKER_URL,
        .credentials.username = CONFIG_BROKER_USER,
        .credentials.authentication.password = CONFIG_BROKER_PASSWORD,
        .session.last_will.qos = 0,
        .session.last_will.retain = 1,
    };
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
    esp_mqtt_client_handle_t* client = (esp_mqtt_client_handle_t*) arg;
    if (*client == NULL) {
        *client = startMqttClient();
    }
}

static void disconnectHandler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    esp_mqtt_client_handle_t* client = (esp_mqtt_client_handle_t*) arg;
    if (*client) {
        if (stopMqttClient(*client) == ESP_OK) *client = NULL;
    }
}

void mqttClientInit(void)
{
    static esp_mqtt_client_handle_t client = NULL;
    //client = startMqttClient();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connectHandler, &client));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnectHandler, &client));
    ESP_ERROR_CHECK(esp_event_handler_register(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_PORT_CHANGED, &audiomatrixEventHandler, &client));
    ESP_ERROR_CHECK(esp_event_handler_register(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_CONFIG_CHANGED, &audiomatrixEventHandler, &client));
    ESP_LOGI(TAG, "MQTT init finished.");
}
