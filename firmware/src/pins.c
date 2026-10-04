/* Auxiliary GPIO / PWM outputs PA0..PA3 for SysEx addresses 0x10..0x13.
 *
 * Type 0x00 drives the pin as a plain push-pull GPIO (level 0 = low, 1 = high).
 * Type 0x01 switches the pin to PWM and uses level as a duty cycle from
 * 0x00 (0%) to 0x7F (100%). PA0..PA3 are TIM2_CH1..CH4, so one timer and one
 * time base serve all four pins; TIM2 runs free and each channel's compare
 * register is updated as records arrive.
 *
 * A pin is left as an input until its first record, and the mode can be
 * switched between push-pull and PWM at any time (reconfiguring the GPIO
 * disconnects/reconnects the timer's alternate function).
 */
#include <ch32x035.h>

#include "pins.h"

#define PWM_PERIOD    (2399u) /* 48 MHz / 2400 = 20 kHz */
#define PWM_LEVEL_MAX (0x7Fu) /* 7-bit duty: 0x00..0x7F = 0..100% */

enum
{
    PIN_MODE_INPUT = 0,
    PIN_MODE_GPIO,
    PIN_MODE_PWM,
};

static const uint16_t pin_mask[PINS_ADDR_LAST - PINS_ADDR_FIRST + 1] = {
    GPIO_Pin_0,
    GPIO_Pin_1,
    GPIO_Pin_2,
    GPIO_Pin_3,
};

static uint8_t pin_mode[PINS_ADDR_LAST - PINS_ADDR_FIRST + 1] = {
    PIN_MODE_INPUT, PIN_MODE_INPUT, PIN_MODE_INPUT, PIN_MODE_INPUT,
};

static uint8_t pwm_ready = 0;

/* Bring up TIM2 for 20 kHz PWM on all four channels (once). */
static void pwm_init(void)
{
    TIM_TimeBaseInitTypeDef tb = {0};
    TIM_OCInitTypeDef oc = {0};

    if (pwm_ready)
    {
        return;
    }
    pwm_ready = 1;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    tb.TIM_Prescaler = 0;
    tb.TIM_Period = PWM_PERIOD;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tb);

    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &oc);
    TIM_OC2Init(TIM2, &oc);
    TIM_OC3Init(TIM2, &oc);
    TIM_OC4Init(TIM2, &oc);

    /* load a new duty at the next update, so dimming is glitch-free */
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);

    /* the channel outputs are gated by the timer's master output enable; without
     * this the pins stay in their idle (low) state */
    TIM_CtrlPWMOutputs(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

static void set_duty(uint8_t index, uint8_t level)
{
    uint32_t ccr = (uint32_t)level * (PWM_PERIOD + 1) / PWM_LEVEL_MAX;

    switch (index)
    {
        case 0: TIM2->CH1CVR = (uint16_t)ccr; break;
        case 1: TIM2->CH2CVR = (uint16_t)ccr; break;
        case 2: TIM2->CH3CVR = (uint16_t)ccr; break;
        default: TIM2->CH4CVR = (uint16_t)ccr; break;
    }
}

static void config_gpio(uint8_t index)
{
    if (pin_mode[index] == PIN_MODE_GPIO)
    {
        return;
    }

    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = pin_mask[index];
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    pin_mode[index] = PIN_MODE_GPIO;
}

static void config_pwm(uint8_t index)
{
    if (pin_mode[index] == PIN_MODE_PWM)
    {
        return;
    }

    GPIO_InitTypeDef gpio = {0};

    pwm_init();
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = pin_mask[index];
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    pin_mode[index] = PIN_MODE_PWM;
}

uint8_t pins_apply(uint8_t address, uint8_t type, uint8_t level)
{
    if (address < PINS_ADDR_FIRST || address > PINS_ADDR_LAST)
    {
        return 0;
    }

    uint8_t index = address - PINS_ADDR_FIRST;

    switch (type)
    {
        case PIN_TYPE_PUSH_PULL:
            config_gpio(index);
            if (level == 0x00)
            {
                GPIO_ResetBits(GPIOA, pin_mask[index]);
            }
            else if (level == 0x01)
            {
                GPIO_SetBits(GPIOA, pin_mask[index]);
            }
            break;

        case PIN_TYPE_PWM:
            config_pwm(index);
            set_duty(index, level);
            break;

        default:
            break; /* reserved */
    }

    return 1;
}
