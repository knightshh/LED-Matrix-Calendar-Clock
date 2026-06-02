#include "led_matrix.h"

#define R1_PORT    GPIOB
#define R1_PIN     GPIO_Pin_0
#define G1_PORT    GPIOB
#define G1_PIN     GPIO_Pin_1
#define B1_PORT    GPIOA
#define B1_PIN     GPIO_Pin_2
#define R2_PORT    GPIOB
#define R2_PIN     GPIO_Pin_3
#define G2_PORT    GPIOB
#define G2_PIN     GPIO_Pin_8
#define B2_PORT    GPIOB
#define B2_PIN     GPIO_Pin_5

#define A_PORT     GPIOA
#define A_PIN      GPIO_Pin_3
#define B_PORT     GPIOA
#define B_PIN      GPIO_Pin_4
#define C_PORT     GPIOA
#define C_PIN      GPIO_Pin_5
#define D_PORT     GPIOA
#define D_PIN      GPIO_Pin_6
#define E_PORT     GPIOA
#define E_PIN      GPIO_Pin_7

#define CLK_PORT   GPIOA
#define CLK_PIN    GPIO_Pin_1
#define LAT_PORT   GPIOA
#define LAT_PIN    GPIO_Pin_0
#define OE_PORT    GPIOB
#define OE_PIN     GPIO_Pin_6

void LED_Matrix_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = B1_PIN | A_PIN | B_PIN | C_PIN | D_PIN | E_PIN | CLK_PIN | LAT_PIN;
    GPIO_Init(A_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = R1_PIN | G1_PIN | R2_PIN | G2_PIN | B2_PIN | OE_PIN;
    GPIO_Init(B_PORT, &GPIO_InitStructure);

    GPIO_ResetBits(OE_PORT, OE_PIN);
    GPIO_ResetBits(R1_PORT, R1_PIN);
    GPIO_ResetBits(G1_PORT, G1_PIN);
    GPIO_ResetBits(B1_PORT, B1_PIN);
    GPIO_ResetBits(R2_PORT, R2_PIN);
    GPIO_ResetBits(G2_PORT, G2_PIN);
    GPIO_ResetBits(B2_PORT, B2_PIN);
}

void LED_Matrix_SetPixel(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2)
{
    GPIO_WriteBit(R1_PORT, R1_PIN, r1 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(G1_PORT, G1_PIN, g1 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(B1_PORT, B1_PIN, b1 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(R2_PORT, R2_PIN, r2 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(G2_PORT, G2_PIN, g2 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(B2_PORT, B2_PIN, b2 ? Bit_SET : Bit_RESET);

    GPIO_SetBits(CLK_PORT, CLK_PIN);
    GPIO_ResetBits(CLK_PORT, CLK_PIN);
}

void LED_Matrix_Latch(void)
{
    GPIO_SetBits(LAT_PORT, LAT_PIN);
    GPIO_ResetBits(LAT_PORT, LAT_PIN);
}

void LED_Matrix_SetRow(uint8_t row)
{
    GPIO_WriteBit(A_PORT, A_PIN, (row & 0x01) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(B_PORT, B_PIN, (row & 0x02) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(C_PORT, C_PIN, (row & 0x04) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(D_PORT, D_PIN, (row & 0x08) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(E_PORT, E_PIN, (row & 0x10) ? Bit_SET : Bit_RESET);
}
