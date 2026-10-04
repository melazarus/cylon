#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pygame"]
# ///
"""
Set the color of the Cylon's three WS2812 LEDs over USB MIDI.

The firmware listens for a SysEx message of the form

    F0 13 37 <led> <r> <g> <b> F7

where <led> is 0x00 to address all LEDs, or 0x01, 0x02, 0x03 for LED 0/1/2,
and <r>/<g>/<b> are 7-bit MIDI data bytes (the firmware shifts them left by
one, so 0x7F is full brightness).

Usage (uv installs pygame automatically):

    uv run tools/set_leds.py 0 ff0000        # LED 0 -> red
    uv run tools/set_leds.py 1 00ff00        # LED 1 -> green
    uv run tools/set_leds.py 2 0000ff        # LED 2 -> blue
    uv run tools/set_leds.py all ffffff      # all LEDs -> white
    uv run tools/set_leds.py 0 red           # named color
    uv run tools/set_leds.py --list          # list MIDI output ports
    uv run tools/set_leds.py --port cylon 0 0000ff
    uv run tools/set_leds.py --boot          # reboot into the ROM bootloader
"""

from __future__ import annotations

# Suppress the "Hello from the pygame community" banner.
import os

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

import argparse
import re
import sys
import time

import pygame
import pygame.midi

# SysEx manufacturer id used by the firmware (0x1337)
MANUFACTURER_ID = (0x13, 0x37)

# LED address byte: 0x00 = all LEDs, 0x01.. = LED 0.., 0x7F = command
LED_ADDRESS_ALL = 0x00
LED_ADDRESS_BASE = 0x01

SYSEX_START = 0xF0
SYSEX_END = 0xF7

# A SysEx record whose address byte is 0x7F is a command rather than an LED:
#   F0 13 37 7F <cmd> 00 00 F7
COMMAND_ADDRESS = 0x7F
CMD_ENTER_BOOTLOADER = 0x01

NAMED_COLORS = {
    "off": "000000",
    "black": "000000",
    "red": "ff0000",
    "green": "00ff00",
    "blue": "0000ff",
    "white": "ffffff",
    "yellow": "ffff00",
    "cyan": "00ffff",
    "magenta": "ff00ff",
    "orange": "ff8000",
    "purple": "8000ff",
    "pink": "ff80c0",
}


def parse_color(value: str) -> tuple[int, int, int]:
    """Parse '#RRGGBB', 'RRGGBB' or a named color into 8-bit (r, g, b)."""
    text = value.strip().lower()
    text = NAMED_COLORS.get(text, text).lstrip("#")
    if not re.fullmatch(r"[0-9a-f]{6}", text):
        raise argparse.ArgumentTypeError(
            f"invalid color {value!r}: use RRGGBB hex or one of "
            + ", ".join(sorted(NAMED_COLORS))
        )
    red, green, blue = (int(text[i : i + 2], 16) for i in (0, 2, 4))
    return (red, green, blue)


def scale7(color: tuple[int, int, int]) -> tuple[int, int, int]:
    """Scale 8-bit color down to the 7-bit SysEx data range."""
    return tuple(value >> 1 for value in color)


def output_devices() -> list[tuple[int, str]]:
    """Return [(device_id, name)] for every MIDI output device."""
    devices = []
    for device_id in range(pygame.midi.get_count()):
        interface, name, _is_input, is_output, _opened = pygame.midi.get_device_info(
            device_id
        )
        if is_output:
            devices.append((device_id, name.decode(errors="replace")))
    return devices


def find_device(hint: str) -> tuple[int, str]:
    devices = output_devices()
    if not devices:
        raise SystemExit(
            "No MIDI output devices found. Is the Cylon connected and enumerated?"
        )
    for device_id, name in devices:
        if hint.lower() in name.lower():
            return device_id, name
    # Fall back to the first output device if the hint does not match anything.
    print(
        f"warning: no MIDI output matching {hint!r}, using {devices[0][1]!r}",
        file=sys.stderr,
    )
    return devices[0]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set the color of the Cylon LEDs over USB MIDI."
    )
    parser.add_argument("led", nargs="?", help="which LED to set: 0, 1, 2 or 'all'")
    parser.add_argument(
        "color",
        nargs="?",
        help="color as RRGGBB hex or a name (red, green, blue, ...)",
    )
    parser.add_argument(
        "--port",
        default="cylon",
        help="MIDI output name (substring match), default 'cylon'",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="list the available MIDI output ports and exit",
    )
    parser.add_argument(
        "--boot",
        action="store_true",
        help="reset the board into its ROM USB bootloader (WCH ISP) and exit",
    )
    args = parser.parse_args()

    pygame.midi.init()
    try:
        if args.list:
            for device_id, name in output_devices():
                print(f"{device_id}: {name}")
            return 0

        if args.boot:
            device_id, name = find_device(args.port)
            output = pygame.midi.Output(device_id)
            try:
                message = [
                    SYSEX_START,
                    *MANUFACTURER_ID,
                    COMMAND_ADDRESS,
                    CMD_ENTER_BOOTLOADER,
                    0x00,
                    0x00,
                    SYSEX_END,
                ]
                output.write_sys_ex(pygame.midi.time(), message)
                # let the driver flush before the device drops off the bus
                time.sleep(0.05)
            finally:
                output.close()
            print(
                f"asked {name!r} to reboot into its ROM bootloader; it will "
                "re-enumerate as a WCH ISP device (1A86:8010)"
            )
            print("now flash with: cd firmware && pio run -e release -t upload")
            return 0

        if args.led is None or args.color is None:
            parser.error("LED and COLOR are required (or use --list or --boot)")

        try:
            color = parse_color(args.color)
        except argparse.ArgumentTypeError as exc:
            parser.error(str(exc))

        if args.led.lower() == "all":
            addresses = [LED_ADDRESS_ALL]
            label = "all"
        else:
            try:
                led = int(args.led)
            except ValueError:
                parser.error("LED must be 0, 1, 2 or 'all'")
            if not 0 <= led <= 2:
                parser.error("LED must be 0, 1, 2 or 'all'")
            addresses = [LED_ADDRESS_BASE + led]
            label = str(led)

        device_id, name = find_device(args.port)
        output = pygame.midi.Output(device_id)
        try:
            for address in addresses:
                message = [
                    SYSEX_START,
                    *MANUFACTURER_ID,
                    address,
                    *scale7(color),
                    SYSEX_END,
                ]
                output.write_sys_ex(pygame.midi.time(), message)
            # give the driver a moment to flush before closing the port
            time.sleep(0.05)
        finally:
            output.close()
    finally:
        pygame.midi.quit()

    hex_color = "".join(f"{value:02x}" for value in color)
    print(f"set LED(s) {label} to #{hex_color} on {name!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
