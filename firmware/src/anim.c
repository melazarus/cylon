/* Animation engine for the Cylon's three WS2812 LEDs.
 *
 * SysEx records with address bytes 0x04..0x0A start an animation; a static
 * colour record (0x00..0x03) stops it. anim_tick() is
 * called from the main loop and redraws the strip at a fixed frame rate.
 *
 * Timing comes from TIM3 run as a free-running 1 kHz counter, so no interrupts
 * are needed. Several LEDs' worth of animations are cheap: one frame is a few
 * dozen integer operations plus the ~215 us LED transfer.
 */
#include <ch32x035.h>

#include "anim.h"
#include "board.h"

#define ANIM_FRAME_MS  (20)    /* redraw at 50 Hz */
#define BLINK_1HZ_MS   (1000)
#define BLINK_2HZ_MS   (500)
#define BREATHE_1HZ_MS (1000)
#define BREATHE_2HZ_MS (500)
#define LARSON_STEP_MS (200)   /* ms for the dot to travel one LED */
#define LARSON_SPREAD  (384)   /* trail half-width, in 1/256 LED units (1.5 LEDs) */
#define WHEEL_ROT_MS   (3000)  /* full 360 deg rotation */

enum
{
    ANIM_NONE = 0,
    ANIM_BLINK_1HZ,
    ANIM_BLINK_2HZ,
    ANIM_BREATHE_1HZ,
    ANIM_BREATHE_2HZ,
    ANIM_LARSON,
    ANIM_WHEEL_OFFSET,
    ANIM_WHEEL_UNIFORM,
};

/* raised cosine (1 - cos(2*pi*i/64)) / 2 * 255, i.e. one smooth fade in/out */
static const uint8_t breathe_lut[64] = {
    0, 1, 2, 5, 10, 15, 21, 29, 37, 47, 57, 67, 79, 90, 103, 115,
    127, 140, 152, 165, 176, 188, 198, 208, 218, 226, 234, 240, 245, 250, 253, 254,
    255, 254, 253, 250, 245, 240, 234, 226, 218, 208, 198, 188, 176, 165, 152, 140,
    128, 115, 103, 90, 79, 67, 57, 47, 37, 29, 21, 15, 10, 5, 2, 1,
};

static uint8_t anim_mode = ANIM_NONE;
static uint8_t base_r, base_g, base_b;
static uint32_t start_ms;
static uint32_t last_render_ms;

/* --- 32-bit millisecond clock from the free-running 16-bit TIM3 counter --- */
static uint16_t tick_last;
static uint32_t tick_high;

static uint32_t anim_millis(void)
{
    uint16_t now = TIM3->CNT;

    if (now < tick_last)
    {
        tick_high += 0x10000u; /* the 16-bit counter wrapped */
    }
    tick_last = now;

    return tick_high + now;
}

void anim_init(void)
{
    TIM_TimeBaseInitTypeDef tb = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    tb.TIM_Prescaler = (SystemCoreClock / 1000) - 1; /* 1 tick = 1 ms */
    tb.TIM_Period = 0xFFFF;                          /* wraps every 65.5 s */
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &tb);
    TIM_Cmd(TIM3, ENABLE);
}

void anim_stop(void)
{
    anim_mode = ANIM_NONE;
}

void anim_start(uint8_t address, uint8_t r, uint8_t g, uint8_t b)
{
    switch (address)
    {
        case 0x04: anim_mode = ANIM_BLINK_1HZ;     break;
        case 0x05: anim_mode = ANIM_BLINK_2HZ;     break;
        case 0x06: anim_mode = ANIM_BREATHE_1HZ;   break;
        case 0x07: anim_mode = ANIM_BREATHE_2HZ;   break;
        case 0x08: anim_mode = ANIM_LARSON;        break;
        case 0x09: anim_mode = ANIM_WHEEL_OFFSET;  break;
        case 0x0A: anim_mode = ANIM_WHEEL_UNIFORM; break;
        default:   return;
    }

    base_r = r;
    base_g = g;
    base_b = b;

    start_ms = anim_millis();
    last_render_ms = start_ms - ANIM_FRAME_MS; /* draw on the very next tick */
}

/* 8-bit multiply: (c * scale) / 255 */
static uint8_t scale8(uint8_t c, uint8_t scale)
{
    return (uint8_t)(((uint16_t)c * scale) >> 8);
}

/* HSV hue (0..255 = 0..360 deg) at full saturation/value -> RGB */
static void hue_to_rgb(uint8_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = (uint8_t)(hue / 43);              /* 0..5 */
    uint8_t rem = (uint8_t)((hue - region * 43) * 6);  /* 0..255 */
    uint8_t p = 0;
    uint8_t q = (uint8_t)(255 - rem);
    uint8_t t = rem;

    switch (region)
    {
        case 0:  *r = 255; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = 255; *b = p;   break;
        case 2:  *r = p;   *g = 255; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = 255; break;
        case 4:  *r = t;   *g = p;   *b = 255; break;
        default: *r = 255; *g = p;   *b = q;   break;
    }
}

/* the base colour at a given brightness (0..255) on every LED */
static void render_solid(uint8_t level)
{
    for (int i = 0; i < BOARD_LEDS; i++)
    {
        board_set_led(i, scale8(base_r, level), scale8(base_g, level), scale8(base_b, level));
    }
}

static void render_blink(uint32_t t, uint32_t period)
{
    uint8_t on = (t % period) < (period / 2); /* 50% duty */
    render_solid(on ? 255 : 0);
}

static void render_breathe(uint32_t t, uint32_t period)
{
    uint8_t idx = (uint8_t)((t % period) * 64 / period);
    render_solid(breathe_lut[idx]);
}

static void render_larson(uint32_t t)
{
    if (BOARD_LEDS < 2)
    {
        render_solid(255);
        return;
    }

    const uint32_t span = (BOARD_LEDS - 1) * LARSON_STEP_MS; /* ms per sweep */
    uint32_t phase = t % (2 * span);
    uint32_t pos = (phase < span) ? phase : (2 * span - phase); /* bounce */
    pos = pos * (BOARD_LEDS - 1) * 256 / span;                  /* 0 .. (N-1)*256 */

    for (int i = 0; i < BOARD_LEDS; i++)
    {
        int32_t d = (int32_t)pos - (int32_t)i * 256;
        if (d < 0)
        {
            d = -d;
        }

        int32_t s = 256 - (d * 256) / LARSON_SPREAD;
        if (s < 0)
        {
            s = 0;
        }
        else if (s > 255)
        {
            s = 255;
        }

        uint8_t level = (uint8_t)s;
        board_set_led(i, scale8(base_r, level), scale8(base_g, level), scale8(base_b, level));
    }
}

static void render_wheel(uint32_t t, uint8_t offset_per_led)
{
    uint8_t base_hue = (uint8_t)((t % WHEEL_ROT_MS) * 256 / WHEEL_ROT_MS);

    for (int i = 0; i < BOARD_LEDS; i++)
    {
        uint8_t hue = base_hue;
        if (offset_per_led)
        {
            hue = (uint8_t)(hue + i * 256 / BOARD_LEDS);
        }

        uint8_t r, g, b;
        hue_to_rgb(hue, &r, &g, &b);
        board_set_led(i, r, g, b);
    }
}

void anim_tick(void)
{
    if (anim_mode == ANIM_NONE)
    {
        return;
    }

    uint32_t now = anim_millis();
    if ((uint32_t)(now - last_render_ms) < ANIM_FRAME_MS)
    {
        return;
    }
    last_render_ms = now;

    uint32_t t = now - start_ms;

    switch (anim_mode)
    {
        case ANIM_BLINK_1HZ:     render_blink(t, BLINK_1HZ_MS);     break;
        case ANIM_BLINK_2HZ:     render_blink(t, BLINK_2HZ_MS);     break;
        case ANIM_BREATHE_1HZ:   render_breathe(t, BREATHE_1HZ_MS); break;
        case ANIM_BREATHE_2HZ:   render_breathe(t, BREATHE_2HZ_MS); break;
        case ANIM_LARSON:        render_larson(t);                  break;
        case ANIM_WHEEL_OFFSET:  render_wheel(t, 1);                break;
        case ANIM_WHEEL_UNIFORM: render_wheel(t, 0);                break;
        default:                 return;
    }

    board_show_leds();
}
