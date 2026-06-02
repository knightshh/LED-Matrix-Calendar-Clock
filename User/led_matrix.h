#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include "stm32f10x.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

void LED_Matrix_Init(void);
void LED_Matrix_SetPixel(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
void LED_Matrix_Latch(void);
void LED_Matrix_SetRow(uint8_t row);

#endif
