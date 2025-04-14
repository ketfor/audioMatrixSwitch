#pragma once
#ifndef __HOME_WIFI_TYPES_H__
#define __HOME_WIFI_TYPES_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t type;
    char ip[16];
    char ssid[16]; 
    char password[16]; 
} wifiConfig_t;

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_TYPES_H__