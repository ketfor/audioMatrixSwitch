#pragma once
#ifndef __HOME_WIFI_EVENT_TYPES_H__
#define __HOME_WIFI_EVENT_TYPES_H__

#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOME_WIFI_EVENT_STOP = 0,
    HOME_WIFI_EVENT_START
} home_wifi_event_t;

ESP_EVENT_DECLARE_BASE(HOME_WIFI_EVENT);

#ifdef __cplusplus
}
#endif

#endif // __HOME_WIFI_EVENT_TYPES_H__