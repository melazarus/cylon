#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pygame"]
# ///
"""
Set the color of the Cylon's three WS2812 LEDs, or drive the auxiliary pins,
over USB MIDI.

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
    uv run tools/set_leds.py --pin 0 --high  # PA0 push-pull high
    uv run tools/set_leds.py --pin 1 --pwm 64          # PA1 PWM, 50 %
    uv run tools/set_leds.py --pin 2 --servo 64 --speed 10  # PA2 servo 50 %, 1 s move
    uv run tools/set_leds.py --servo-range 500 2260        # narrow the servo range
    uv run tools/set_leds.py --list          # list MIDI output ports
    uv run tools/set_leds.py --port cylon 0 0000ff
    uv run tools/set_leds.py --boot          # reboot into the ROM bootloader
    uv run tools/set_leds.py --version       # ask the firmware for its version
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
CMD_GET_VERSION = 0x02
CMD_SET_SERVO_RANGE = 0x10

# The firmware answers a version request with a normal record:
#   F0 13 37 0B <major> <minor> <patch> F7
VERSION_ADDRESS = 0x0B

# Auxiliary pin records: <addr> <type> <level> <extra>, addr 0x10..0x13 = PA0..PA3
PIN_ADDRESS_BASE = 0x10
PIN_TYPE_PUSH_PULL = 0x00
PIN_TYPE_PWM = 0x01
PIN_TYPE_SERVO = 0x02

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


def clamp7(value: int) -> int:
    """Clamp a value to the 7-bit SysEx data range (0..127)."""
    return max(0, min(0x7F, int(value)))


def parse_pin(value: str) -> int:
    """Parse a pin designator: 0-3 or PA0-PA3."""
    text = value.strip().upper()
    if text.startswith("PA"):
        text = text[2:]
    try:
        pin = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("pin must be 0-3 or PA0-PA3")
    if not 0 <= pin <= 3:
        raise argparse.ArgumentTypeError("pin must be 0-3 or PA0-PA3")
    return pin


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


def input_devices() -> list[tuple[int, str]]:
    """Return [(device_id, name)] for every MIDI input device."""
    devices = []
    for device_id in range(pygame.midi.get_count()):
        interface, name, is_input, _is_output, _opened = pygame.midi.get_device_info(
            device_id
        )
        if is_input:
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


def find_input_device(hint: str) -> tuple[int, str]:
    devices = input_devices()
    if not devices:
        raise SystemExit(
            "No MIDI input devices found. Is the Cylon connected and enumerated?"
        )
    for device_id, name in devices:
        if hint.lower() in name.lower():
            return device_id, name
    print(
        f"warning: no MIDI input matching {hint!r}, using {devices[0][1]!r}",
        file=sys.stderr,
    )
    return devices[0]


def _find_version(data: list[int]) -> tuple[int, int, int] | None:
    """Find the F0 13 37 0B <major> <minor> <patch> reply in raw SysEx bytes."""
    for i in range(len(data) - 5):
        if (
            data[i] == MANUFACTURER_ID[0]
            and data[i + 1] == MANUFACTURER_ID[1]
            and data[i + 2] == VERSION_ADDRESS
        ):
            return (data[i + 3], data[i + 4], data[i + 5])
    return None


def read_version(
    midi_input: pygame.midi.Input, timeout: float = 2.0
) -> tuple[tuple[int, int, int] | None, list[list[int]]]:
    """Wait for the device's version reply and return (version, raw events).

    PortMidi packs the SysEx byte stream four bytes at a time into the four
    ``status``/``data`` fields of each event (the leading 0xF0 and trailing
    0xF7 included), so concatenating every event's fields reconstructs the
    message on Windows, Linux and macOS. The raw events are returned so the
    caller can show them when no reply arrives.
    """
    deadline = time.monotonic() + timeout
    data: list[int] = []
    raw: list[list[int]] = []
    while True:
        if midi_input.poll():
            for event, _timestamp in midi_input.read(64):
                raw.append(list(event))
                data.extend(value & 0xFF for value in event)
            version = _find_version(data)
            if version is not None:
                return version, raw
        if time.monotonic() >= deadline:
            return _find_version(data), raw
        time.sleep(0.01)


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
    parser.add_argument(
        "--version",
        action="store_true",
        help="request and print the firmware version",
    )
    parser.add_argument(
        "--pin",
        type=parse_pin,
        metavar="PIN",
        help="auxiliary pin to drive: 0-3 or PA0-PA3",
    )
    parser.add_argument(
        "--high", action="store_true", help="with --pin: drive the pin high"
    )
    parser.add_argument(
        "--low", action="store_true", help="with --pin: drive the pin low"
    )
    parser.add_argument(
        "--pwm",
        type=int,
        metavar="LEVEL",
        help="with --pin: PWM duty, 0-127 (0-100%%)",
    )
    parser.add_argument(
        "--servo",
        type=int,
        metavar="POS",
        help="with --pin: servo position, 0-127 (0-100%%)",
    )
    parser.add_argument(
        "--speed",
        type=int,
        metavar="N",
        help="with --servo: move time in 100 ms units (0 = instant, 1-127)",
    )
    parser.add_argument(
        "--servo-range",
        nargs=2,
        type=int,
        metavar=("MIN_US", "MAX_US"),
        help="set the servo pulse range in microseconds and exit",
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
                "re-enumerate as a WCH ISP device (4348:55e0 / 1a86:55e0)"
            )
            print("now flash with: cd firmware && pio run -e release -t upload")
            return 0

        if args.version:
            output_id, output_name = find_device(args.port)
            input_id, _input_name = find_input_device(args.port)
            output = pygame.midi.Output(output_id)
            midi_input = pygame.midi.Input(input_id)
            try:
                message = [
                    SYSEX_START,
                    *MANUFACTURER_ID,
                    COMMAND_ADDRESS,
                    CMD_GET_VERSION,
                    0x00,
                    0x00,
                    SYSEX_END,
                ]
                # A freshly opened MME input can drop the first reply, so ask
                # again until one arrives (the device answers every request).
                version = None
                raw: list[list[int]] = []
                deadline = time.monotonic() + 3.0
                while version is None and time.monotonic() < deadline:
                    output.write_sys_ex(pygame.midi.time(), message)
                    version, chunk = read_version(midi_input, timeout=0.4)
                    raw.extend(chunk)
            finally:
                midi_input.close()
                output.close()
            if version is None:
                print(
                    f"error: no version reply on MIDI input {_input_name!r}",
                    file=sys.stderr,
                )
                if raw:
                    print(f"received {len(raw)} MIDI event(s):", file=sys.stderr)
                    for event in raw[:16]:
                        print(f"  {' '.join(f'{b:02x}' for b in event)}", file=sys.stderr)
                else:
                    print(
                        "  (the device sent nothing back — check the MIDI input port)",
                        file=sys.stderr,
                    )
                return 1
            print(f"{output_name!r}: firmware {version[0]}.{version[1]}.{version[2]}")
            return 0

        if args.servo_range is not None:
            min_us, max_us = args.servo_range
            min_units = clamp7(round(min_us / 20))
            max_units = clamp7(round(max_us / 20))
            if max_units <= min_units:
                parser.error("--servo-range MIN must be smaller than MAX")
            device_id, name = find_device(args.port)
            output = pygame.midi.Output(device_id)
            try:
                message = [
                    SYSEX_START,
                    *MANUFACTURER_ID,
                    COMMAND_ADDRESS,
                    CMD_SET_SERVO_RANGE,
                    min_units,
                    max_units,
                    SYSEX_END,
                ]
                output.write_sys_ex(pygame.midi.time(), message)
                time.sleep(0.05)
            finally:
                output.close()
            print(f"set servo range {min_units * 20}-{max_units * 20} us on {name!r}")
            return 0

        if args.pin is not None:
            actions = [args.high, args.low, args.pwm is not None, args.servo is not None]
            if sum(actions) != 1:
                parser.error(
                    "--pin requires exactly one of --high, --low, --pwm or --servo"
                )
            if args.speed is not None and args.servo is None:
                parser.error("--speed is only valid with --servo")

            if args.high or args.low:
                level = 0x01 if args.high else 0x00
                record = [PIN_TYPE_PUSH_PULL, level, 0x00]
                description = "high" if args.high else "low"
            elif args.pwm is not None:
                level = clamp7(args.pwm)
                record = [PIN_TYPE_PWM, level, 0x00]
                description = f"PWM {round(level / 127 * 100)}%"
            else:
                position = clamp7(args.servo)
                speed = clamp7(args.speed) if args.speed is not None else 0
                record = [PIN_TYPE_SERVO, position, speed]
                description = f"servo {round(position / 127 * 100)}%"
                if speed:
                    description += f", move {speed * 100} ms"

            device_id, name = find_device(args.port)
            output = pygame.midi.Output(device_id)
            try:
                message = [
                    SYSEX_START,
                    *MANUFACTURER_ID,
                    PIN_ADDRESS_BASE + args.pin,
                    *record,
                    SYSEX_END,
                ]
                output.write_sys_ex(pygame.midi.time(), message)
                time.sleep(0.05)
            finally:
                output.close()
            print(f"set PA{args.pin} {description} on {name!r}")
            return 0

        if args.led is None or args.color is None:
            parser.error(
                "LED and COLOR are required (or use --pin, --list, --boot or --version)"
            )

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
        # Deliberately skip pygame.midi.quit(): Pm_Terminate() can deadlock on
        # Windows after an input stream has been opened. Every stream is closed
        # explicitly above and the process is about to exit, so the OS reclaims
        # PortMidi's resources anyway.
        pass

    hex_color = "".join(f"{value:02x}" for value in color)
    print(f"set LED(s) {label} to #{hex_color} on {name!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
