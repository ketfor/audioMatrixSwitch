#pragma once
#ifndef __MATRIX_SPI_TYPES_H__
#define __MATRIX_SPI_TYPES_H__

#include "matrix_spi_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int shift;
} matrixSpi_t;

#ifdef __cplusplus
}
#endif

#endif // __MATRIX_SPI_TYPES_H__