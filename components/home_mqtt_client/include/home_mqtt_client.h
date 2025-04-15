#pragma once
#ifndef __HOME_MQTT_CLIENT_H__
#define __HOME_MQTT_CLIENT_H__

#include "home_mqtt_client_types.h"

#ifdef __cplusplus
extern "C" {
#endif

mqttConfig_t * getMqttConfig();
const char * getJsonMqttConfig();
BaseType_t saveMqttConfig(mqttConfig_t *pMqttConfig);
BaseType_t setMqttDefaultPreferences();
void mqttClientInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_MQTT_CLIENT_H__