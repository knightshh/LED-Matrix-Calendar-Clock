#ifndef DELAY_H
#define DELAY_H

#include "stm32f10x.h"

/* ms 级延时 (busy-loop) */
void Delay(__IO uint32_t nTime);

/* us 级延时, 基于 SysTick LOAD/VAL 寄存器, 精度 ~1 cycle (≈14ns @72MHz)
 * 不依赖中断, 不被编译器优化掉, 适合驱动 DS1302 等慢速串行总线。 */
void Delay_us(uint32_t nus);

#endif
