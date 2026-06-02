/* ============================================================
 *  LED 全彩点阵屏日历时钟  (128 x 64 / HUB75 / 1-32 扫)
 *  MCU: STM32F103C8  @ 72 MHz
 *
 *  硬件接线（与 PCB 保持一致）
 *  --------------------------------------------------------
 *   信号 | MCU      | 信号 | MCU
 *   R1   | PB0      | A    | PA3
 *   G1   | PB1      | B    | PA4
 *   B1   | PA2      | C    | PA5
 *   R2   | PB12     | D    | PA6
 *   G2   | PB8      | E    | PA7
 *   B2   | PB5      | CLK  | PA1
 *   OE   | PB6      | LAT  | PA0
 *  --------------------------------------------------------
 * ============================================================ */
#include "stm32f10x.h"
#include "ds1302.h"

/* ============================================================
 *  烧录时间同步配置
 *
 *  SYNC_RTC_ON_BOOT = 1:
 *      本次固件 "首次上电" 时, 用下方 SYNC_* 指定的时间覆盖
 *      DS1302 内部时间; 之后**同一份固件**再上电 / 复位都不再
 *      写, DS1302 继续按原走时计时。
 *
 *      实现方式: 用编译时间戳 (__DATE__ __TIME__) 生成 16-bit
 *      指纹存到 DS1302 内部 RAM。每次启动比对 RAM 中的指纹:
 *        不同  -> 是新烧的固件, 同步一次, 然后把新指纹写回 RAM
 *        相同  -> 跟上次是同一份固件, 不写, 直接读
 *      因此把宏开成 1 也能多次复位, 不会重置时间。
 *
 *  SYNC_RTC_ON_BOOT = 0:
 *      永远不写, 完全交给 DS1302 内部时间。
 *
 *  SYNC_WEEK 取值 1..7, 该字段只写进 DS1302 的 DAY 寄存器,
 *  屏幕显示星期实际是用 Sakamoto 算法从 年/月/日 反推, 写错
 *  不影响显示。
 * ============================================================ */
#define SYNC_RTC_ON_BOOT  0     /* 1=新固件首次上电写入下方时间, 0=永不写 */

#define SYNC_YEAR     2026
#define SYNC_MONTH    5
#define SYNC_DAY      30
#define SYNC_HOUR     21
#define SYNC_MINUTE   35
#define SYNC_SECOND   0
#define SYNC_WEEK     6         /* 2026-05-30 是周六, 写错不影响正确显示*/

/* ---------- GPIOB ---------- */
#define R1_PIN     (1U << 0)
#define G1_PIN     (1U << 1)
#define R2_PIN     (1U << 12)
#define G2_PIN     (1U << 8)
#define B2_PIN     (1U << 5)
#define OE_PIN     (1U << 6)

/* ---------- GPIOA ---------- */
#define LAT_PIN    (1U << 0)
#define CLK_PIN    (1U << 1)
#define B1_PIN     (1U << 2)
#define A_PIN      (1U << 3)
#define B_PIN      (1U << 4)
#define C_PIN      (1U << 5)
#define D_PIN      (1U << 6)
#define E_PIN      (1U << 7)

#define SCREEN_W   128
#define SCREEN_H   64

/* ============================================================
 *  3-bit 颜色（bit0=R, bit1=G, bit2=B）
 * ============================================================ */
#define COLOR_BLACK    0x00
#define COLOR_RED      0x01
#define COLOR_GREEN    0x02
#define COLOR_YELLOW   0x03
#define COLOR_BLUE     0x04
#define COLOR_MAGENTA  0x05
#define COLOR_CYAN     0x06
#define COLOR_WHITE    0x07

/* 帧缓冲：每像素 1 字节，低 3 位有效 */
static uint8_t frame_buffer[SCREEN_H][SCREEN_W];

/* ============================================================
 *  5x7 ASCII 字体（每行低 5 位，bit4 为最左列）
 *  字符外补 1 像素间距 -> 每格 6x8
 * ============================================================ */
typedef struct {
    char ch;
    uint8_t row[7];
} glyph_t;

static const glyph_t g_font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {':', {0x00,0x06,0x06,0x00,0x06,0x06,0x00}},
    {'/', {0x01,0x02,0x02,0x04,0x08,0x08,0x10}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x06,0x06}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},

    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}},
    {'3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},

    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
    {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
    {'h', {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}},
    {'m', {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}},
    {'r', {0x00,0x00,0x16,0x18,0x10,0x10,0x10}},
    {'s', {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}},
};
#define FONT_COUNT (sizeof(g_font)/sizeof(g_font[0]))

static const uint8_t *find_glyph(char c)
{
    for (uint32_t i = 0; i < FONT_COUNT; i++) {
        if (g_font[i].ch == c) return g_font[i].row;
    }
    return 0;
}

/* ============================================================
 *  绘图原语
 * ============================================================ */
static void draw_pixel(int x, int y, uint8_t color)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
        frame_buffer[y][x] = color & 0x07;
    }
}

static void clear_buffer(void)
{
    uint32_t *p = (uint32_t *)frame_buffer;
    for (uint32_t i = 0; i < sizeof(frame_buffer) / 4; i++) p[i] = 0;
}

static void fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            draw_pixel(x + i, y + j, color);
}

static void draw_hline(int x, int y, int len, uint8_t color)
{
    for (int i = 0; i < len; i++) draw_pixel(x + i, y, color);
}

static void draw_dotted_hline(int x, int y, int len, int step, uint8_t color)
{
    for (int i = 0; i < len; i += step) draw_pixel(x + i, y, color);
}

/* scale: 1=原始 5x7，2=10x14 ... */
static void draw_char(int x, int y, char c, uint8_t color, int scale)
{
    const uint8_t *bm = find_glyph(c);
    if (!bm) return;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = bm[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                if (scale == 1) {
                    draw_pixel(x + col, y + row, color);
                } else {
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++)
                            draw_pixel(x + col * scale + dx,
                                       y + row * scale + dy, color);
                }
            }
        }
    }
}

static void draw_string(int x, int y, const char *s, uint8_t color, int scale)
{
    int step = 6 * scale; /* 5px 字宽 + 1px 间距 */
    while (*s) {
        draw_char(x, y, *s, color, scale);
        x += step;
        s++;
    }
}

static int string_width(const char *s, int scale)
{
    int n = 0;
    while (*s) { n++; s++; }
    return n * 6 * scale - scale;  /* 末尾不计间距 */
}

/* L 形装饰角，长度 3 */
static void draw_corner(int x, int y, int dx, int dy, uint8_t color)
{
    for (int i = 0; i < 3; i++) {
        draw_pixel(x + i * dx, y, color);
        draw_pixel(x, y + i * dy, color);
    }
}

/* ============================================================
 *  16x16 中文字模（黑体精简版）
 *   每行一个 uint16，bit15 = 最左列
 *   索引顺序：周 / 一 / 二 / 三 / 四 / 五 / 六 / 日
 * ============================================================ */
#define CJK_ZHOU  0
#define CJK_YI    1
#define CJK_ER    2
#define CJK_SAN   3
#define CJK_SI    4
#define CJK_WU    5
#define CJK_LIU   6
#define CJK_RI    7

static const uint16_t g_cjk_font[8][16] = {
    /* 周 */
    {
        0x0000, 0x1FFC, 0x1084, 0x1FF4,
        0x1084, 0x1184, 0x17E4, 0x3004,
        0x37E4, 0x2424, 0x2424, 0x27E4,
        0x6004, 0x401C, 0x0000, 0x0000,
    },
    /* 一 */
    {
        0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x7FFE,
        0x7FFE, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000,
    },
    /* 二 */
    {
        0x0000, 0x0000, 0x0000, 0x0FF0,
        0x0FF0, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000,
        0x7FFE, 0x7FFE, 0x0000, 0x0000,
    },
    /* 三 */
    {
        0x0000, 0x0000, 0x0000, 0x0FF0,
        0x0FF0, 0x0000, 0x0000, 0x1FF8,
        0x1FF8, 0x0000, 0x0000, 0x0000,
        0x7FFE, 0x7FFE, 0x0000, 0x0000,
    },
    /* 四 */
    {
        0x0000, 0x3FFC, 0x2244, 0x2244,
        0x2244, 0x2244, 0x2644, 0x2444,
        0x2874, 0x2804, 0x2004, 0x2004,
        0x3FFC, 0x2004, 0x0000, 0x0000,
    },
    /* 五 */
    {
        0x0000, 0x0000, 0x3FFC, 0x3FFC,
        0x0100, 0x0300, 0x0300, 0x1FF0,
        0x0210, 0x0210, 0x0210, 0x0210,
        0x0610, 0x7FFE, 0x0000, 0x0000,
    },
    /* 六 */
    {
        0x0000, 0x0300, 0x0180, 0x0080,
        0x7FFE, 0x7FFE, 0x0000, 0x0420,
        0x0630, 0x0C30, 0x0818, 0x180C,
        0x300C, 0x2004, 0x0000, 0x0000,
    },
    /* 日 */
    {
        0x0000, 0x0000, 0x1FF0, 0x1FF0,
        0x1830, 0x1830, 0x1830, 0x1FF0,
        0x1FF0, 0x1830, 0x1830, 0x1830,
        0x1FF0, 0x1FF0, 0x0000, 0x0000,
    },
};

static void draw_cjk_char(int x, int y, uint8_t idx, uint8_t color)
{
    if (idx >= sizeof(g_cjk_font) / sizeof(g_cjk_font[0])) return;
    const uint16_t *bm = g_cjk_font[idx];
    for (int row = 0; row < 16; row++) {
        uint16_t bits = bm[row];
        if (!bits) continue;
        for (int col = 0; col < 16; col++) {
            if (bits & (1u << (15 - col))) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

/* ============================================================
 *  星期推算（Sakamoto 法）
 * ============================================================ */
/* Sakamoto 法：返回 0=Sun … 6=Sat */
static uint8_t day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y -= 1;
    return (uint8_t)((y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7);
}

/* 返回 0..6（周日..周六）对应的汉字索引 */
static uint8_t weekday_cjk_index(uint8_t w)
{
    /* w: 0=Sun 1=Mon 2=Tue 3=Wed 4=Thu 5=Fri 6=Sat
       中文：周日 周一 周二 周三 周四 周五 周六 */
    static const uint8_t idx[7] = {
        CJK_RI, CJK_YI, CJK_ER, CJK_SAN, CJK_SI, CJK_WU, CJK_LIU
    };
    return idx[w];
}

/* ============================================================
 *  零分配数字格式化
 * ============================================================ */
static void put2(char *buf, uint8_t val)
{
    buf[0] = (char)('0' + (val / 10) % 10);
    buf[1] = (char)('0' + val % 10);
}

static void put4(char *buf, uint16_t val)
{
    buf[0] = (char)('0' + (val / 1000) % 10);
    buf[1] = (char)('0' + (val / 100)  % 10);
    buf[2] = (char)('0' + (val / 10)   % 10);
    buf[3] = (char)('0' + val % 10);
}

/* ============================================================
 *  画面渲染
 *
 *   y=0..6   "Dream Chaser"     1x  CYAN     (居中)
 *   y=10..23 HH:MM:SS           2x  YELLOW   (居中, 冒号闪)
 *   y=27..29 蓝色虚线分隔
 *   y=32..38 YYYY/MM/DD         1x  WHITE    (居中)
 *   y=42..57 周X (16x16 中文)     MAGENTA  (居中)
 *   y=60..62 秒进度条 (按秒变色)
 *   4 角青色 L 形装饰
 * ============================================================ */
static void render_screen(uint8_t hh, uint8_t mm, uint8_t ss,
                          uint16_t year, uint8_t mon, uint8_t day,
                          uint8_t blink_on)
{
    char time_str[9];
    char date_str[11];
    int  x, w;

    clear_buffer();

    /* 4 角装饰 */
    draw_corner(0,        0,         1,  1, COLOR_CYAN);
    draw_corner(SCREEN_W - 1, 0,    -1,  1, COLOR_CYAN);
    draw_corner(0,        SCREEN_H - 1, 1, -1, COLOR_CYAN);
    draw_corner(SCREEN_W - 1, SCREEN_H - 1, -1, -1, COLOR_CYAN);

    /* 标题 */
    {
        const char *title = "Dream Chaser";
        w = string_width(title, 1);
        x = (SCREEN_W - w) / 2;
        draw_string(x, 0, title, COLOR_CYAN, 1);
    }

    /* 时间 HH:MM:SS（2x） */
    put2(&time_str[0], hh);
    time_str[2] = blink_on ? ':' : ' ';
    put2(&time_str[3], mm);
    time_str[5] = blink_on ? ':' : ' ';
    put2(&time_str[6], ss);
    time_str[8] = 0;

    w = string_width(time_str, 2);    /* 8 chars * 12 - 2 = 94 */
    x = (SCREEN_W - w) / 2;
    draw_string(x, 10, time_str, COLOR_YELLOW, 2);

    /* 蓝色虚线分隔（时间 / 日期之间） */
    draw_dotted_hline(8, 28, SCREEN_W - 16, 3, COLOR_BLUE);

    /* 日期 YYYY/MM/DD */
    put4(&date_str[0], year);
    date_str[4] = '/';
    put2(&date_str[5], mon);
    date_str[7] = '/';
    put2(&date_str[8], day);
    date_str[10] = 0;

    w = string_width(date_str, 1);    /* 10*6 - 1 = 59 */
    x = (SCREEN_W - w) / 2;
    draw_string(x, 32, date_str, COLOR_WHITE, 1);

    /* 星期（"周X" 两个 16x16 汉字，洋红色） */
    {
        uint8_t wd = day_of_week(year, mon, day);
        int total_w = 16 * 2;            /* 两个汉字宽 */
        int xs = (SCREEN_W - total_w) / 2;
        draw_cjk_char(xs,      42, CJK_ZHOU,                 COLOR_MAGENTA);
        draw_cjk_char(xs + 16, 42, weekday_cjk_index(wd),    COLOR_MAGENTA);
    }

    /* 秒进度条（红 0-19s，黄 20-39s，绿 40-59s） */
    {
        uint8_t bar_color;
        if      (ss < 20) bar_color = COLOR_RED;
        else if (ss < 40) bar_color = COLOR_YELLOW;
        else              bar_color = COLOR_GREEN;

        int len = ((int)ss + 1) * SCREEN_W / 60;
        if (len > SCREEN_W) len = SCREEN_W;

        /* 进度条底色（深蓝细线）+ 进度（鲜亮色） */
        fill_rect(0,   60, SCREEN_W, 3, COLOR_BLACK);
        draw_hline(0,  61, SCREEN_W, COLOR_BLUE);
        fill_rect(0,   60, len,      3, bar_color);
    }
}

/* ============================================================
 *  扫描刷新（HUB75：先关 OE -> 行选 -> 移位 -> 锁存 -> 开 OE -> 保持）
 * ============================================================ */
static void scan_screen(void)
{
    for (uint8_t scan_row = 0; scan_row < 32; scan_row++) {
        /* 1) 关闭显示，避免移位/换行时鬼影 */
        GPIOB->BSRR = OE_PIN;

        /* 2) 行选；同时把 PA0/PA1/PA2 (LAT/CLK/B1) 清零，方便 BSRR/BRR 控制 */
        uint16_t row_sel =
            ((scan_row & 0x01) ? A_PIN : 0) |
            ((scan_row & 0x02) ? B_PIN : 0) |
            ((scan_row & 0x04) ? C_PIN : 0) |
            ((scan_row & 0x08) ? D_PIN : 0) |
            ((scan_row & 0x10) ? E_PIN : 0);
        GPIOA->ODR = row_sel;

        /* 3) 把上下两半行 128 列像素移入移位寄存器 */
        for (int col = 0; col < SCREEN_W; col++) {
            uint8_t up = frame_buffer[scan_row][col];
            uint8_t dn = frame_buffer[scan_row + 32][col];

            /* 上半屏 R/G + 下半屏 R/G/B 在 GPIOB；OE 保持 1 */
            uint16_t pb = OE_PIN;
            if (up & COLOR_RED)   pb |= R1_PIN;
            if (up & COLOR_GREEN) pb |= G1_PIN;
            if (dn & COLOR_RED)   pb |= R2_PIN;
            if (dn & COLOR_GREEN) pb |= G2_PIN;
            if (dn & COLOR_BLUE)  pb |= B2_PIN;
            GPIOB->ODR = pb;

            /* 上半屏 B1 在 PA2，用 BSRR/BRR 保留行选不变 */
            if (up & COLOR_BLUE) GPIOA->BSRR = B1_PIN;
            else                 GPIOA->BRR  = B1_PIN;

            /* CLK 上升沿移位 */
            GPIOA->BSRR = CLK_PIN;
            GPIOA->BRR  = CLK_PIN;
        }

        /* 4) 锁存 */
        GPIOA->BSRR = LAT_PIN;
        GPIOA->BRR  = LAT_PIN;

        /* 5) 打开显示 */
        GPIOB->BRR = OE_PIN;

        /* 6) 保持时间决定亮度 */
        for (volatile uint32_t i = 0; i < 80; i++) __NOP();
    }

    /* 全部行扫完后关闭显示，避免最后一行常亮 */
    GPIOB->BSRR = OE_PIN;
}

/* ============================================================
 *  时间状态(由 DS1302 提供权威时间)
 *
 *  策略:
 *   - DS1302 是断电仍走时的硬件时钟, 上电时一次性读出, 之后由
 *     TIM2 每秒中断从 DS1302 重新读取一次, 显示侧只缓存上一次
 *     的值, 任何重启 / 掉电再上电都会从 DS1302 恢复正确时间。
 *   - 冒号闪烁仍由 TIM2 软件 500ms 触发。
 * ============================================================ */
static volatile uint8_t  s_hour     = 0;
static volatile uint8_t  s_minute   = 0;
static volatile uint8_t  s_second   = 0;
static volatile uint16_t s_year     = 2026;
static volatile uint8_t  s_month    = 1;
static volatile uint8_t  s_day      = 1;
static volatile uint8_t  s_dirty    = 1;
static volatile uint8_t  s_blink_on = 1;
static volatile uint8_t  s_need_rtc_read = 1;   /* 主循环负责真正读 RTC, 避免在中断里花太多时间 */
static          uint8_t  s_rtc_ok   = 0;        /* 0 = DS1302 通信失败, 退化到软件计时 */

static uint8_t days_in_month_simple(uint16_t y, uint8_t m)
{
    static const uint8_t tbl[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t d = tbl[(m - 1) % 12];
    if (m == 2 && (((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0))) d = 29;
    return d;
}

static void sw_advance_one_second(void)
{
    if (++s_second >= 60) {
        s_second = 0;
        if (++s_minute >= 60) {
            s_minute = 0;
            if (++s_hour >= 24) {
                s_hour = 0;
                if (++s_day > days_in_month_simple(s_year, s_month)) {
                    s_day = 1;
                    if (++s_month > 12) { s_month = 1; s_year++; }
                }
            }
        }
    }
}

static void sync_from_rtc(void)
{
    ds1032_read_realTime();
    s_hour   = TimeData.hour;
    s_minute = TimeData.minute;
    s_second = TimeData.second;
    s_year   = TimeData.year;
    s_month  = TimeData.month;
    s_day    = TimeData.day;
    s_dirty  = 1;
}

void TIM2_IRQHandler(void)
{
    static uint32_t ms_tick = 0;

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        /* 每 500 ms 切换冒号闪烁状态 */
        if ((ms_tick % 500) == 0) {
            s_blink_on ^= 1;
            s_dirty = 1;
        }

        if (++ms_tick >= 1000) {
            ms_tick = 0;
            if (s_rtc_ok) {
                /* 通知主循环到 DS1302 取一次最新时间 */
                s_need_rtc_read = 1;
            } else {
                /* DS1302 不通: 退化到纯软件计时, 至少显示能动 */
                sw_advance_one_second();
                s_dirty = 1;
            }
        }
    }
}

static void TIM2_Init_1ms(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef nv;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 72MHz / 72 / 1000 = 1kHz -> 1ms 中断 */
    tb.TIM_Period        = 1000 - 1;
    tb.TIM_Prescaler     = 72   - 1;
    tb.TIM_ClockDivision = 0;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tb);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nv.NVIC_IRQChannel                   = TIM2_IRQn;
    nv.NVIC_IRQChannelPreemptionPriority = 1;
    nv.NVIC_IRQChannelSubPriority        = 1;
    nv.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nv);

    TIM_Cmd(TIM2, ENABLE);
}

/* ============================================================ */
int main(void)
{
    /* 时钟使能（GPIOA / GPIOB / AFIO） */
    RCC->APB2ENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO;

    /* 关掉 JTAG 保留 SWD：避免 PB3/PB4/PA15 与 LED 信号冲突
       （工程把 GPIOB->CRL 全部设为输出，会占用默认 JTDO=PB3） */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    /* 全部置为推挽输出 50MHz */
    GPIOA->CRH = 0x33333333;
    GPIOA->CRL = 0x33333333;
    GPIOB->CRH = 0x33333333;
    GPIOB->CRL = 0x33333333;

    /* 上电默认：关闭显示，所有数据线置 0 */
    GPIOA->ODR = 0;
    GPIOB->ODR = OE_PIN;

    /* DS1302 初始化:
     *  - SYNC_RTC_ON_BOOT=1 时, 用上方 SYNC_* 宏指定的时间强制
     *    覆盖 DS1302 内部时间;
     *  - SYNC_RTC_ON_BOOT=0 时, 仅做 GPIO/振荡器/合法性检查,
     *    保留 DS1302 内部已有时间(电池供电下掉电不丢)。 */
    ds1032_init();
    s_rtc_ok = ds1302_self_test();

    if (s_rtc_ok) {
#if SYNC_RTC_ON_BOOT
        /* 用编译时间戳生成一个 16-bit 指纹, 存到 DS1302 内部 RAM
         * (RAM 跟时间寄存器共用电池, 掉电不丢)。
         * 同一份固件每次复位 / 上电时, 指纹与 RAM 中存的相同 ->
         * 跳过同步, 保留 DS1302 现有时间;
         * 重新编译并烧录后, __TIME__/__DATE__ 改变 -> 指纹不同
         * -> 执行一次同步, 然后把新指纹写回 RAM, 下次启动就不会
         * 再写了。 */
        {
            const char *stamp = __DATE__ __TIME__;
            uint16_t fp = 0xA53C;            /* 任意非零种子 */
            uint8_t  i;
            for (i = 0; stamp[i]; i++) {
                fp = (uint16_t)((fp << 5) - fp + (uint8_t)stamp[i]);  /* fp*31 + c */
            }
            /* RAM 字节 0..2: 魔数 0x5A + fp 高 8 位 + fp 低 8 位 */
            uint8_t magic = ds1302_ram_read(0);
            uint8_t fp_hi = ds1302_ram_read(1);
            uint8_t fp_lo = ds1302_ram_read(2);

            if (magic != 0x5A
                || fp_hi != (uint8_t)(fp >> 8)
                || fp_lo != (uint8_t)(fp & 0xFF)) {
                /* 这是新固件 (或 RAM 未初始化) -> 同步一次时间 */
                ds1302_set_time(SYNC_YEAR, SYNC_MONTH, SYNC_DAY,
                                SYNC_HOUR, SYNC_MINUTE, SYNC_SECOND,
                                SYNC_WEEK);
                ds1302_ram_write(0, 0x5A);
                ds1302_ram_write(1, (uint8_t)(fp >> 8));
                ds1302_ram_write(2, (uint8_t)(fp & 0xFF));
            }
            /* 指纹相同则什么都不做, DS1302 继续按原来的时间走 */
        }
#endif
        sync_from_rtc();
    }

    /* 首屏 */
    render_screen(s_hour, s_minute, s_second,
                  s_year, s_month, s_day, s_blink_on);

    TIM2_Init_1ms();

    while (1) {
        if (s_rtc_ok && s_need_rtc_read) {
            s_need_rtc_read = 0;
            sync_from_rtc();
        }
        if (s_dirty) {
            s_dirty = 0;
            render_screen(s_hour, s_minute, s_second,
                          s_year, s_month, s_day, s_blink_on);
        }
        scan_screen();
    }
}
