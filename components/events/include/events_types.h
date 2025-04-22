#pragma once
#ifndef __EVENTS_TYPES_H__
#define __EVENTS_TYPES_H__

#include "esp_event.h"
#include "esp_wifi_types.h"
#include "esp_netif_types.h"
#include "onboardled_types.h"
#include "home_web_server_event_types.h"
#include "home_wifi_event_types.h"
#include "home_mqtt_client_event_types.h"
#include "audiomatrix_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* event_data_t;

typedef void (*callbackHandler_t)(void* callback_handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data);

typedef union eventsDataItem {
    uint32_t val;
    void *ptr;
} eventsDataItem_t;

typedef struct {
    bool data_allocated;
    bool data_set;
    esp_event_base_t event_base;
    int32_t event_id;
    eventsDataItem_t event_data;
} eventsDataQueueItem_t;

esp_err_t eventsCallbackHandlerRegister(esp_event_base_t event_base, int32_t event_id,
    callbackHandler_t callback_handler, void* calback_handler_arg);

esp_err_t eventsCallbackExec(esp_event_base_t event_base, int32_t event_id,
    void* event_data);

BaseType_t eventsDataQueuePost(esp_event_base_t event_base, int32_t event_id,
    void* event_data, size_t event_data_size, TickType_t ticks_to_wait);

BaseType_t eventsDataQueuePostFromISR(esp_event_base_t event_base, int32_t event_id,
    void* event_data, size_t event_data_size, BaseType_t* task_unblocked);
    
BaseType_t eventsDataQueueGet(esp_event_base_t event_base, int32_t event_id,
    event_data_t* event_data, TickType_t ticks_to_wait);

BaseType_t eventsDataQueueGetFromISR(esp_event_base_t event_base, int32_t event_id,
    event_data_t* event_data, BaseType_t *const pxHigherPriorityTaskWoken);
    
#ifdef __cplusplus
}
#endif

#endif // __EVENTS_TYPES_H__
