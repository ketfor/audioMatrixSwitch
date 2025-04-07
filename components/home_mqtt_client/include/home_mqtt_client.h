#pragma once
#ifndef __HOME_MQTT_CLIENT_H__
#define __HOME_MQTT_CLIENT_H__

#include "home_web_server_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqttClientInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_MQTT_CLIENT_H__