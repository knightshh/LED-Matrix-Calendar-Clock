#include "Delay.h"

void Delay(__IO uint32_t nTime)
{
    nTime *= 12000;
    while(nTime--);
}

/* ------------------------------------------------------------
 *  SysTick 精确 us 延时
 *  - 复用 CMSIS 的 SysTick, 时钟源固定为 HCLK (72MHz)
 *  - 不开启 SysTick 中断, 仅用 COUNTFLAG 轮询
 *  - 每次 reload = nus * 72 - 1, 最大 ~233us, 大延时分批跑
 * ------------------------------------------------------------ */
void Delay_us(uint32_t nus)
{
    /* SysTick 24-bit 计数器, 最大 0x00FFFFFF cycles, @72MHz 约 233015us */
    while (nus)
    {
        uint32_t step = (nus > 1000) ? 1000 : nus;
        SysTick->LOAD = step * 72 - 1;
        SysTick->VAL  = 0;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
        SysTick->CTRL = 0;
        nus -= step;
    }
}
