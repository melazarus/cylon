/*
    https://github.com/NoNamedCat/CH32X035_USBMIDI
*/

#pragma once

#define CH32X035_ESIG_UNIID1    0x1FFFF7E8
#define CH32X035_ESIG_UNIID2    0x1FFFF7EC
#define CH32X035_ESIG_UNIID3    0x1FFFF7F0

#define WCH_USBMIDI_MANUF_STR            "AREA3001"
#define WCH_USBMIDI_PROD_STR             "CYLON"
#define WCH_USBMIDI_INTERF_STR           "USBMIDI"
#define WCH_USBMIDI_SERIAL_PREFIX        "MD"

#ifndef VERSION_MAJOR
#define VERSION_MAJOR "0"
#endif
#ifndef VERSION_MINOR
#define VERSION_MINOR "0"
#endif
#ifndef VERSION_PATCH
#define VERSION_PATCH "0"
#endif

#define STR_LEN(s) (sizeof(s) - 1)

#define WCH_USBMIDI_MANUF_LEN            STR_LEN(WCH_USBMIDI_MANUF_STR)
#define WCH_USBMIDI_PROD_LEN             STR_LEN(WCH_USBMIDI_PROD_STR)  
#define WCH_USBMIDI_INTERF_LEN           STR_LEN(WCH_USBMIDI_INTERF_STR)
#define WCH_USBMIDI_SERIAL_PREFIX_LEN    STR_LEN(WCH_USBMIDI_SERIAL_PREFIX)
#define WCH_USBMIDI_VERSION_HEX_CHARS    6 /* 2 hex chars each for major, minor, patch */
#define WCH_USBMIDI_UID_HEX_CHARS        24
#define WCH_USBMIDI_SERIAL_LEN           (WCH_USBMIDI_SERIAL_PREFIX_LEN + WCH_USBMIDI_VERSION_HEX_CHARS + WCH_USBMIDI_UID_HEX_CHARS)

/* 0x16C0 is Objective Development's shared VID (V-USB/obdev) and 0x27DD is not
 * assigned to this project. Replace these with a VID/PID you are licensed to
 * use before shipping; both can also be overridden from the build flags, e.g.
 *   -DWCH_USBMIDI_VENDOR_ID=0x1209 -DWCH_USBMIDI_PRODUCT_ID=0x0001 */
#ifndef WCH_USBMIDI_VENDOR_ID
#define WCH_USBMIDI_VENDOR_ID        0x16C0
#endif
#ifndef WCH_USBMIDI_PRODUCT_ID
#define WCH_USBMIDI_PRODUCT_ID       0x27DD
#endif
#define WCH_USBMIDI_DEVICE_VERSION   0x0100
#define WCH_USBMIDI_LANGUAGE         0x0409
/* 3x WS2812 at full white (~180 mA) plus the MCU/USB overhead */
#define WCH_USBMIDI_MAX_POWER_mA     250
