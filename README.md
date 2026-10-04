# Cylon

> A tiny open source / open hardware USB notification light — three side-emitting addressable LEDs on a USB-A stick, driven by a CH32X035 RISC-V MCU.

![Cylon board render](images/render_front.png)

Cylon plugs straight into a USB-A port and gives you a programmable RGB indicator for build status, notifications, alerts, ambient lighting, etc. It is designed to be cheap, hackable, and easy to manufacture — the USB-A plug is formed by the PCB's own gold-finger edge connector, so there is no connector to solder.

## Features

- **USB-A PCB plug** — no cable or enclosure required, plugs directly into a host port.
- **CH32X035F8U6** — WCH RISC-V MCU in a small WQFN-20 package, with native USB 2.0 Full-Speed.
- **3× side-emitting WS2812B** (`XL-4020RGBC-2812B`) — individually addressable RGB, arranged so light spills out sideways/around the board. (Note the mechanical strength of these LED's is not ideal, apply a small amount of super glue or use different LED's to improve)
- **USB bootloader + SWD** — flash over USB or via a WCH-LinkE / SWD probe.
- **Boot button** — enter the ROM bootloader without unplugging or shorting pads.
- **Minimal BOM** — just 7 components to order, all available from LCSC.
- **Open hardware** — full EasyEDA Pro project, schematic, PCB, Gerbers, and 3D model included.
- **4 GPIO's** - For extra GPIO's for your pleasure.

## Hardware

| | |
|---|---|
| **MCU** | WCH CH32X035F8U6 (RISC-V, WQFN-20) |
| **USB** | USB 2.0 Full-Speed device (USB-A male PCB plug) |
| **LEDs** | 3× XL-4020RGBC-2812B (WS2812B-compatible, side-emitting) |
| **LED data** | `PB3` → LED1 `DIN`, chained LED1 → LED2 → LED3 |
| **USB D+/D−** | `PC16` (`UDP`) / `PC17` (`UDM`) |
| **Debug** | SWD on `PC18` (`DIO`) / `PC19` (`DCK`) |
| **Boot** | `TS-1088-AR02016` tact switch + 5.1 kΩ resistor |
| **Power** | 5 V from USB, 10 µF bulk + 100 nF decoupling per LED/MCU |
| **Board** | 2-layer, ~23 × 19 mm (incl. USB plug tab), EasyEDA Pro, rev V1.0 |

### Pinout

| Signal | CH32X035 pin |
|---|---|
| WS2812 data out | `PB3` |
| USB `D+` | `PC16` |
| USB `D−` | `PC17` |
| SWD `DIO` | `PC18` |
| SWD `DCK` | `PC19` |
| PAD0 | `PA0` |
| PAD1 | `PA1` |
| PAD2 | `PA2` |
| PAD3 | `PA3` |
| `5V0` | `VDD` |
| `GND` | `GND` / EP |

> The WS2812 data line is connected to the first LED in the chain. Each LED's `DO` feeds the next LED's `DI`, so all three are driven from a single GPIO.

## Repository layout

```
.
├── hardware/
│   ├── EasyEdaProDoc.epro2   # EasyEDA Pro project (schematic + PCB)
│   ├── schematic.pdf         # Schematic export
│   ├── PCB.pdf               # PCB layout export
│   ├── gerber.zip            # Fabrication Gerbers
│   ├── BOM.csv               # Bill of materials (LCSC part numbers)
│   ├── PickAndPlace.xlsx     # SMT pick-and-place / assembly data
│   └── 3D.step               # Full 3D STEP model
├── images/
│   └── render_front.png      # Board render
├── LICENSE
└── README.md
```


## Firmware

TODO

## Contributing

Issues, pull requests, and hardware/firmware forks are all welcome. If you build one, share a photo!

## License

Released under the [MIT License](LICENSE). © 2026 Hans Polders.

