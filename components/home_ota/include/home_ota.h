#pragma once
#ifndef __HOME_OTA_H__
#define __HOME_OTA_H__

#include "home_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void doFirmwareUpgrade(const char * release);
void updateReleasesInfo();
const char * getReleasesInfo();
void otaInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_OTA_H__