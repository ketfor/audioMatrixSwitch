#pragma once
#ifndef __HOME_WIFI_H__
#define __HOME_WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t getMAC(uint8_t *mac);
BaseType_t getIPv4Str(char * iPv4Str);

void wifiStationInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_H__