"""Inject the Cylon firmware version into the build.

The version comes from the ``CYLON_VERSION`` environment variable (the release
workflow passes the git tag, e.g. ``v1.2.3``). Local builds fall back to
``DEFAULT_VERSION``. The numbers become the ``VERSION_MAJOR``/``MINOR``/``PATCH``
string macros used by ``lib/CH32X035_USB_MIDI`` for the USB serial number and by
``src/midi.c`` for the command ``0x02`` version reply.

This is a PlatformIO pre-script; SCons provides the global ``env``.
"""

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

import os
import re

DEFAULT_VERSION = "1.1.0"


def parse_version(value: str) -> tuple[str, str, str]:
    """Return (major, minor, patch) from a ``v1.2.3``-style string."""
    match = re.match(r"^v?(\d+)\.(\d+)\.(\d+)", value.strip())
    if not match:
        raise ValueError(value)
    return match.group(1), match.group(2), match.group(3)


raw = os.environ.get("CYLON_VERSION", "").strip() or DEFAULT_VERSION
try:
    major, minor, patch = parse_version(raw)
except ValueError:
    print(f"Cylon: ignoring non-semver CYLON_VERSION={raw!r}, using {DEFAULT_VERSION}")
    major, minor, patch = parse_version(DEFAULT_VERSION)

# The library expects string literals (it calls atoi() on them), so keep the
# escaped quotes that SCons passes through to the compiler.
env.Append(  # noqa: F821
    BUILD_FLAGS=[
        f'-DVERSION_MAJOR=\\"{major}\\"',
        f'-DVERSION_MINOR=\\"{minor}\\"',
        f'-DVERSION_PATCH=\\"{patch}\\"',
    ]
)

print(f"Cylon firmware version {major}.{minor}.{patch}")
