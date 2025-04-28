#pragma once
#ifndef __HOME_OTA_H__
#define __HOME_OTA_H__

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t doFirmwareUpgrade();
void otaInit(void);

#ifdef __cplusplus
}
#endif

#endif // __HOME_OTA_H__