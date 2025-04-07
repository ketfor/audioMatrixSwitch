#pragma once
#ifndef __ONBOARDLED_EVENT_TYPES_H__
#define __ONBOARDLED_EVENT_TYPES_H__

#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ONBOARDLED_EVENT_STARTED = 0,
    ONBOARDLED_EVENT_SETCOLOR
} onboardled_event_t;

ESP_EVENT_DECLARE_BASE(ONBOARDLED_EVENT);

#ifdef __cplusplus
}
#endif

#endif // __ONBOARDLED_EVENT_TYPES_H__