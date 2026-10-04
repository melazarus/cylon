/* Cylon hardware: 3 WS2812 LEDs on PB1
 *
 * LED: XL-4020RGBC-WS2812B (WS2812B compatible, data order G,R,B, MSB first)
 *   bit period : 1.25 us (800 kHz)
 *   '0' high   : ~0.33 us
 *   '1' high   : ~0.83 us
 *   reset low  : 125 us (100 bit periods)
 *
 * The waveform is generated with the TIM1 channel 3 complementary output
 * (TIM1_CH3N); TIM1 remap 001 routes it to PB1. TIM1 runs at 48 MHz
 * (prescaler 0, ARR = 59 -> 60 ticks = 1.25 us per bit) and DMA1 channel 5
 * (the TIM1_UP request) writes one compare value (CH3CVR) per timer update,
 * i.e. one value per WS2812 bit.
 *
 * CH3N is the complement of the PWM reference. With PWM mode 2 the reference
 * is low while CNT < CCR, so CH3N is high at the start of the period for CCR
 * ticks: the compare value is the bit's high time directly, and CCR = 0 keeps
 * the output low for the whole period (the reset/latch).
 */
#include <ch32x035.h> /* both X033 and X035 */
#include <string.h>   /* memset() */

#include "board.h"
#include "debug.h"

/* WS2812 data output: PB1 (TIM1_CH3N after TIM1 remap 001) */
#define LED_PORT GPIOB
#define LED_PIN  GPIO_Pin_1
#define LEDS_NUM BOARD_LEDS

/* bit timing in 48 MHz ticks */
#define BIT_PERIOD_TICKS (60)  /* 1.25 us */
#define BIT0_HIGH_TICKS  (16)  /* ~0.33 us */
#define BIT1_HIGH_TICKS  (40)  /* ~0.83 us */
#define RESET_ENTRIES    (100) /* 100 bit periods = 125 us */

#define LED_DMA_CH     DMA1_Channel5
#define CCR_BUFFER_LEN ((LEDS_NUM * 24) + RESET_ENTRIES)

typedef struct
{
    uint8_t g; /* Green */
    uint8_t r; /* Red */
    uint8_t b; /* Blue */
} ws2812b_color_t;

/* the LED colors as set by the caller */
static ws2812b_color_t leds[LEDS_NUM];

/* one compare value per WS2812 bit, plus the reset at the end of a frame */
static uint16_t ccr_buf[CCR_BUFFER_LEN];

/* encode one color byte as 8 compare values, MSB first */
static void encode_byte(uint16_t *buf, uint8_t value)
{
    for (int i = 0; i < 8; i++)
    {
        buf[i] = (value & (0x80 >> i)) ? BIT1_HIGH_TICKS : BIT0_HIGH_TICKS;
    }
}

/* encode one LED as 24 compare values in WS2812 order (G, R, B) */
static void encode_color(uint16_t *buf, uint8_t g, uint8_t r, uint8_t b)
{
    encode_byte(&buf[0], g);
    encode_byte(&buf[8], r);
    encode_byte(&buf[16], b);
}

/* configure TIM1_CH3N on PB1 for the WS2812 waveform */
static void led_timer_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_BDTRInitTypeDef TIM_BDTRInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);

    /* route TIM1_CH3N to PB1 */
    GPIO_PinRemapConfig(GPIO_PartialRemap1_TIM1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Period = BIT_PERIOD_TICKS - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC3Init(TIM1, &TIM_OCInitStructure);

    /* the dead-time/break block must be configured for the complementary
     * output to be driven (dead time 0, automatic output enable) */
    TIM_BDTRInitStructure.TIM_OSSIState = TIM_OSSIState_Disable;
    TIM_BDTRInitStructure.TIM_OSSRState = TIM_OSSRState_Disable;
    TIM_BDTRInitStructure.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
    TIM_BDTRInitStructure.TIM_DeadTime = 0;
    TIM_BDTRInitStructure.TIM_Break = TIM_Break_Disable;
    TIM_BDTRInitStructure.TIM_BreakPolarity = TIM_BreakPolarity_High;
    TIM_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStructure);

    /* shadow register on: each DMA value becomes active at the NEXT update
     * event, i.e. it is loaded a full bit period ahead of when it is used.
     * The frame therefore starts with one extra reset bit, and the first data
     * bit is transferred by the timer hardware at the update event rather than
     * by the DMA a few cycles late. */
    TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_CCxNCmd(TIM1, TIM_Channel_3, TIM_CCxN_Enable);

    /* the reset at the end of the buffer is never overwritten by board_show_leds() */
    for (int i = LEDS_NUM * 24; i < CCR_BUFFER_LEN; i++)
    {
        ccr_buf[i] = 0;
    }
}

/* configure DMA1 channel 5 to feed CH3CVR on every timer update */
static void led_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(LED_DMA_CH);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&TIM1->CH3CVR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)ccr_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = CCR_BUFFER_LEN;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(LED_DMA_CH, &DMA_InitStructure);

    TIM_DMACmd(TIM1, TIM_DMA_Update, ENABLE);
}

void board_init(void)
{
    memset(leds, 0, sizeof(leds));

    led_timer_init();
    led_dma_init();

    /* start with the output enabled and the data line low */
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
    DMA_Cmd(LED_DMA_CH, ENABLE);
}

void board_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= LEDS_NUM)
    {
        return;
    }
    leds[index].r = r;
    leds[index].g = g;
    leds[index].b = b;
}

void board_show_leds(void)
{
    /* DMA_Mode_Normal does not reload automatically, so wait for any frame
     * still in flight before touching ccr_buf: the DMA reads the buffer while
     * it runs, so encoding first could tear the frame being transmitted */
    while (DMA_GetCurrDataCounter(LED_DMA_CH) != 0)
    {
    }
    DMA_ClearFlag(DMA1_FLAG_TC5);
    DMA_Cmd(LED_DMA_CH, DISABLE);

    /* Stop the free-running timer and restart it together with the DMA below,
     * so every frame starts at a known counter value. Restarting the DMA under
     * a running timer lets it race the update event, which mistimes the first
     * bits of the frame. The last active compare value is 0 (the reset), so the
     * line stays low while the timer is stopped. */
    TIM_Cmd(TIM1, DISABLE);
    TIM1->CNT = 0;

    /* copy the LED colors into the DMA compare buffer */
    for (int i = 0; i < LEDS_NUM; i++)
    {
        encode_color(&ccr_buf[i * 24], leds[i].g, leds[i].r, leds[i].b);
    }

    /* arm the DMA first, then release the timer */
    DMA_SetCurrDataCounter(LED_DMA_CH, CCR_BUFFER_LEN);
    DMA_Cmd(LED_DMA_CH, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
}

/* interrupt handlers */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void NMI_Handler(void)
{
    PRINT("NMI_Handler\r\n");
}

void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void)
{
    PRINT("HARDFAULT\r\n");
    while (1)
    {
    }
}
