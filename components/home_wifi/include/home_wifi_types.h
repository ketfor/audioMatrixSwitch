#pragma once
#ifndef __HOME_WIFI_TYPES_H__
#define __HOME_WIFI_TYPES_H__

#include <stdint.h>
#include "home_wifi_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t mode;
    char ip[16];
    char hostname[16];
    char ssid[32]; 
    char password[64]; 
} wifiConfig_t;

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_TYPES_H__