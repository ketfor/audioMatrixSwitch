#pragma once
#ifndef __HOME_MQTT_CLIENT_TYPES_H__
#define __HOME_MQTT_CLIENT_TYPES_H__

#include <stdint.h>
#include "home_mqtt_client_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char protocol[8];
    char host[16];
    uint16_t port;
    char username[16]; 
    char password[16]; 
} mqttConfig_t;

#ifdef __cplusplus
}
#endif

#endif //__HOME_MQTT_CLIENT_TYPES_H__