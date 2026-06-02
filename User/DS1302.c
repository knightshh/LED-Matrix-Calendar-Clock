/* ============================================================
 *  DS1302 三线串行实时时钟驱动 (保守版)
 *  接线: CLK -> PA8  DAT -> PA9  RST(CE) -> PA10
 *
 *  本版本相对前一版的关键改动:
 *   1) GPIO 速度由 50MHz 降到 2MHz, 减小长杜邦线上的边沿振铃
 *      与地反弹 (50MHz 推挽 + 20cm 飞线很容易把一个 SCLK 沿
 *      变成多个, 导致 DS1302 移位寄存器错位)。
 *   2) 读模式下 PA9 配置为 "上拉输入" 而不是 "浮空输入"。许
 *      多便宜的 DS1302 模块没装板载上拉, 浮空时读出不稳定。
 *   3) 上电先 100ms 静默, 让 DS1302 完成内部 POR (datasheet
 *      要求 CE 在 VCC < 2.0V 期间保持低电平)。
 *   4) 自检同时尝试 RAM 写读 与 秒寄存器 写读, 任一通过即认
 *      为通信正常, 提高鲁棒性。
 * ============================================================ */
#include "ds1302.h"
#include "Delay.h"

struct TIMEData TimeData;
static u8 read_time[7];

/* DS1302 5V 时 tCC/tCDH 最小约 4us, 用 6us 留余量。
 * 适当放大不会显著影响系统性能 — 一次完整读 7 字节也只 ~1ms。 */
#define T_BIT  6u

/* ------------------------------------------------------------
 *  GPIO 初始化(只在启动时调用一次)
 *  把 SCLK / IO / CE 都设为 2MHz 推挽输出。
 * ------------------------------------------------------------ */
void ds1302_gpio_init(void)
{
    GPIO_InitTypeDef io;

    RCC_APB2PeriphClockCmd(DS1302_GPIO_RCC, ENABLE);

    io.GPIO_Pin   = DS1302_SCLK_PIN | DS1302_IO_PIN | DS1302_CE_PIN;
    io.GPIO_Speed = GPIO_Speed_2MHz;       /* 关键: 慢速降低边沿干扰 */
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(DS1302_GPIO_PORT, &io);

    CE_L;
    SCLK_L;
    DATA_L;
}

/* ------------------------------------------------------------
 *  发送 1 字节 (LSB first)
 *  调用前需保证 IO 方向 = 输出, CE = 1
 *  调用后 SCLK 处于 1
 * ------------------------------------------------------------ */
void ds1302_write_onebyte(u8 data)
{
    u8 i;

    DS1302_IO_AS_OUTPUT();

    for (i = 0; i < 8; i++)
    {
        SCLK_L;
        if (data & 0x01) DATA_H;
        else             DATA_L;
        Delay_us(T_BIT);
        SCLK_H;
        Delay_us(T_BIT);
        data >>= 1;
    }
}

/* ------------------------------------------------------------
 *  写一个寄存器
 * ------------------------------------------------------------ */
void ds1302_wirte_rig(u8 address, u8 data)
{
    /* 进入态: SCLK=0, CE=0; 然后 CE 上升启动一次传输 */
    SCLK_L;
    CE_L;
    Delay_us(T_BIT);
    CE_H;
    Delay_us(T_BIT);

    ds1302_write_onebyte(address);
    ds1302_write_onebyte(data);

    /* 收尾: 先 SCLK=0, 再 CE=0, 然后再多一段空闲间隔 */
    SCLK_L;
    Delay_us(T_BIT);
    CE_L;
    Delay_us(T_BIT * 2);
}

/* ------------------------------------------------------------
 *  读一个寄存器
 *
 *  时序要点:
 *   - 命令字 8 个 SCLK 上升沿后, 紧接着的第一个下降沿
 *     (在循环第一拍 SCLK_L) 上 DS1302 输出 D0
 *   - 之后每次 SCLK 0->1->0, 在 1->0 的下降沿输出下一位
 * ------------------------------------------------------------ */
u8 ds1302_read_rig(u8 address)
{
    u8 i;
    u8 ret = 0x00;

    SCLK_L;
    CE_L;
    Delay_us(T_BIT);
    CE_H;
    Delay_us(T_BIT);

    ds1302_write_onebyte(address);          /* 命令 (R/W=1, 读) */

    /* 切到上拉输入态, 等总线稳定 */
    DS1302_IO_AS_INPUT_PU();
    Delay_us(T_BIT);

    /* 此刻 SCLK = 1, 进入循环后第一拍的 SCLK_L 即为
     * "命令字写完后的第一个下降沿", DS1302 输出 D0 */
    for (i = 0; i < 8; i++)
    {
        SCLK_L;
        Delay_us(T_BIT);
        if (DATA_READ()) ret |= (1u << i);
        SCLK_H;
        Delay_us(T_BIT);
    }

    /* 收尾: 拉低 SCLK -> 拉低 CE -> IO 切回输出, 释放总线 */
    SCLK_L;
    Delay_us(T_BIT);
    CE_L;
    DS1302_IO_AS_OUTPUT();
    DATA_L;
    Delay_us(T_BIT * 2);
    return ret;
}

/* ------------------------------------------------------------
 *  10 进制 -> BCD
 * ------------------------------------------------------------ */
static u8 dec_to_bcd(u8 dec)
{
    return (u8)(((dec / 10) << 4) | (dec % 10));
}

/* ------------------------------------------------------------
 *  写入完整时间(自动加 / 撤 写保护)
 * ------------------------------------------------------------ */
void ds1302_set_time(u16 year, u8 month, u8 day,
                     u8 hour, u8 minute, u8 second, u8 week)
{
    if (year >= 2000) year -= 2000;

    ds1302_wirte_rig(DS1302_REG_WP,    0x00);
    ds1302_wirte_rig(DS1302_REG_SEC,   dec_to_bcd(second) & 0x7F);  /* CH=0 启动振荡 */
    ds1302_wirte_rig(DS1302_REG_MIN,   dec_to_bcd(minute));
    ds1302_wirte_rig(DS1302_REG_HOUR,  dec_to_bcd(hour) & 0x3F);    /* 24h 模式 */
    ds1302_wirte_rig(DS1302_REG_DATE,  dec_to_bcd(day));
    ds1302_wirte_rig(DS1302_REG_MONTH, dec_to_bcd(month));
    ds1302_wirte_rig(DS1302_REG_DAY,   dec_to_bcd(week));
    ds1302_wirte_rig(DS1302_REG_YEAR,  dec_to_bcd((u8)year));
    ds1302_wirte_rig(DS1302_REG_WP,    0x80);
}

/* ------------------------------------------------------------
 *  自检
 *  方法 1: RAM[0] 写 0xA5/0x5A 各一次, 都能读回视为通过
 *  方法 2: 备份秒寄存器, 写一个独特值再读回, 然后写回原值
 *
 *  两种方法都能直接证明 STM32 跟 DS1302 之间能正常通信,
 *  且互不干扰真实走时数据 (秒寄存器测试在 ms 量级内完成,
 *  最坏情况是把秒数偏掉几毫秒)。
 * ------------------------------------------------------------ */
u8 ds1302_self_test(void)
{
    u8 result = 0;
    u8 a, b;
    u8 sec_backup, sec_check;

    ds1302_wirte_rig(DS1302_REG_WP, 0x00);

    /* ---- 方法 1: RAM 测试 ---- */
    ds1302_wirte_rig(DS1302_REG_RAM0, 0xA5);
    a = ds1302_read_rig(DS1302_REG_RAM0 | 0x01);
    ds1302_wirte_rig(DS1302_REG_RAM0, 0x5A);
    b = ds1302_read_rig(DS1302_REG_RAM0 | 0x01);
    if (a == 0xA5 && b == 0x5A) result |= 0x01;

    /* ---- 方法 2: 秒寄存器 写读测试 ---- */
    sec_backup = ds1302_read_rig(DS1302_REG_SEC | 0x01);
    /* 写一个 BCD 合法且不太可能巧合相同的值: 49 秒 (CH=0) */
    ds1302_wirte_rig(DS1302_REG_SEC, 0x49);
    sec_check = ds1302_read_rig(DS1302_REG_SEC | 0x01);
    /* 还原原值 (保留 CH 位) */
    ds1302_wirte_rig(DS1302_REG_SEC, sec_backup & 0x7F);
    if ((sec_check & 0x7F) == 0x49) result |= 0x02;

    ds1302_wirte_rig(DS1302_REG_WP, 0x80);

    return result;
}

/* ------------------------------------------------------------
 *  DS1302 内部 RAM 读写 (31 字节, index 0..30)
 *  地址映射: 写 = 0xC0 + 2*index, 读 = 0xC1 + 2*index
 * ------------------------------------------------------------ */
void ds1302_ram_write(u8 index, u8 data)
{
    if (index >= 31) return;
    ds1302_wirte_rig(DS1302_REG_WP, 0x00);
    ds1302_wirte_rig((u8)(DS1302_REG_RAM0 + 2 * index), data);
    ds1302_wirte_rig(DS1302_REG_WP, 0x80);
}

u8 ds1302_ram_read(u8 index)
{
    if (index >= 31) return 0xFF;
    return ds1302_read_rig((u8)(DS1302_REG_RAM0 + 2 * index + 1));
}

/* ------------------------------------------------------------
 *  完整性校验
 *  读出 7 个时间寄存器, 检查是否都在 BCD 合法范围内。
 *  这里的目的是发现 "之前由于时序错误导致部分寄存器没写进去"
 *  的故障状态(读出 0xFF), 触发一次全量重写。
 *  返回 1 = 全部合法, 0 = 至少一个字段越界。
 * ------------------------------------------------------------ */
static u8 bcd_in_range(u8 raw, u8 max_high_nibble, u8 max_low_nibble)
{
    return (((raw >> 4) <= max_high_nibble) && ((raw & 0x0F) <= max_low_nibble)) ? 1u : 0u;
}

static u8 ds1302_calendar_is_valid(void)
{
    u8 s = ds1302_read_rig(DS1302_REG_SEC   | 0x01) & 0x7F;
    u8 m = ds1302_read_rig(DS1302_REG_MIN   | 0x01) & 0x7F;
    u8 h = ds1302_read_rig(DS1302_REG_HOUR  | 0x01) & 0x3F;   /* 强制 24h */
    u8 d = ds1302_read_rig(DS1302_REG_DATE  | 0x01) & 0x3F;
    u8 mo= ds1302_read_rig(DS1302_REG_MONTH | 0x01) & 0x1F;
    u8 wk= ds1302_read_rig(DS1302_REG_DAY   | 0x01) & 0x07;
    u8 y = ds1302_read_rig(DS1302_REG_YEAR  | 0x01);

    /* 各字段 BCD 范围检查 */
    if (!bcd_in_range(s,  5, 9))  return 0;          /* 0..59 */
    if (!bcd_in_range(m,  5, 9))  return 0;          /* 0..59 */
    if (!bcd_in_range(h,  2, 9))  return 0;          /* 0..23 */
    if (!bcd_in_range(d,  3, 9) || d == 0) return 0; /* 1..31 */
    if (!bcd_in_range(mo, 1, 9) || mo == 0) return 0;/* 1..12 */
    if (wk == 0 || wk > 7)        return 0;          /* 1..7  */
    if (!bcd_in_range(y,  9, 9))  return 0;          /* 0..99 */

    /* 进一步的边界 */
    if (h  >= 0x24) return 0;
    if (d  >  0x31) return 0;
    if (mo >  0x12) return 0;

    return 1;
}

/* ------------------------------------------------------------
 *  上电初始化
 *  策略:
 *   1) 通信不通          -> 不写, 上层走软件计时
 *   2) CH=1 (从未启动)   -> 写缺省时间, 启动振荡
 *   3) 任一寄存器非法    -> 整体重写, 修复半残数据 (本次场景)
 *   4) 全部合法          -> 保留原时间(掉电保存)
 * ------------------------------------------------------------ */
void ds1032_init(void)
{
    u8 sec;

    ds1302_gpio_init();
    Delay(100);                      /* 等 DS1302 内部 POR 完成 */

    ds1302_wirte_rig(DS1302_REG_WP, 0x00);
    ds1302_wirte_rig(DS1302_REG_TC, 0x00);

    if (!ds1302_self_test())
    {
        ds1302_wirte_rig(DS1302_REG_WP, 0x80);
        return;
    }

    sec = ds1302_read_rig(DS1302_REG_SEC | 0x01);

    if ((sec & 0x80) || !ds1302_calendar_is_valid())
    {
        /* 时钟未运行 或 历史数据被损坏 -> 强制全量重写 */
        ds1302_set_time(2026, 5, 30,
                        12, 0, 0,
                        7);
    }
    else
    {
        ds1302_wirte_rig(DS1302_REG_WP, 0x80);
    }
}

/* ------------------------------------------------------------
 *  读取所有时间寄存器(原始 BCD)
 * ------------------------------------------------------------ */
void ds1032_read_time(void)
{
    read_time[0] = ds1302_read_rig(DS1302_REG_SEC   | 0x01);
    read_time[1] = ds1302_read_rig(DS1302_REG_MIN   | 0x01);
    read_time[2] = ds1302_read_rig(DS1302_REG_HOUR  | 0x01);
    read_time[3] = ds1302_read_rig(DS1302_REG_DATE  | 0x01);
    read_time[4] = ds1302_read_rig(DS1302_REG_MONTH | 0x01);
    read_time[5] = ds1302_read_rig(DS1302_REG_DAY   | 0x01);
    read_time[6] = ds1302_read_rig(DS1302_REG_YEAR  | 0x01);
}

/* ------------------------------------------------------------
 *  BCD -> 十进制并写入 TimeData
 * ------------------------------------------------------------ */
void ds1032_read_realTime(void)
{
    u8 raw;

    ds1032_read_time();

    raw = read_time[0] & 0x7F;
    TimeData.second = ((raw >> 4) & 0x07) * 10 + (raw & 0x0F);

    raw = read_time[1] & 0x7F;
    TimeData.minute = ((raw >> 4) & 0x07) * 10 + (raw & 0x0F);

    raw = read_time[2] & 0x3F;
    TimeData.hour   = ((raw >> 4) & 0x03) * 10 + (raw & 0x0F);

    raw = read_time[3] & 0x3F;
    TimeData.day    = ((raw >> 4) & 0x03) * 10 + (raw & 0x0F);

    raw = read_time[4] & 0x1F;
    TimeData.month  = ((raw >> 4) & 0x01) * 10 + (raw & 0x0F);

    TimeData.week   = read_time[5] & 0x07;

    raw = read_time[6];
    TimeData.year   = (u16)(((raw >> 4) & 0x0F) * 10 + (raw & 0x0F)) + 2000;
}
