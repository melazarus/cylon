# Building, versioning & releasing

How to build the Cylon firmware, choose a version number, and cut a release.
The wire protocol is documented separately in [`PROTOCOL.md`](PROTOCOL.md).

## Lifecycle at a glance

```
local   pio run -e release         -> firmware/.pio/build/release/firmware.bin
tag     git tag v1.1.0 && push     -> .github/workflows/release.yml
CI      build with CYLON_VERSION   -> draft release: firmware.bin, cylon-v1.1.0.bin
publish click "Publish release"    -> .github/workflows/pages.yml bundles the binary
site    version selector           -> WebUSB flash
```

## Prerequisites

- [PlatformIO](https://platformio.org) (the CLI is enough)
- The [Community CH32V platform](https://github.com/Community-PIO-CH32V/platform-ch32v), pinned to a known commit:

```bash
pip install platformio
pio pkg install -g -p "https://github.com/Community-PIO-CH32V/platform-ch32v.git#8cf3b51c0ee537756a04c28336f1466e03424020"
```

The pin (also used by the release workflow) makes builds reproducible. The first
build downloads the WCH none-OS SDK and the RISC-V toolchain, so it takes a
while; later builds are fast.

## Environments

`firmware/platformio.ini` defines two environments:

| Environment | Flags | Notes |
| ----------- | ----- | ----- |
| `debug` | `-DDEBUG=1 -Og -g3 -Wall -Wextra` | serial logging on `USART1` (`PB10`, 115200 8N1) |
| `release` | `-DDEBUG=0 -Os -Wall -Wextra` | drops logging and newlib `printf`; the environment CI builds |

Both must build **warning-free**. The output is
`firmware/.pio/build/<env>/firmware.bin`.

## The firmware version

The version is injected at build time by [`firmware/version.py`](../firmware/version.py)
(a PlatformIO pre-script):

- It reads **`CYLON_VERSION`** from the environment (the release workflow passes
  the git tag, e.g. `v1.1.0`).
- If unset, it falls back to **`DEFAULT_VERSION`** (`1.0.0`).
- It accepts `v<major>.<minor>.<patch>` (the `v` is optional). Anything that
  isn't semver prints a warning and falls back to the default.
- It defines the `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_PATCH` macros used
  by the USB-MIDI library (USB serial number) and by `midi.c` (the `0x02`
  version reply).

Bump `DEFAULT_VERSION` when you release, so a plain local build reports the same
version as the release. CI overrides it from the tag, so the tag is the single
source of truth for released firmware.

```bash
# local build reporting a specific version
CYLON_VERSION=v1.1.0 pio run -e release
```

### Where the version appears

| Place | Format | Example for `1.1.0` |
| ----- | ------ | ------------------- |
| USB serial number | `MD` + 6 hex chars + 24 hex UID | `MD010100…` |
| USB-MIDI `0x02` reply (record `0x0B`) | `F0 13 37 0B <major> <minor> <patch> F7` | `F0 13 37 0B 01 01 00 F7` |
| Release asset | `cylon-<tag>.bin` | `cylon-v1.1.0.bin` |

## Building

```bash
cd firmware
pio run -e release      # or: pio run -e debug
```

## Flashing

See the README's [Flashing](../README.md#flashing) section for the full detail.
In short:

- **USB ISP (normal):** put the board in the ROM bootloader — unplug USB, hold
  BOOT, plug back in — then `pio run -e release -t upload`. The bootloader
  enumerates as `4348:55e0` / `1a86:55e0`.
- **Without the button:** `uv run tools/set_leds.py --boot` (or the web tool's
  checkbox) reboots a running board into the bootloader over MIDI.
- **SWD:** `PC18`/`PC19` via a WCH-LinkE, always available and independent of
  the boot strap.
- **Browser:** the `public/` web tool flashes over WebUSB. On Windows bind the
  bootloader to WinUSB with [Zadig](https://zadig.akeo.ie/); on Linux add the
  udev rule (see the README).

## Versioning

Cylon follows [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`.

| Change | Bump | Example |
| ------ | ---- | ------- |
| **New, backwards-compatible feature** | **MINOR** | `1.0.0 → 1.1.0` — a new animation, command, pin mode |
| **Bug fix only** (no new functionality) | **PATCH** | `1.0.0 → 1.0.1` — fix flicker, correct a colour |
| **Breaking change** to the protocol or behaviour | **MAJOR** | `1.0.0 → 2.0.0` — change an existing record's meaning, remove an address |

Rules of thumb:

- Adding a new command or a new address/record is **MINOR** — existing tools
  keep working.
- Changing what an existing address or command *does*, or removing one, is
  **MAJOR**.
- If a change touches the protocol, update [`PROTOCOL.md`](PROTOCOL.md) in the
  same commit and mention it in the release notes.

## Cutting a release

1. (Optional but tidy) bump `DEFAULT_VERSION` in `firmware/version.py` to the
   new version.
2. Commit and push to `main`.
3. Tag and push:

   ```bash
   git tag -a v1.1.0 -m "Cylon 1.1.0"
   git push origin v1.1.0
   ```

4. [`.github/workflows/release.yml`](../.github/workflows/release.yml) runs:
   installs PlatformIO + the pinned CH32V platform, builds the `release`
   environment with `CYLON_VERSION` set from the tag, and creates a **draft**
   GitHub release with `firmware.bin` and `cylon-<tag>.bin`.
5. Review and **Publish** the draft. Publishing triggers
   [`.github/workflows/pages.yml`](../.github/workflows/pages.yml), which runs
   [`.github/scripts/bundle_firmware.py`](../.github/scripts/bundle_firmware.py)
   to download published release binaries into `public/firmware/` and writes
   `releases.json`; the site's version selector then lists the new version.

Notes:

- Releases are created as **drafts** on purpose — nothing is public (and the
  web updater won't list it) until you publish.
- To re-run a build for the same tag, delete the tag and draft, then re-push:

  ```bash
  git tag -d v1.1.0
  git push origin :refs/tags/v1.1.0
  git tag -a v1.1.0 -m "Cylon 1.1.0"
  git push origin v1.1.0
  ```

- The site serves firmware **same-origin** from `public/firmware/` because
  GitHub's release asset host does not send CORS headers. Don't link the site
  directly at `github.com/…/releases/download/…`.

## Troubleshooting

- **Both environments must build warning-free** (`-Wall -Wextra`); treat warnings
  as errors.
- **Version looks wrong:** check the `Cylon firmware version X.Y.Z` line the
  pre-script prints at the top of the build, and remember a plain local build
  uses `DEFAULT_VERSION`, not the latest tag.
- **Flashed but still the old version:** the board's `0x02` reply and USB serial
  report the *built* version — re-run with the right `CYLON_VERSION`/tag.
- **Firmware update fails:** the device may be left in the bootloader; unplug
  and replug to run the application again, then reflash. A bad flash is always
  recoverable via the BOOT strap or SWD.
