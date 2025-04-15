#pragma once
#ifndef __HOME_JSON_H__
#define __HOME_JSON_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

const char * JSON_Message(const char *mes );
void jsonStrValue(cJSON *json, char *out, size_t outSize, char *param, char *def);
void jsonUInt8Value(cJSON *json, uint8_t *out, char *param, int def);
void jsonUInt16Value(cJSON *json, uint16_t *out, char *param, int def);
void jsonUInt32Value(cJSON *json, uint32_t *out, char *param, int def);

#ifdef __cplusplus
}
#endif

#endif //__HOME_JSON_H__
