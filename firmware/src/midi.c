/* USB-MIDI LED controller: the host sets the LEDs, animations and auxiliary
 * pins with SysEx records. All other MIDI messages are ignored.
 */
#include <wch_usbmidi_internal.h>

#include "anim.h"
#include "board.h"
#include "midi.h"
#include "pins.h"

/* USB-MIDI Code Index Numbers (CIN), USB MIDI spec table 4-1 */
#define MIDI_CIN_SYSEX_START_CONT (0x04) /* SysEx starts or continues */
#define MIDI_CIN_SYSEX_END_1BYTE  (0x05) /* SysEx ends with 1 byte, or 1-byte System Common */
#define MIDI_CIN_SYSEX_END_2BYTE  (0x06) /* SysEx ends with 2 bytes, or empty SysEx */
#define MIDI_CIN_SYSEX_END_3BYTE  (0x07) /* SysEx ends with 3 bytes */
#define MIDI_CIN_MASK             (0x0F)

#define MIDI_STATUS_BIT  (0x80) /* set on every status byte, clear on every data byte */
#define MIDI_SYSEX_START (0xF0)
#define MIDI_SYSEX_END   (0xF7)

/* SysEx manufacturer ID identifying our LED protocol (0x1337) */
#define SYSEX_MANUFACTURER_ID_1 (0x13)
#define SYSEX_MANUFACTURER_ID_2 (0x37)

/* SysEx record address byte:
 *   0x00        all LEDs (broadcast)
 *   0x01..0x03  LED 0..2 (LED index = address - 1)
 *   0x04..0x0A  animations (see anim.h)
 *   0x10..0x13  auxiliary pins (see pins.h)
 *   0x7F        command record <0x7F> <cmd> <arg0> <arg1>; command 0x01 resets
 *               into the ROM USB bootloader so the host can reflash without
 *               touching the BOOT strap. */
#define ADDR_ALL                    (0x00)
#define ADDR_LED_BASE               (0x01)
#define SYSEX_ADDR_COMMAND          (0x7F)
#define SYSEX_CMD_ENTER_BOOTLOADER  (0x01)

/* set when a MIDI message changed the LED colors; the main loop shows them */
static uint8_t flag_update_leds = 0;

/* SysEx message assembly */
#define MAX_SYSEX_DATA ((4 * BOARD_LEDS) + 8)
static uint8_t in_sysex = 0;
static uint8_t sysex_data[MAX_SYSEX_DATA];
static int sysex_data_len = 0;

/* Store one colour for the addressed LED: 0x00 writes all LEDs, 0x01.. writes
 * a single one. Returns 1 if the address matched an LED, 0 otherwise. */
static uint8_t apply_color(uint8_t address, uint8_t r, uint8_t g, uint8_t b)
{
    if (address == ADDR_ALL)
    {
        anim_stop();
        for (int i = 0; i < BOARD_LEDS; i++)
        {
            board_set_led(i, r, g, b);
        }
        return 1;
    }

    if (address < ADDR_LED_BASE || (address - ADDR_LED_BASE) >= BOARD_LEDS)
    {
        return 0;
    }

    anim_stop();
    board_set_led(address - ADDR_LED_BASE, r, g, b);
    return 1;
}

/* Set the flash boot-mode flag and reset. The chip restarts into the factory
 * ROM USB bootloader (WCH ISP, 1A86:8010) instead of the user application, so
 * the host can flash new firmware without the BOOT strap. Never returns. */
static void enter_bootloader(void)
{
    SystemReset_StartMode(Start_Mode_BOOT);
    NVIC_SystemReset();
    while (1)
    {
        /* not reached: NVIC_SystemReset() resets the core */
    }
}

/* apply a complete SysEx message: F0 13 37 [<led> <r> <g> <b>]... F7 */
static void finalize_sysex(void)
{
    if (sysex_data_len >= 8 && (sysex_data_len % 4) == 0 &&
        sysex_data[1] == SYSEX_MANUFACTURER_ID_1 &&
        sysex_data[2] == SYSEX_MANUFACTURER_ID_2)
    {
        for (int i = 3; (i + 4) < sysex_data_len && sysex_data[i] != MIDI_SYSEX_END; i += 4)
        {
            if (sysex_data[i] == SYSEX_ADDR_COMMAND)
            {
                if (sysex_data[i + 1] == SYSEX_CMD_ENTER_BOOTLOADER &&
                    sysex_data[i + 2] == 0 && sysex_data[i + 3] == 0)
                {
                    enter_bootloader();
                }
                continue;
            }

            /* SysEx data bytes are 7-bit (0-127) */
            uint8_t address = sysex_data[i];
            uint8_t raw1 = sysex_data[i + 1];
            uint8_t raw2 = sysex_data[i + 2];
            uint8_t raw3 = sysex_data[i + 3];

            if (address >= ANIM_ADDR_FIRST && address <= ANIM_ADDR_LAST)
            {
                /* colours shift left to use the full 8-bit LED range (0-254) */
                anim_start(address, raw1 << 1, raw2 << 1, raw3 << 1);
            }
            else if (pins_apply(address, raw1, raw2))
            {
                /* auxiliary pin record: applied, nothing to show */
            }
            else if (apply_color(address, raw1 << 1, raw2 << 1, raw3 << 1))
            {
                flag_update_leds = 1;
            }
        }
    }

    in_sysex = 0;
    sysex_data_len = 0;
}

static void handle_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    switch (cin)
    {
        case MIDI_CIN_SYSEX_START_CONT:
            if (b1 == MIDI_SYSEX_START)
            {
                sysex_data_len = 0;
                in_sysex = 1;
            }

            /* accumulate, but only if b1 is a data byte (not a status byte) */
            if (((in_sysex && !(b1 & MIDI_STATUS_BIT)) || b1 == MIDI_SYSEX_START) &&
                sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
            }
            break;

        case MIDI_CIN_SYSEX_END_1BYTE:
            if (in_sysex && b1 == MIDI_SYSEX_END && sysex_data_len < MAX_SYSEX_DATA)
            {
                sysex_data[sysex_data_len++] = b1;
                finalize_sysex();
            }
            break;

        case MIDI_CIN_SYSEX_END_2BYTE:
            if (in_sysex && b2 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 1))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                finalize_sysex();
            }
            else if (b1 == MIDI_SYSEX_START && !in_sysex)
            {
                /* empty SysEx (F0 F7) */
                in_sysex = 0;
                sysex_data_len = 0;
            }
            break;

        case MIDI_CIN_SYSEX_END_3BYTE:
            if (in_sysex && b3 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
                finalize_sysex();
            }
            break;

        default:
            /* other MIDI messages are ignored */
            break;
    }
}

/* never returns */
void midi_run(void)
{
    uint8_t midi_pkt[4];

    while (1)
    {
        /* drain every complete packet the host has queued, then refresh the
         * LEDs at most once per burst: a frame blocks for ~0.5 ms, so showing
         * once avoids paying that cost per individual message */
        while (USB_available() >= 4 && USB_read(midi_pkt, 4) == 4)
        {
            handle_midi(midi_pkt[0] & MIDI_CIN_MASK, midi_pkt[1], midi_pkt[2], midi_pkt[3]);
        }

        if (flag_update_leds)
        {
            flag_update_leds = 0;
            board_show_leds();
        }

        anim_tick();
    }
}
