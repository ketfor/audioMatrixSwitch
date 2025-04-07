#pragma once
#ifndef __AUDIOMATRIX_H__
#define __AUDIOMATRIX_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audiomatrix_types.h"
#include "audiomatrix_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t getHaMQTTOutputConfig(uint8_t num, char *topic, size_t topicSize, char *payload, size_t payloadSize);
BaseType_t getHaMQTTDeviceState(char *topic, size_t topicSize, char *payload, size_t payloadSize);
BaseType_t getHaMQTTStateTopic(char *topic, size_t topicSize);
BaseType_t setHaMQTTOutput(char *topic, size_t topicSize, char *payload, size_t payloadSize);
void audiomatrixInit(void);

#ifdef __cplusplus
}
#endif

#endif //__AUDIOMATRIX_H__