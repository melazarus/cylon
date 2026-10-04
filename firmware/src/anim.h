#ifndef ANIM_H_
#define ANIM_H_

#include <stdint.h>

/* SysEx address bytes that start an animation instead of setting a static
 * colour. They reuse the <led> <r> <g> <b> record layout:
 *
 *   0x04  blink   at 1 Hz
 *   0x05  blink   at 2 Hz
 *   0x06  breathe at 1 Hz
 *   0x07  breathe at 2 Hz
 *   0x08  Larson scanner (colour = the moving dot)
 *   0x09  colour wheel, one hue offset per LED (r/g/b unused)
 *   0x0A  colour wheel, all LEDs the same hue   (r/g/b unused)
 *
 * Addresses 0x00..0x03 are static colours and stop any running animation. */
#define ANIM_ADDR_FIRST (0x04)
#define ANIM_ADDR_LAST  (0x0A)

/* Initialise the animation time base (a free-running 1 kHz timer). */
void anim_init(void);

/* Start the animation for the given address byte with a base colour. */
void anim_start(uint8_t address, uint8_t r, uint8_t g, uint8_t b);

/* Stop any running animation; the LEDs keep whatever they last showed. */
void anim_stop(void);

/* Draw the current animation frame if one is due. Non-blocking; call it from
 * the main loop. */
void anim_tick(void);

#endif /* ANIM_H_ */
