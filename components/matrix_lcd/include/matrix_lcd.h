#pragma once
#ifndef __MATRIX_LCD_H__
#define __MATRIX_LCD_H__

#ifdef __cplusplus
extern "C" {
#endif


void lcdSetCursor(uint8_t col, uint8_t row);
void lcdHome(void);
void lcdClearScreen(void);
void lcdWriteChar(char c);
void lcdWriteStr(const char* str); 
void matrixLcdInit(void);

#ifdef __cplusplus
}
#endif

#endif // __MATRIX_LCD_H__