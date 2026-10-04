#ifndef BOARD_H_
#define BOARD_H_

#include <stdint.h>

/* Cylon hardware: 3 WS2812 LEDs driven from PB1 (see board.c) */
#define BOARD_LEDS (3)

/* initialize the LEDs */
void board_init(void);

/* set the color of one LED (0 .. BOARD_LEDS - 1), it is shown by board_show_leds() */
void board_set_led(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void board_show_leds(void);

#endif /* BOARD_H_ */
