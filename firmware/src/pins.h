#ifndef PINS_H_
#define PINS_H_

#include <stdint.h>

/* Auxiliary outputs PA0..PA3, controlled by SysEx addresses 0x10..0x13:
 *
 *   F0 13 37 <addr> <type> <level> <extra> F7
 *
 *   addr  = 0x10 PA0, 0x11 PA1, 0x12 PA2, 0x13 PA3
 *   type  = 0x00 push-pull output
 *           0x01 PWM output
 *           0x02 servo output
 *   level = push-pull: 0x00 low, 0x01 high
 *           PWM:       0x00 = 0% .. 0x7F = 100% duty
 *           servo:     0x00 = 0% .. 0x7F = 100% position
 *   extra = push-pull/PWM: ignored
 *           servo: move time; 0x00 = instant, 0x01..0x7F = 100 ms .. 12.7 s
 *
 * PA0..PA3 are TIM2 channels 1..4, so they share one time base. PWM runs TIM2
 * at 20 kHz and servo at 50 Hz, so while at least one servo is active any PWM
 * pin also runs at 50 Hz. A pin stays an input until its first record; modes
 * can be switched at any time. */
#define PINS_ADDR_FIRST (0x10)
#define PINS_ADDR_LAST  (0x13)

#define PIN_TYPE_PUSH_PULL (0x00)
#define PIN_TYPE_PWM       (0x01)
#define PIN_TYPE_SERVO     (0x02)

/* Apply a pin record. Returns 1 if the address belongs to one of the pins. */
uint8_t pins_apply(uint8_t address, uint8_t type, uint8_t level, uint8_t extra);

/* Set the servo pulse range for every servo pin.
 *
 * min/max are in 20 us units (0x00..0x7F = 0..2540 us); the default is
 * 25 (500 us) .. 125 (2500 us). Servos whose mechanical range is narrower
 * should be narrowed here so the top of the position range does not drive
 * them against their end stop. Returns 1 if the range was accepted. */
uint8_t pins_set_servo_range(uint8_t min_units, uint8_t max_units);

/* Advance in-progress servo moves. Call from the main loop. */
void pins_tick(void);

#endif /* PINS_H_ */
