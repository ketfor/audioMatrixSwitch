#pragma once
#ifndef __NVS_PREFERENCES_H__
#define __NVS_PREFERENCES_H__

#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t nvsOpen(const char* name_group, nvs_open_mode_t open_mode, nvs_handle_t *pHandle);
BaseType_t getStrPref(nvs_handle_t pHandle, const char *key, char *value, size_t valueSize);
BaseType_t setStrPref(nvs_handle_t pHandle, const char *key, char *value);
BaseType_t getUInt8Pref(nvs_handle_t pHandle, const char *key, uint8_t *value);
BaseType_t setUInt8Pref(nvs_handle_t pHandle, const char *key, uint8_t value);
BaseType_t getUInt16Pref(nvs_handle_t pHandle, const char *key, uint16_t *value);
BaseType_t setUInt16Pref(nvs_handle_t pHandle, const char *key, uint16_t value);
BaseType_t getUInt32Pref(nvs_handle_t pHandle, const char *key, uint32_t *value);
BaseType_t setUInt32Pref(nvs_handle_t pHandle, const char *key, uint32_t value);
#ifdef __cplusplus
}
#endif

#endif //__NVS_PREFERENCES_H__
