/* Auxiliary GPIO / PWM / servo outputs PA0..PA3 for SysEx addresses 0x10..0x13.
 *
 * Type 0x00 drives the pin as a plain push-pull GPIO (level 0 = low, 1 = high).
 * Type 0x01 switches the pin to PWM and uses level as a duty cycle from 0x00
 * (0%) to 0x7F (100%). Type 0x02 drives an RC servo: level is the position
 * (0x00..0x7F = 0..100%) and the extra byte is the move time (0x00 = instant,
 * 0x01..0x7F = 100 ms .. 12.7 s).
 *
 * PA0..PA3 are TIM2_CH1..CH4 and share a single time base. PWM wants 20 kHz,
 * a servo wants a 0.5..2.5 ms pulse every 20 ms (50 Hz), so the timer is
 * reconfigured whenever the active modes change: it runs at 20 kHz while only
 * GPIO/PWM pins are in use and at 50 Hz as soon as one servo is active. A PWM
 * record applied while a servo is active is still honoured as a duty
 * percentage, but at 50 Hz.
 *
 * Servo moves are interpolated in pins_tick() from the millisecond clock in
 * anim.c, so a move time produces a smooth sweep rather than a jump.
 */
#include <ch32x035.h>

#include "anim.h"
#include "pins.h"

#define PWM_PERIOD    (2399u)  /* 48 MHz / 2400 = 20 kHz */
#define SERVO_PSC     (47u)    /* 48 MHz / 48 = 1 MHz, so 1 tick = 1 us */
#define SERVO_PERIOD  (19999u) /* 20000 us = 20 ms = 50 Hz */
#define LEVEL_MAX     (0x7Fu)  /* 7-bit level: 0x00..0x7F = 0..100% */

/* Servo pulse range, configurable through pins_set_servo_range(). Values are
 * in 20 us units so 0.5..2.5 ms fits in a 7-bit byte. */
#define SERVO_RANGE_STEP_US   (20u)
#define SERVO_RANGE_MIN_UNITS (25u)  /* 500 us */
#define SERVO_RANGE_MAX_UNITS (125u) /* 2500 us */

enum
{
    PIN_MODE_INPUT = 0,
    PIN_MODE_GPIO,
    PIN_MODE_PWM,
    PIN_MODE_SERVO,
};

enum
{
    TIMER_MODE_PWM = 0,
    TIMER_MODE_SERVO,
};

static const uint16_t pin_mask[PINS_ADDR_LAST - PINS_ADDR_FIRST + 1] = {
    GPIO_Pin_0,
    GPIO_Pin_1,
    GPIO_Pin_2,
    GPIO_Pin_3,
};

typedef struct
{
    uint8_t mode;
    uint8_t duty;    /* last PWM level, kept so it survives a timer switch */
    uint8_t current; /* servo position currently driven */
    uint8_t target;  /* servo position being moved to */
    uint8_t start;   /* servo position the current move started from */
    uint32_t move_start_ms;
    uint32_t move_ms; /* 0 = jump straight to the target */
} pin_state_t;

static pin_state_t pin_state[PINS_ADDR_LAST - PINS_ADDR_FIRST + 1];
static uint8_t timer_mode = TIMER_MODE_PWM;
static uint8_t servo_count = 0;
static uint8_t timer_ready = 0;
static uint32_t servo_min_us = SERVO_RANGE_MIN_UNITS * SERVO_RANGE_STEP_US;
static uint32_t servo_max_us = SERVO_RANGE_MAX_UNITS * SERVO_RANGE_STEP_US;

static void write_ccr(uint8_t index, uint32_t ccr)
{
    switch (index)
    {
        case 0: TIM2->CH1CVR = (uint16_t)ccr; break;
        case 1: TIM2->CH2CVR = (uint16_t)ccr; break;
        case 2: TIM2->CH3CVR = (uint16_t)ccr; break;
        default: TIM2->CH4CVR = (uint16_t)ccr; break;
    }
}

static uint32_t timer_period(void)
{
    return timer_mode == TIMER_MODE_SERVO ? SERVO_PERIOD : PWM_PERIOD;
}

static void set_duty(uint8_t index, uint8_t level)
{
    pin_state[index].duty = level;
    write_ccr(index, (uint32_t)level * (timer_period() + 1) / LEVEL_MAX);
}

static void set_servo_pulse(uint8_t index, uint8_t position)
{
    uint32_t span = servo_max_us - servo_min_us;
    write_ccr(index, servo_min_us + (uint32_t)position * span / LEVEL_MAX);
}

/* Re-apply every active output for the current time base. */
static void reapply_outputs(void)
{
    for (uint8_t i = 0; i < PINS_ADDR_LAST - PINS_ADDR_FIRST + 1; i++)
    {
        if (pin_state[i].mode == PIN_MODE_PWM)
        {
            set_duty(i, pin_state[i].duty);
        }
        else if (pin_state[i].mode == PIN_MODE_SERVO)
        {
            set_servo_pulse(i, pin_state[i].current);
        }
    }
}

static void timer_config(uint8_t mode)
{
    TIM_TimeBaseInitTypeDef tb = {0};

    if (timer_mode == mode)
    {
        return;
    }
    timer_mode = mode;

    tb.TIM_Prescaler = (mode == TIMER_MODE_SERVO) ? SERVO_PSC : 0;
    tb.TIM_Period = (mode == TIMER_MODE_SERVO) ? SERVO_PERIOD : PWM_PERIOD;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tb); /* also reloads PSC/ARR immediately */

    reapply_outputs();
}

/* Bring up TIM2 and its four channels (once). */
static void timer_init(void)
{
    TIM_TimeBaseInitTypeDef tb = {0};
    TIM_OCInitTypeDef oc = {0};

    if (timer_ready)
    {
        return;
    }
    timer_ready = 1;

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

    /* load a new compare value at the next update, so changes are glitch-free */
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);

    /* the channel outputs are gated by the timer's master output enable; without
     * this the pins stay in their idle (low) state */
    TIM_CtrlPWMOutputs(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

static void config_gpio(uint8_t index)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = pin_mask[index];
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
}

static void config_af(uint8_t index)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = pin_mask[index];
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
}

uint8_t pins_apply(uint8_t address, uint8_t type, uint8_t level, uint8_t extra)
{
    if (address < PINS_ADDR_FIRST || address > PINS_ADDR_LAST)
    {
        return 0;
    }

    uint8_t index = address - PINS_ADDR_FIRST;
    uint8_t previous = pin_state[index].mode;

    timer_init();

    switch (type)
    {
        case PIN_TYPE_PUSH_PULL:
            if (previous == PIN_MODE_SERVO)
            {
                servo_count--;
            }
            pin_state[index].mode = PIN_MODE_GPIO;
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
            if (previous == PIN_MODE_SERVO)
            {
                servo_count--;
            }
            pin_state[index].mode = PIN_MODE_PWM;
            config_af(index);
            set_duty(index, level);
            break;

        case PIN_TYPE_SERVO:
            if (previous != PIN_MODE_SERVO)
            {
                servo_count++;
            }
            pin_state[index].mode = PIN_MODE_SERVO;
            config_af(index);
            pin_state[index].start = pin_state[index].current;
            pin_state[index].target = level;
            pin_state[index].move_start_ms = anim_millis();
            pin_state[index].move_ms = (uint32_t)extra * 100u;
            if (pin_state[index].move_ms == 0)
            {
                pin_state[index].current = level;
            }
            set_servo_pulse(index, pin_state[index].current);
            break;

        default:
            return 1; /* reserved type: acknowledged but ignored */
    }

    /* Servo needs 50 Hz; everything else prefers 20 kHz. */
    if (servo_count > 0)
    {
        timer_config(TIMER_MODE_SERVO);
    }
    else
    {
        timer_config(TIMER_MODE_PWM);
    }

    return 1;
}

uint8_t pins_set_servo_range(uint8_t min_units, uint8_t max_units)
{
    uint32_t min_us = (uint32_t)min_units * SERVO_RANGE_STEP_US;
    uint32_t max_us = (uint32_t)max_units * SERVO_RANGE_STEP_US;

    if (max_us <= min_us || max_us > SERVO_PERIOD)
    {
        return 0;
    }

    servo_min_us = min_us;
    servo_max_us = max_us;

    /* re-drive any servo that is already holding a position */
    for (uint8_t i = 0; i < PINS_ADDR_LAST - PINS_ADDR_FIRST + 1; i++)
    {
        if (pin_state[i].mode == PIN_MODE_SERVO)
        {
            set_servo_pulse(i, pin_state[i].current);
        }
    }

    return 1;
}

void pins_tick(void)
{
    uint32_t now = anim_millis();

    for (uint8_t i = 0; i < PINS_ADDR_LAST - PINS_ADDR_FIRST + 1; i++)
    {
        if (pin_state[i].mode != PIN_MODE_SERVO || pin_state[i].current == pin_state[i].target)
        {
            continue;
        }

        if (pin_state[i].move_ms == 0 || (now - pin_state[i].move_start_ms) >= pin_state[i].move_ms)
        {
            pin_state[i].current = pin_state[i].target;
        }
        else
        {
            int32_t delta = (int32_t)pin_state[i].target - (int32_t)pin_state[i].start;
            int32_t elapsed = (int32_t)(now - pin_state[i].move_start_ms);
            int32_t step = delta * elapsed / (int32_t)pin_state[i].move_ms;
            pin_state[i].current = (uint8_t)((int32_t)pin_state[i].start + step);
        }

        set_servo_pulse(i, pin_state[i].current);
    }
}
