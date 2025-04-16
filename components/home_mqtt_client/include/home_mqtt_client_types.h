#pragma once
#ifndef __HOME_MQTT_CLIENT_TYPES_H__
#define __HOME_MQTT_CLIENT_TYPES_H__

#include <stdint.h>
#include "home_mqtt_client_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char protocol[16];
    char host[16];
    uint32_t port;
    char username[16]; 
    char password[16]; 
} mqttConfig_t;

typedef enum {
    HOME_MQTT_DISCONNECTED = 0,
    HOME_MQTT_CONNECTED,
    HOME_MQTT_CONNECTING
} mqttState_t;

#ifdef __cplusplus
}
#endif

#endif //__HOME_MQTT_CLIENT_TYPES_H__