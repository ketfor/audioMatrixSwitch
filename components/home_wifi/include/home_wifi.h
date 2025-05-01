#pragma once
#ifndef __HOME_WIFI_H__
#define __HOME_WIFI_H__

#include "home_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t getMAC(uint8_t *mac);
BaseType_t getIPv4Str(char * iPv4Str);
wifiConfig_t * getWifiConfig();
const char * getJsonWifiConfig();
BaseType_t saveWifiConfig(wifiConfig_t *pMqttConfig);
BaseType_t updateTime();

void wifiInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_H__