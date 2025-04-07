#pragma once
#ifndef __AUDIOMATRIX_EVENT_TYPES_H__
#define __AUDIOMATRIX_EVENT_TYPES_H__

#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIOMATRIX_EVENT_PORT_CHANGED = 0,
    AUDIOMATRIX_EVENT_CONFIG_CHANGED
} audiomatrix_event_t;

ESP_EVENT_DECLARE_BASE(AUDIOMATRIX_EVENT);

#ifdef __cplusplus
}
#endif

#endif // __AUDIOMATRIX_EVENT_TYPES_H__