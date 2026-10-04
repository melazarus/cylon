# Cylon — protocol & memory map

Reference for the wire protocol and the CH32X035 memory layout used by the
firmware. Source of truth is the code: `firmware/src/midi.c` (protocol),
`board.c`, `anim.c`, `pins.c`, and the PlatformIO SDK headers for the addresses.

---

# Part 1 — USB-MIDI protocol

## 1.1 Transport

- Class-compliant **USB-MIDI** device (no host driver).
- The device listens on **MIDI channel 0** only.
- The device also has a **USB-MIDI IN** endpoint. Command replies (currently
  only the firmware version) are sent back as SysEx, so a host that wants a
  reply must open the matching MIDI input as well as the output.
- **System Exclusive (SysEx)** — full control: static colours, animations,
  auxiliary pins, commands.
- Every byte in a SysEx payload is a **7-bit MIDI data byte** (`0x00`–`0x7F`).

## 1.2 SysEx framing

```
F0 13 37 [ <record> ] ... F7
```

| Byte | Meaning |
| ---- | ------- |
| `F0` | SysEx start |
| `13 37` | protocol tag (a chosen marker, **not** a registered manufacturer ID) |
| records | one or more 4-byte records (below) |
| `F7` | SysEx end |

A single message may carry any number of records; they are applied in order and
the last write wins. The parser also accepts a message split across several
USB-MIDI packets and all four SysEx Code Index Numbers (`0x04`–`0x07`).

## 1.3 Records and the address map

Each record is `<address> <b1> <b2> <b3>` — four 7-bit bytes. The first byte
selects what the record does:

| Address | Target | b1 | b2 | b3 |
| ------- | ------ | -- | -- | -- |
| `0x00` | all LEDs | r | g | b |
| `0x01`–`0x03` | LED 0/1/2 | r | g | b |
| `0x04`–`0x0A` | animation | see §1.5 | | |
| `0x0B` | firmware version reply (device → host) | major | minor | patch |
| `0x0C`–`0x0F` | *reserved* | | | |
| `0x10`–`0x13` | pin PA0–PA3 | type | level | speed / ignored |
| `0x14`–`0x7E` | *reserved* | | | |
| `0x7F` | command | cmd | arg0 | arg1 |

Unused/reserved addresses are ignored.

## 1.4 Static LED colour

```
<led> <r> <g> <b>          led = 0x00 (all) or 0x01..0x03
```

`r`/`g`/`b` are 7-bit; the firmware shifts them left by one, so the LED receives
an **even** 8-bit value and `0x7F` is the maximum (`0xFE`). Setting a static
colour **stops any running animation**.

## 1.5 Animations

Addresses `0x04`–`0x0A` start an animation. The three colour bytes are the base
colour for the first four and the scanner; the colour wheels ignore them.

| Address | Animation | Colour bytes |
| ------- | --------- | ------------ |
| `0x04` | blink at 1 Hz | base colour |
| `0x05` | blink at 2 Hz | base colour |
| `0x06` | breathe at 1 Hz | base colour |
| `0x07` | breathe at 2 Hz | base colour |
| `0x08` | Larson scanner (the moving dot) | base colour |
| `0x09` | colour wheel, one hue offset per LED | ignored |
| `0x0A` | colour wheel, all LEDs same hue | ignored |

Animations are rendered on-device at 50 Hz. Any static colour record (§1.4) stops the running animation.

## 1.6 Auxiliary pins

Addresses `0x10`–`0x13` map to **PA0–PA3**:

| Byte | Meaning |
| ---- | ------- |
| `<type>` | `0x00` push-pull output, `0x01` PWM output, `0x02` servo output |
| `<level>` | push-pull: `0x00` low / `0x01` high<br>PWM: `0x00` = 0 % … `0x7F` = 100 % duty<br>servo: `0x00` = 0 % … `0x7F` = 100 % position |
| `<extra>` | push-pull / PWM: ignored<br>servo: move time — `0x00` = instant, `0x01`–`0x7F` = 100 ms … 12.7 s |

A pin stays an input until its first record and can switch between modes at any
time. Pins are independent of the LEDs and do **not** stop a running animation.

- **PWM** runs at 20 kHz.
- **Servo** drives a standard RC servo: a 50 Hz frame with a 0.5 ms (0 %) to
  2.5 ms (100 %) pulse (configurable with command `0x10`). A non-zero move time
  sweeps the position linearly from where it is now to the new target.
- PA0–PA3 are the four channels of **one timer** (`TIM2`), so PWM (20 kHz) and
  servo (50 Hz) cannot run at once: while at least one servo is active the
  timer runs at 50 Hz and any PWM pin is also 50 Hz (its duty is unchanged).

## 1.7 Commands

Address `0x7F` is a command record `<0x7F> <cmd> <arg0> <arg1>`:

| Command | Meaning | 
| ------- | ------- | 
| `0x01` | reboot into the ROM USB bootloader |
| `0x02` | request firmware version (reply: record `0x0B`) |
| `0x10` | set the servo pulse range (`arg0` = min, `arg1` = max, 20 µs units) |

Unknown commands and non-zero arguments are ignored.

Command `0x02` asks the device to answer over the USB-MIDI IN endpoint with a
normal four-byte record so the host can reuse its record parser:

```
F0 13 37 0B <major> <minor> <patch> F7
```

Each component is a 7-bit number. The current firmware is `1.0.0`, so the
reply is `F0 13 37 0B 01 00 00 F7`. The device answers once per matching `0x02`
record; `arg0` and `arg1` must be zero, like every other command.

Command `0x10` sets the pulse range used by servo pins (type `0x02`). Both
arguments are in **20 µs units** (`0x00`–`0x7F` = 0–2540 µs); the default is
`0x19` (25 → 500 µs) to `0x7D` (125 → 2500 µs). Narrow it if a servo reaches
its mechanical limit before 100 %: a servo that buzzes above ≈2.26 ms can be
limited to `F0 13 37 7F 10 19 71 F7` (500–2260 µs), after which position `0x7F`
is that maximum instead of over-driving the servo. The setting is runtime only
and applies to every servo pin.

## 1.8 Interaction rules

- A **static colour** (SysEx `0x00`–`0x03`) **stops** any running
  animation and holds that colour.
- An **animation** starts (or replaces) the running animation.
- **Pins** never affect the LEDs and never stop an animation.

## 1.9 Examples

```
LED 0 red              F0 13 37 01 7F 00 00 F7
All LEDs green         F0 13 37 00 00 7F 00 F7
LED0 red, LED2 blue    F0 13 37 01 7F 00 00 03 00 00 7F F7
Larson scanner, cyan   F0 13 37 08 00 7F 7F F7
Colour wheel (offset)  F0 13 37 09 00 00 00 F7
PA0 high               F0 13 37 10 00 01 00 F7
PA1 PWM 50%            F0 13 37 11 01 40 00 F7
PA2 servo 50%, 1 s     F0 13 37 12 02 40 0A F7
Reboot to bootloader   F0 13 37 7F 01 00 00 F7
Request firmware ver.  F0 13 37 7F 02 00 00 F7
Version reply (1.0.0)  F0 13 37 0B 01 00 00 F7   (device → host)
```
