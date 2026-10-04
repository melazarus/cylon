#ifndef PINS_H_
#define PINS_H_

#include <stdint.h>

/* Auxiliary outputs PA0..PA3, controlled by SysEx addresses 0x10..0x13:
 *
 *   F0 13 37 <addr> <type> <level> <ignored> F7
 *
 *   addr  = 0x10 PA0, 0x11 PA1, 0x12 PA2, 0x13 PA3
 *   type  = 0x00 push-pull output, or 0x01 PWM output
 *   level = push-pull: 0x00 low, 0x01 high
 *           PWM:       0x00 = 0% .. 0x7F = 100%
 *   extra = ignored
 *
 * PA0..PA3 are TIM2 channels 1..4, so a PWM record drives a 20 kHz PWM on that
 * pin. A pin stays an input until its first record; the mode can be switched
 * between push-pull and PWM at any time. */
#define PINS_ADDR_FIRST (0x10)
#define PINS_ADDR_LAST  (0x13)

#define PIN_TYPE_PUSH_PULL (0x00)
#define PIN_TYPE_PWM       (0x01)

/* Apply a pin record. Returns 1 if the address belongs to one of the pins. */
uint8_t pins_apply(uint8_t address, uint8_t type, uint8_t level);

#endif /* PINS_H_ */
