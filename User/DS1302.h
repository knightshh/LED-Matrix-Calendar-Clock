#ifndef __DS1302_H
#define __DS1302_H

#include "stm32f10x.h"

/* ============================================================
 *  DS1302 引脚:  CLK -> PA8   DAT -> PA9   RST(CE) -> PA10
 * ============================================================ */
#define DS1302_GPIO_PORT     GPIOA
#define DS1302_GPIO_RCC      RCC_APB2Periph_GPIOA

#define DS1302_SCLK_PIN      GPIO_Pin_8
#define DS1302_IO_PIN        GPIO_Pin_9
#define DS1302_CE_PIN        GPIO_Pin_10

/* ------------------------------------------------------------
 *  PA9 在 GPIOA->CRH 中占 [7:4] 位
 *  - 输出态: MODE=11(50MHz), CNF=00(推挽)        -> CRH[7:4] = 0x3
 *  - 输入态: MODE=00(输入)  CNF=10(带上下拉)     -> CRH[7:4] = 0x8
 *    并且要把 ODR 对应位写 1, 表示 "上拉"
 *
 *  用上拉而不是浮空, 是因为很多 DS1302 模块没装外部上拉,
 *  浮空时主机会读到不确定电平, 导致读出全 0xFF / 全 0x00。
 * ------------------------------------------------------------ */
#define DS1302_IO_AS_OUTPUT()                                          \
    do {                                                               \
        GPIOA->CRH = (GPIOA->CRH & ~(0xFul << 4)) | (0x3ul << 4);      \
    } while (0)

#define DS1302_IO_AS_INPUT_PU()                                        \
    do {                                                               \
        GPIOA->BSRR = DS1302_IO_PIN;          /* 选择上拉            */\
        GPIOA->CRH  = (GPIOA->CRH & ~(0xFul << 4)) | (0x8ul << 4);     \
    } while (0)

#define CE_L     (GPIOA->BRR  = DS1302_CE_PIN)
#define CE_H     (GPIOA->BSRR = DS1302_CE_PIN)
#define SCLK_L   (GPIOA->BRR  = DS1302_SCLK_PIN)
#define SCLK_H   (GPIOA->BSRR = DS1302_SCLK_PIN)
#define DATA_L   (GPIOA->BRR  = DS1302_IO_PIN)
#define DATA_H   (GPIOA->BSRR = DS1302_IO_PIN)
#define DATA_READ() ((GPIOA->IDR & DS1302_IO_PIN) ? 1u : 0u)

/* DS1302 寄存器(写地址) */
#define DS1302_REG_SEC       0x80
#define DS1302_REG_MIN       0x82
#define DS1302_REG_HOUR      0x84
#define DS1302_REG_DATE      0x86
#define DS1302_REG_MONTH     0x88
#define DS1302_REG_DAY       0x8A
#define DS1302_REG_YEAR      0x8C
#define DS1302_REG_WP        0x8E
#define DS1302_REG_TC        0x90
#define DS1302_REG_RAM0      0xC0

struct TIMEData
{
    u16 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
    u8  week;
};

extern struct TIMEData TimeData;

void ds1302_gpio_init(void);
void ds1302_write_onebyte(u8 data);
void ds1302_wirte_rig(u8 address, u8 data);
u8   ds1302_read_rig(u8 address);

void ds1032_init(void);
void ds1302_set_time(u16 year, u8 month, u8 day,
                     u8 hour, u8 minute, u8 second, u8 week);
void ds1032_read_time(void);
void ds1032_read_realTime(void);

/* RAM 自检 + 时钟寄存器自检, 任一通过即认为通信正常。
 *  返回:  bit0=RAM 测试通过, bit1=秒寄存器 写读测试通过
 *  != 0 视为通信 OK。 */
u8 ds1302_self_test(void);

/* --- DS1302 内部 RAM 读写 ---
 *  index: 0..30, 共 31 字节非易失 RAM, 与时间寄存器共用同一电池
 *  / 超级电容供电, 适合存放 "上次同步指纹" 之类的小标记。
 *  写函数自动处理写保护位。 */
void ds1302_ram_write(u8 index, u8 data);
u8   ds1302_ram_read(u8 index);

#endif
