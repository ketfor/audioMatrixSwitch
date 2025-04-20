#pragma once
#ifndef __HOME_WIFI_TYPES_H__
#define __HOME_WIFI_TYPES_H__

#include <stdint.h>
#include "esp_netif_ip_addr.h"
#include "home_wifi_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STRIP "%hhu.%hhu.%hhu.%hhu"
#define STR2IPPOINT(strip) &((uint8_t*)(&strip))[0], \
    &((uint8_t*)(&strip))[1], \
    &((uint8_t*)(&strip))[2], \
    &((uint8_t*)(&strip))[3]


#define NVSKEY_WIFI_MODE "wifi.mode"
//#define NVSKEY_WIFI_IP "wifi.ip"
#define NVSKEY_WIFI_IPADDR "wifi.ipaddr"
#define NVSKEY_WIFI_HOSTNAME "wifi.hostname"
#define NVSKEY_WIFI_SSID "wifi.ssid"
#define NVSKEY_WIFI_PASSWORD "wifi.password"

typedef struct {
    uint8_t mode;
    char ip[16];
    char hostname[16];
    char ssid[32]; 
    char password[64]; 
    esp_ip4_addr_t ipaddr;
} wifiConfig_t;

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_TYPES_H__