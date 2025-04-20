#pragma once
#ifndef __MATRIX_SPI_EVENT_TYPES_H__
#define __MATRIX_SPI_EVENT_TYPES_H__

#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MATRIXSPI_EVENT_SEND = 0
} matrixSpiEvent_t;

ESP_EVENT_DECLARE_BASE(MATRIXSPI_EVENT);

#ifdef __cplusplus
}
#endif

#endif // __MATRIX_SPI_EVENT_TYPES_H__