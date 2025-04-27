#pragma once
#ifndef __MATRIX_RELAY_H__
#define __MATRIX_RELAY_H__

#ifdef __cplusplus
extern "C" {
#endif

void sendToRelay(uint16_t *buf, uint8_t sz);
void matrixRelayInit(void);

#ifdef __cplusplus
}
#endif

#endif // __MATRIX_RELAY_H__