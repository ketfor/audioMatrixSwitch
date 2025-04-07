#pragma once
#ifndef __ONBOARDLED_TYPES_H__
#define __ONBOARDLED_TYPES_H__

#include "onboardled_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int red;
    int green;
    int blue;
} led_strip_collor_t;

#ifdef __cplusplus
}
#endif

#endif // __ONBOARDLED_TYPES_H__