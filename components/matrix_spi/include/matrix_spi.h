#pragma once
#ifndef __MATRIX_SPI_H__
#define __MATRIX_SPI_H__

#include "matrix_spi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void SendToMatrix(uint16_t *buf, uint8_t sz);
void matrixSpiInit(void);

#ifdef __cplusplus
}
#endif

#endif // __MATRIX_SPI_H__