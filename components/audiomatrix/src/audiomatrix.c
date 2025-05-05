#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "home_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "home_json.h"
#include "nvs_preferences.h"
#include "events_types.h"
#include "home_ota.h"
#include "audiomatrix.h"
#include "matrix_relay.h" //74HC595
#include "matrix_lcd.h" //
#include "onboardled.h"

static const char *TAG = "audiomatrix";

#define NVSGROUP "device"
#define ONAME "out%d"
#define INAME "in%d"
#define STATE_TEMPLATE "{{ value_json.state }}"
#define OUTPUT_STATE_TEMPLATE "{{ value_json.out%d }}"
#define MUTEX_TAKE_TICK_PERIOD 1000 / portTICK_PERIOD_MS
ESP_EVENT_DEFINE_BASE(AUDIOMATRIX_EVENT);

static SemaphoreHandle_t xMutex;

static device_t device;
static nvs_handle_t pHandle = 0;

static const char *outputClass[3] = {"disable", "switch", "select"};

static void toSnakeCase(char *dstStr, const char *srcStr, size_t dstStrSize){
    size_t i = 0;
    while (i < dstStrSize - 1 && srcStr[i] != 0) {
        if (srcStr[i] != ' ') dstStr[i] = tolower(srcStr[i]);
        else dstStr[i] = '_';
        i++;
    }
    dstStr[i] = 0;
}

static void getConfigurationUrl(char *confUrl, size_t sizeConfUrl) 
{
    if (strlen(device.configurationUrl) > 0)
        strlcpy(confUrl, device.configurationUrl, sizeConfUrl);
    else {
        char iPv4Str[16];
        getIPv4Str(iPv4Str);
        snprintf(confUrl, sizeConfUrl, "http://%s", iPv4Str);
    }
}

device_t * getDevice()
{
    return &device; 
}

static BaseType_t getDeviceId(char *deviceId){
    uint8_t mac[6];

    BaseType_t ret = getMAC(mac);
    if (ret == pdTRUE) 
        sprintf(deviceId, "0x%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ret;
}

void sendOutputToDispaly()
{
    for (uint8_t num = 0; num < OUT_PORTS; num++) {
        char line[9];
        sprintf(line, "%s:%s ", device.outputs[num].shortName, device.inputs[device.outputs[num].inputPort].shortName);
        lcdSetCursor((num%2)*8, num/2);
        lcdWriteStr(line);
    }
}
static void sendOutputToMatrix()
{
    uint16_t shift = 0;
    uint8_t nums[] = {1,0,3,2};

    for (uint8_t i = 0; i < sizeof(nums); i++) {
        uint8_t num = nums[i];
        shift <<= 4;
        switch (device.outputs[num].inputPort) {
            case 0: 
                if (num == 0) shift |= 0b0000; else shift |= 0b0101;
                break;
            case 1: 
                if (num == 0) shift |= 0b0101; else shift |= 0b0000;
                break;
            case 2: 
                shift |= 0b1111;
                break;
            default:
            shift |= 0b0000;   
        }
    }
    shift = ~shift;
    sendToRelay(&shift, 1);
}

static void inputConfigure(uint8_t num)
{
    char key[16];
    input_t *input = &(device.inputs[num]);
    input->num = num;
    snprintf(key, sizeof(key), "in%d.name", (int)num + 1);
    getStrPref(pHandle, key, input->name, sizeof(input->name));
    snprintf(key, sizeof(key), "in%d.sh_name", (int)num + 1);
    getStrPref(pHandle, key, input->shortName, sizeof(input->shortName));
    snprintf(key, sizeof(key), "in%d.ln_name", (int)num + 1);
    getStrPref(pHandle, key, input->longName, sizeof(input->longName));
    toSnakeCase(input->formatedName, input->name, sizeof(input->formatedName));
}

static void outputConfigure(uint8_t num)
{
    char key[16];
    output_t *output = &(device.outputs[num]);
    output->num = num;
    // class
    snprintf(key, sizeof(key), "out%d.class", (int)num + 1);
    getUInt8Pref(pHandle, key, &(output->class));
    // name
    snprintf(key, sizeof(key), "out%d.name", (int)num + 1);
    getStrPref(pHandle, key, output->name, sizeof(output->name));
    snprintf(key, sizeof(key), "out%d.sh_name", (int)num + 1);
    getStrPref(pHandle, key, output->shortName, sizeof(output->shortName));
    snprintf(key, sizeof(key), "out%d.ln_name", (int)num + 1);
    getStrPref(pHandle, key, output->longName, sizeof(output->longName));
    toSnakeCase(output->formatedName, output->name, sizeof(output->formatedName));
    // objectId
    strlcpy(output->objectId, device.formatedName, sizeof(output->objectId)); 
    strlcat(output->objectId, "_", sizeof(output->objectId));
    strlcat(output->objectId, output->formatedName, sizeof(output->objectId)); // "sublightkitchen_do_not_disturb"
    // uniqueId
    strlcpy(output->uniqueId, device.identifier, sizeof(output->uniqueId)); 
    strlcat(output->uniqueId, "_", sizeof(output->uniqueId));
    strlcat(output->uniqueId, outputClass[output->class], sizeof(output->uniqueId));
    strlcat(output->uniqueId, "_", sizeof(output->uniqueId));
    strlcat(output->uniqueId, output->formatedName, sizeof(output->uniqueId)); // "0xa4c138fe6784e893_switch_do_not_disturb_z2mone"
    // commandTopic
    snprintf(output->commandTopic, sizeof(output->commandTopic), 
        "%s/set/out%d", device.stateTopic, (int)num + 1);
    // Num input
    snprintf(key, sizeof(key), "out%d.input", (int)num + 1);
    getUInt8Pref(pHandle, key, &(output->inputPort));
}

static BaseType_t deviceConfigure()
{    
    ESP_LOGI(TAG, "Setting device config...");
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed device config");
        return pdFALSE;
    }
    nvsOpen(NVSGROUP, NVS_READONLY, &pHandle);
    getStrPref(pHandle, "dev.identifier", device.identifier, sizeof(device.identifier));
    
    getStrPref(pHandle, "dev.name", device.name, sizeof(device.name));
    toSnakeCase(device.formatedName, device.name, sizeof(device.formatedName));
    //
    strlcpy(device.manufacturer, CONFIG_AM_DEVICE_MANUFACTURER, sizeof(device.manufacturer));
    strlcpy(device.model, CONFIG_AM_DEVICE_MODEL, sizeof(device.model));
    strlcpy(device.modelId, CONFIG_AM_DEVICE_MODEL_ID, sizeof(device.modelId));
    strlcpy(device.hwVersion, CONFIG_AM_DEVICE_HW, sizeof(device.hwVersion));
    strlcpy(device.swVersion, getCurrentRelease(), sizeof(device.swVersion));
    //
    getStrPref(pHandle, "dev.conf_url", device.configurationUrl, sizeof(device.configurationUrl));
    //
    getStrPref(pHandle, "dev.state_topic", device.stateTopic, sizeof(device.stateTopic));
    getStrPref(pHandle, "dev.hass_topic", device.hassTopic, sizeof(device.hassTopic));
  
    for(uint8_t num = 0; num < IN_PORTS; num++){
        inputConfigure(num);
    }

    for(uint8_t num = 0; num < OUT_PORTS; num++){
        outputConfigure(num);
    }
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);

    sendOutputToMatrix();
    sendOutputToDispaly();
    ESP_LOGI(TAG, "Device config complite");

    ESP_LOGI(TAG, "Posting event \"%s\" #%d:device config changed...", AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_CONFIG_CHANGED);
    esp_err_t err = esp_event_post(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_CONFIG_CHANGED, &device, sizeof(device), portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event to \"%s\" #%d: %d (%s)", AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_PORT_CHANGED, err, esp_err_to_name(err));
    };
    return pdTRUE;
}

BaseType_t saveConfig(device_t *pdevice)
{
    ESP_LOGI(TAG, "Saving device config...");
    char key[16];
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed save device config");
        return pdFALSE;
    }
    if(nvsOpen(NVSGROUP, NVS_READWRITE, &pHandle) != pdTRUE)
        return pdFALSE;
    //SaveConfig
    if (strlen(pdevice->identifier) > 0)
        setStrPref(pHandle, "dev.identifier", pdevice->identifier);
    setStrPref(pHandle, "dev.name", pdevice->name);
    setStrPref(pHandle, "dev.conf_url", pdevice->configurationUrl);
    setStrPref(pHandle, "dev.state_topic", pdevice->stateTopic);
    setStrPref(pHandle, "dev.hass_topic", pdevice->hassTopic);
    // Inputs
    for(uint8_t num = 0; num < IN_PORTS; num++){
        input_t *input = &(pdevice->inputs[num]);
        snprintf(key, sizeof(key), "in%d.name", (int)num + 1);
        setStrPref(pHandle, key, input->name);
        snprintf(key, sizeof(key), "in%d.sh_name", (int)num + 1);
        setStrPref(pHandle, key, input->shortName);
        snprintf(key, sizeof(key), "in%d.ln_name", (int)num + 1);
        setStrPref(pHandle, key, input->longName);
    }
    // Outputs
    for(uint8_t num = 0; num < OUT_PORTS; num++){
        output_t *output = &(pdevice->outputs[num]);
        // Class
        snprintf(key, sizeof(key), "out%d.class", (int)num + 1);
        setUInt8Pref(pHandle, key, output->class);
        // Name
        snprintf(key, sizeof(key), "out%d.name", (int)num + 1);
        setStrPref(pHandle, key, output->name);
        snprintf(key, sizeof(key), "out%d.sh_name", (int)num + 1);
        setStrPref(pHandle, key, output->shortName);
        snprintf(key, sizeof(key), "out%d.ln_name", (int)num + 1);
        setStrPref(pHandle, key, output->longName);
        // num input
        snprintf(key, sizeof(key), "out%d.input", (int)num + 1);
        setUInt8Pref(pHandle, key, output->inputPort); 

    }
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);

    return deviceConfigure();
}

static BaseType_t setPort(uint8_t numOutput, uint8_t numInput) 
{
    ESP_LOGI(TAG, "Setting input port %d to the out port %d ...", numInput, numOutput);
    output_t *output = &(device.outputs[numOutput]);
    output->inputPort = numInput;
    sendOutputToMatrix();
    sendOutputToDispaly();
    esp_err_t err = esp_event_post(AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_PORT_CHANGED, &output, sizeof(output), portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event to \"%s\" #%d: %d (%s)", AUDIOMATRIX_EVENT, AUDIOMATRIX_EVENT_PORT_CHANGED, err, esp_err_to_name(err));
    };
    
    ESP_LOGI(TAG, "The input port number %d is directed to the output port number %d", numInput, numOutput);
    return pdTRUE;
}

BaseType_t savePort(uint8_t numOutput, uint8_t numInput) 
{
    ESP_LOGI(TAG, "Saving input port %d to the out port %d ...", numInput, numOutput);
    if(xSemaphoreTake( xMutex, MUTEX_TAKE_TICK_PERIOD ) != pdTRUE) {
        ESP_LOGW(TAG, "Failed save input port %d to the out port %d ...", numInput, numOutput);
        return pdFALSE;
    }

    if(nvsOpen(NVSGROUP, NVS_READWRITE, &pHandle) != pdTRUE)
        return pdFALSE;
    char key[16];
    snprintf(key, sizeof(key), "out%d.input", (int)numOutput + 1);
    setUInt8Pref(pHandle, key, numInput);
    nvs_close(pHandle);
    xSemaphoreGive(xMutex);
    setPort(numOutput, numInput);
    return pdTRUE;
}

BaseType_t setDefaultPreferences() 
{
    ESP_LOGI(TAG, "Setting default preference of device");
    device_t *pdevice = malloc(sizeof(device_t));
 
    getDeviceId(pdevice->identifier);
    strlcpy(pdevice->name, CONFIG_AM_DEVICE_NAME, sizeof(pdevice->name));
    strlcpy(pdevice->stateTopic, CONFIG_AM_MQTT_DEVICE_TOPIC, sizeof(pdevice->stateTopic));
    strlcpy(pdevice->hassTopic, CONFIG_AM_MQTT_HA_TOPIC, sizeof(pdevice->hassTopic));

    // Inputs
    for(uint8_t num = 0; num < IN_PORTS; num++){
        input_t *input = &(pdevice->inputs[num]);
        snprintf(input->name, sizeof(input->name), "In%d", (int)num + 1);
        snprintf(input->shortName, sizeof(input->shortName), "In%d", (int)num + 1);
        snprintf(input->longName, sizeof(input->longName), "In%d", (int)num + 1);
    }
    // Outputs
    for(uint8_t num = 0; num < OUT_PORTS; num++){
        output_t *output = &(pdevice->outputs[num]);
        // Class 
        switch (num) {
            case 4: 
                output->class = CLASS_DISABLE;
                break;
            case 5:
                output->class = CLASS_SELECT;
                break;
            default:
                output->class = CLASS_SWITCH;
        }
        //Name
        snprintf(output->name, sizeof(output->name), "Out%d", (int)num + 1);
        snprintf(output->shortName, sizeof(output->shortName), "Ou%d", (int)num + 1);
        snprintf(output->longName, sizeof(output->longName), "Out%d", (int)num + 1);
        //InputPort
        if (num == 0) output->inputPort = 1;
        else output->inputPort = 0;
    }
    saveConfig(pdevice);
    free(pdevice);
    return pdTRUE; 
}

const char * getDeviceConfig()
{
    // payload
    cJSON *root = cJSON_CreateObject();
    cJSON *json_device, *json_inputs, *json_input, *json_outputs, *json_output;
    
    // Device
    json_device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(json_device, "identifier", device.identifier);
    cJSON_AddStringToObject(json_device, "name", device.name);
    cJSON_AddStringToObject(json_device, "manufacturer", device.manufacturer);
    cJSON_AddStringToObject(json_device, "model", device.model);
    cJSON_AddStringToObject(json_device, "model_id", device.modelId);
    cJSON_AddStringToObject(json_device, "hw_version", device.hwVersion);
    cJSON_AddStringToObject(json_device, "sw_version", device.swVersion);
    char configurationUrl[64];
    getConfigurationUrl(configurationUrl, sizeof(configurationUrl));
    cJSON_AddStringToObject(json_device, "conf_url", configurationUrl);
    cJSON_AddStringToObject(json_device, "state_topic", device.stateTopic);
    cJSON_AddStringToObject(json_device, "hass_topic", device.hassTopic);

    //inputs
    json_inputs = cJSON_AddArrayToObject(root, "inputs");
    for (uint8_t num = 0; num < IN_PORTS; num++) {
        input_t *input = &(device.inputs[num]);
        cJSON_AddItemToArray(json_inputs, json_input = cJSON_CreateObject());
        cJSON_AddNumberToObject(json_input, "id", input->num);
        cJSON_AddStringToObject(json_input, "name", input->name);
        cJSON_AddStringToObject(json_input, "short_name", input->shortName);
        cJSON_AddStringToObject(json_input, "long_name", input->longName);
    }    

    // output
    json_outputs = cJSON_AddArrayToObject(root, "outputs");
    for (uint8_t num = 0; num < OUT_PORTS; num++) {
        output_t *output = &(device.outputs[num]);
        cJSON_AddItemToArray(json_outputs, json_output = cJSON_CreateObject());
        cJSON_AddNumberToObject(json_output, "class", output->class);
        cJSON_AddNumberToObject(json_output, "id", output->num);
        cJSON_AddStringToObject(json_output, "name", output->name);
        cJSON_AddStringToObject(json_output, "short_name", output->shortName);
        cJSON_AddStringToObject(json_output, "long_name", output->longName);
        cJSON_AddNumberToObject(json_output, "input", output->inputPort);
    }    

    char *jsonConfig = cJSON_Print(root);
    cJSON_Delete(root);
    return jsonConfig;
}

const char * getDeviceState()
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", "online");
    for(uint8_t num = 0; num < OUT_PORTS; num++){
        output_t *output = &(device.outputs[num]);
        char name[6];
        sprintf(name, ONAME, (int)num + 1);
        cJSON_AddNumberToObject(root, name, output->inputPort);
    }
    char *jsonState = cJSON_Print(root);
    cJSON_Delete(root);
    return jsonState;
}

BaseType_t getHaMQTTStateTopic(char *topic, size_t topicSize)
{
    strlcpy(topic, device.stateTopic, topicSize);
    return pdTRUE;
}

BaseType_t getHaMQTTOutputConfig(uint8_t num, uint8_t class, char *topic, size_t topicSize, char *payload, size_t payloadSize)
{
    output_t *output = &(device.outputs[num]);

    // topic
    strlcpy(topic, device.hassTopic, topicSize);
    strlcat(topic, "/", topicSize);
    strlcat(topic, outputClass[class], topicSize);
    strlcat(topic, "/", topicSize);
    strlcat(topic, device.identifier, topicSize);
    strlcat(topic, "/", topicSize);
    strlcat(topic, outputClass[class], topicSize);
    strlcat(topic, "_", topicSize);
    strlcat(topic, output->formatedName, topicSize);
    strlcat(topic, "/config", topicSize);
    
    if (output->class != class){
        strlcpy(payload, "", payloadSize);
        return pdTRUE;
    }
    //else strlcat(topic, "/config", topicSize);

    // payload
    cJSON *root = cJSON_CreateObject();
    cJSON *json_availabilities, *json_availability, *json_device, *json_device_identifiers, *json_options;
    json_availabilities = cJSON_AddArrayToObject(root, "availability");
    cJSON_AddItemToArray(json_availabilities, json_availability = cJSON_CreateObject());
    cJSON_AddStringToObject(json_availability, "topic", device.stateTopic);
    cJSON_AddStringToObject(json_availability, "value_template", STATE_TEMPLATE);
    // Device
    json_device = cJSON_AddObjectToObject(root, "device");
    json_device_identifiers = cJSON_AddArrayToObject(json_device, "identifiers");
    cJSON_AddStringToObject(json_device_identifiers, "", device.identifier);
    if (num == 0) {
        cJSON_AddStringToObject(json_device, "name", device.name);
        cJSON_AddStringToObject(json_device, "manufacturer", device.manufacturer);
        cJSON_AddStringToObject(json_device, "model", device.model);
        cJSON_AddStringToObject(json_device, "model_id", device.modelId);
        cJSON_AddStringToObject(json_device, "hw_version", device.hwVersion);
        cJSON_AddStringToObject(json_device, "sw_version", device.swVersion);
        char configurationUrl[64];
        getConfigurationUrl(configurationUrl, sizeof(configurationUrl));
        cJSON_AddStringToObject(json_device, "configuration_url", configurationUrl);
    }
    // Object
    cJSON_AddStringToObject(root, "name", output->name);
    cJSON_AddStringToObject(root, "object_id", output->objectId);
    cJSON_AddStringToObject(root, "unique_id", output->uniqueId);
    cJSON_AddStringToObject(root, "command_topic", output->commandTopic);
    cJSON_AddStringToObject(root, "state_topic", device.stateTopic);
    if (output->class == CLASS_SWITCH) {
        cJSON_AddNumberToObject(root, "payload_off", 1);
        cJSON_AddNumberToObject(root, "payload_on", 0);
        char stateTemplate[24];
        snprintf(stateTemplate, sizeof(stateTemplate), OUTPUT_STATE_TEMPLATE, (int)num + 1);
        cJSON_AddStringToObject(root, "value_template", stateTemplate);
    }
    if (output->class == CLASS_SELECT) {
        json_options = cJSON_AddArrayToObject(root, "options");
        for (uint8_t inum = 0; inum < IN_PORTS; inum++) {
            cJSON_AddStringToObject(json_options, "", device.inputs[inum].longName);
        }
        // value_template
        char stateTemplate[100 + (sizeof(((input_t*)0)->longName) + 16) * IN_PORTS];
        strlcpy(stateTemplate, "{% set mapper = {", sizeof(stateTemplate));
        for (uint8_t inum = 0; inum < IN_PORTS; inum++) {
            char option[sizeof(((input_t*)0)->longName) + 16];
            snprintf(option, sizeof(option), "%d:'%s',", inum, device.inputs[inum].longName);
            strlcat(stateTemplate, option, sizeof(stateTemplate));
        }
        strlcat(stateTemplate, "} %}", sizeof(stateTemplate));
        char setx[34];
        snprintf(setx, sizeof(setx), "{%% set x = value_json.out%d %%}", (int)num + 1);
        strlcat(stateTemplate, setx, sizeof(stateTemplate));
        strlcat(stateTemplate, "{{ mapper[x] if x in mapper else 'Failed' }}", sizeof(stateTemplate));
        cJSON_AddStringToObject(root, "value_template", stateTemplate);
        // command_tempalate
        char commandTemplate[100 + (sizeof(((input_t*)0)->longName) + 16) * IN_PORTS];
        strlcpy(commandTemplate, "{% set mapper = {", sizeof(commandTemplate));
        for (uint8_t inum = 0; inum < IN_PORTS; inum++) {
            char option[sizeof(((input_t*)0)->longName) + 16];
            snprintf(option, sizeof(option), "'%s':%d,", device.inputs[inum].longName, inum);
            strlcat(commandTemplate, option, sizeof(commandTemplate));
        }
        strlcat(commandTemplate, "} %}", sizeof(commandTemplate));
        //strlcat(commandTemplate, "{% set x = {{option}} %}", sizeof(commandTemplate));
        strlcat(commandTemplate, "{{ mapper[value] if value in mapper else 'Failed' }}", sizeof(commandTemplate));
        //strlcat(commandTemplate, "{{ value }}", sizeof(commandTemplate));
        cJSON_AddStringToObject(root, "command_template", commandTemplate);
    }
    
    BaseType_t result = pdTRUE;
    char *jsonPayload = cJSON_Print(root);
    size_t jsonPayloadSize = strlcpy(payload, jsonPayload, payloadSize);
    if (payloadSize < jsonPayloadSize) {
        ESP_LOGE(TAG, "JSON size (%d) is larger then the payload size(%d)", jsonPayloadSize, payloadSize);
        result = pdFALSE;
    }
    cJSON_free(jsonPayload);
    cJSON_Delete(root);
    return result;
}
/// @brief 
/// @param topic 
/// @param topicSize 
/// @param payload 
/// @param payloadSize 
/// @return pdTRUE if OK else pdFALSE
BaseType_t getHaMQTTDeviceState(char *topic, size_t topicSize, char *payload, size_t payloadSize)
{
    strlcpy(topic, device.stateTopic, topicSize);

    const char * jsonPayload = getDeviceState();
    BaseType_t result = pdTRUE;
    size_t jsonPayloadSize = strlcpy(payload, jsonPayload, payloadSize);
    if (payloadSize < jsonPayloadSize) {
        ESP_LOGE(TAG, "JSON size (%d) is larger then the payload size (%d)", jsonPayloadSize, payloadSize);
        result = pdFALSE;
    }
    free((void*)jsonPayload);
    return result;
}

/// @brief Set the outgoing port to match the incoming port according to MQTT data
/// @param topic 
/// @param topicSize 
/// @param payload 
/// @param payloadSize 
/// @return pdTRUE if OK else pdFALSE
BaseType_t setHaMQTTOutput(char *topic, size_t topicSize, char *payload, size_t payloadSize)
{
    int8_t numOutput = -1;
    for(uint8_t num = 0; num < OUT_PORTS; num++){
        output_t *output = &(device.outputs[num]);
        if (strncmp(output->commandTopic, topic, topicSize) == 0){
            numOutput = num;
        }
    }
    if (numOutput >= 0 && payloadSize == 1 && payload[0] >= 48 && payload[0] < 48 + IN_PORTS) {       
        savePort(numOutput, payload[0] - 48);
        return pdTRUE;
    }
    return pdFALSE;
}

/// @brief Audiomatrix initialization
void audiomatrixInit(void)
{
    /*
    if(nvsOpen(NVSGROUP, NVS_READWRITE, &pHandle) == pdTRUE) {
        ESP_LOGI(TAG, "Erase all preferences at namespace 'device'");
        nvs_erase_all(pHandle);
        nvs_close(pHandle);
    }
    */
    
    onboardledInit();
    matrixRelayInit();

    static StaticSemaphore_t xSemaphoreBuffer;
    xMutex = xSemaphoreCreateMutexStatic(&xSemaphoreBuffer);

    if(nvsOpen(NVSGROUP, NVS_READONLY, &pHandle) != pdTRUE ) {
        ESP_LOGW(TAG, "Namespace 'device' notfound");
        setDefaultPreferences();
    }
    else if (getStrPref(pHandle, "dev.identifier", device.identifier, sizeof(device.identifier)) != pdTRUE) {
        ESP_LOGW(TAG, "Preferences 'dev.identifier' not found");
        nvs_close(pHandle);
        setDefaultPreferences();
    }
    else {
        nvs_close(pHandle);
        deviceConfigure();
    }

    led_strip_collor_t color = {
        .red = 0,
        .green = 16,
        .blue = 0
    };
    esp_err_t err = esp_event_post(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &color, sizeof(color), portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event to \"%s\" #%d: %d (%s)", ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, err, esp_err_to_name(err));
    };
    
    ESP_LOGI(TAG, "Audiomatrix init finished.");
}
