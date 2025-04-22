#include <string.h>
#include "esp_log.h"
#include "home_json.h"

//static const char *TAG = "home_json";

const char * JSON_Message(const char *mes )
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", mes);
    const char *jmes = cJSON_Print(root);
    cJSON_Delete(root);
    return jmes;
}

void jsonStrValue(cJSON *json, char *out, size_t outSize, char *param, char *def)
{
    if (cJSON_HasObjectItem(json, param)) {
        strlcpy(out, cJSON_GetObjectItem(json, param)->valuestring, outSize);
    }
    else {
        strlcpy(out, def, outSize);
    }
}

void jsonUInt8Value(cJSON *json, uint8_t *out, char *param, int def)
{
    if (cJSON_HasObjectItem(json, param)) {
        *out = cJSON_GetObjectItem(json, param)->valueint;
    }
    else {
        *out = def;
    }
}

void jsonUInt16Value(cJSON *json, uint16_t *out, char *param, int def)
{
    if (cJSON_HasObjectItem(json, param)) {
        *out = cJSON_GetObjectItem(json, param)->valueint;
    }
    else {
        *out = def;
    }
}

void jsonUInt32Value(cJSON *json, uint32_t *out, char *param, int def)
{
    if (cJSON_HasObjectItem(json, param)) {
        *out = cJSON_GetObjectItem(json, param)->valueint;
    }
    else {
        *out = def;
    }
}
