# ESP32-HOST-MIDI for WT99P4C5

> Author: Ecoli  
> Notes: [noregrets42619.github.io](https://noregrets42619.github.io/)  
> Repository: [Noregrets42619/ESP32-HOST-MIDI](https://github.com/Noregrets42619/ESP32-HOST-MIDI)

这是一个运行在 WT99P4C5-S1 开发板上的 MIDI Piano 固件工程。主控为 ESP32-P4，板载 ESP32-C5 作为 WiFi / Bluetooth 协处理器，通过 ESP-Hosted 向 P4 提供无线能力。

工程基于 ESP-IDF，混合引入 Arduino component，用来复用 `ESP32_Host_MIDI`、AppleMIDI、Arduino WiFi 和 BLE MIDI 相关代码。移植目标是把上游 `ESP32_Host_MIDI` 的 piano 思路落到 P4+C5 硬件上，同时接入 USB Host、SPI 屏幕、ES8311 音频、WiFi AppleMIDI 和 BLE MIDI。

## Hardware

- Board: WT99P4C5-S1
- Host MCU: ESP32-P4
- Wireless coprocessor: ESP32-C5, running ESP-Hosted slave firmware
- Display: ST7789 SPI screen
- Audio codec: ES8311
- External amplifier: NS4150B, enable pin GPIO53
- MIDI input: USB Host MIDI keyboard
- Network MIDI: RTP-MIDI / AppleMIDI over hosted WiFi
- BLE MIDI: BLE peripheral over hosted Bluetooth VHCI

## Features

- USB Host MIDI input
- Piano key visualization on SPI display
- Local audio synthesis through ES8311 / I2S
- USB MIDI to RTP-MIDI bridge
- USB MIDI to BLE MIDI bridge
- AppleMIDI / RTP-MIDI discovery and session handling
- BLE MIDI peripheral mode
- WiFi and BLE feature switches for memory-sensitive builds

## Project Layout

```text
.
├── main/                         Application entry
├── components/
│   ├── piano_app/                MIDI piano application layer
│   ├── ESP32_Host_MIDI/          Ported MIDI transport and handler library
│   ├── AppleMIDI/                RTP-MIDI / AppleMIDI support
│   ├── MIDI/                     Arduino MIDI base library
│   └── wt99p4c5_s1_board/        WT99P4C5-S1 board support package
├── managed_components/           ESP-IDF component manager dependencies
├── docs/                         Board schematic and hardware reference
├── partitions.csv                Flash partition table
├── sdkconfig                     Current ESP-IDF configuration
└── sdkconfig.defaults            Default project configuration
```

## Application Layer

`piano_app` is the main firmware layer. It connects the MIDI transports, display, and audio path:

- receives USB MIDI packets from the keyboard
- tracks active MIDI notes
- renders the piano UI
- drives the local synth engine
- starts RTP-MIDI when WiFi is enabled
- starts BLE MIDI when Bluetooth is enabled
- forwards USB MIDI messages to network and BLE peers

The display backend uses ESP-IDF `esp_lcd` to drive the ST7789 panel over SPI. The audio path uses the board ES8311 codec and I2S output. WiFi and BLE are provided by the C5 hosted slave, not by local P4 radio hardware.

## MIDI Flow

```text
USB MIDI keyboard
    -> USB Host MIDI
    -> MIDI handler
    -> piano display
    -> ES8311 audio output
    -> RTP-MIDI / BLE-MIDI bridge

AppleMIDI / RTP-MIDI
    -> hosted WiFi
    -> MIDI handler
    -> piano display
    -> ES8311 audio output

BLE MIDI
    -> hosted Bluetooth VHCI
    -> P4 NimBLE host
    -> MIDI handler
    -> piano display
    -> ES8311 audio output
```

## Build

The project is intended to be built with the VSCode ESP-IDF extension.

Typical workflow:

1. `ESP-IDF: Reconfigure Project`
2. `ESP-IDF: Build Project`
3. `ESP-IDF: Flash Project`
4. `ESP-IDF: Monitor`

The C5 side must be flashed with ESP-Hosted slave firmware first. For BLE MIDI, the P4 project needs hosted NimBLE VHCI enabled, and the C5 slave firmware needs Bluetooth sharing enabled.

Expected hosted Bluetooth log:

```text
Host BT Support: Enabled
BT Transport Type: VHCI
```

## Board Files

The `docs/` directory contains WT99P4C5-S1 hardware reference files, including the board schematic PDF and board image.

## Status

USB MIDI, SPI display, ES8311 audio, WiFi AppleMIDI, and BLE MIDI are all integrated in the current firmware. The project keeps the upstream MIDI logic mostly intact and places board-specific work in the application and hardware adaptation layers.
