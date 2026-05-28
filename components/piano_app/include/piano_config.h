#pragma once

#include "sdkconfig.h"

// ST7789 170x320 SPI display. Adjust these pins to match your wiring.
#ifndef PIANO_TFT_SCLK
#define PIANO_TFT_SCLK 32
#endif

#ifndef PIANO_TFT_MOSI
#define PIANO_TFT_MOSI 33
#endif

#ifndef PIANO_TFT_MISO
#define PIANO_TFT_MISO -1
#endif

#ifndef PIANO_TFT_CS
#define PIANO_TFT_CS 6
#endif

#ifndef PIANO_TFT_DC
#define PIANO_TFT_DC 4
#endif

#ifndef PIANO_TFT_RST
#define PIANO_TFT_RST 5
#endif

#ifndef PIANO_TFT_BL
#define PIANO_TFT_BL 38
#endif

#ifndef PIANO_TFT_BL_INVERT
#define PIANO_TFT_BL_INVERT false
#endif

#ifndef PIANO_TFT_SPI_HOST
#define PIANO_TFT_SPI_HOST SPI2_HOST
#endif

#ifndef PIANO_TFT_FREQ_WRITE
#define PIANO_TFT_FREQ_WRITE 20000000
#endif

#ifndef PIANO_TFT_FREQ_READ
#define PIANO_TFT_FREQ_READ 16000000
#endif

#ifndef PIANO_TFT_DMA_CHANNEL
#define PIANO_TFT_DMA_CHANNEL 0
#endif

#ifndef PIANO_TFT_OFFSET_X
#define PIANO_TFT_OFFSET_X 0
#endif

#ifndef PIANO_TFT_OFFSET_Y
#define PIANO_TFT_OFFSET_Y 35
#endif

#ifndef PIANO_TFT_OFFSET_ROTATION
#define PIANO_TFT_OFFSET_ROTATION 1
#endif

#ifndef PIANO_TFT_SWAP_XY
#define PIANO_TFT_SWAP_XY true
#endif

#ifndef PIANO_TFT_MIRROR_X
#define PIANO_TFT_MIRROR_X false
#endif

#ifndef PIANO_TFT_MIRROR_Y
#define PIANO_TFT_MIRROR_Y true
#endif

#ifndef PIANO_TFT_INVERT
#define PIANO_TFT_INVERT true
#endif

#ifndef PIANO_TFT_RGB_ORDER
#define PIANO_TFT_RGB_ORDER false
#endif

#ifndef PIANO_TFT_DATA_ENDIAN_LITTLE
#define PIANO_TFT_DATA_ENDIAN_LITTLE true
#endif

#ifndef PIANO_APP_ENABLE_AUDIO
#define PIANO_APP_ENABLE_AUDIO 1
#endif

#ifndef PIANO_APP_ENABLE_WIFI
#define PIANO_APP_ENABLE_WIFI 1
#endif

#ifndef PIANO_APP_ENABLE_BLE
#if defined(CONFIG_BT_ENABLED) && (defined(CONFIG_NIMBLE_ENABLED) || defined(CONFIG_BLUEDROID_ENABLED))
#define PIANO_APP_ENABLE_BLE 1
#else
#define PIANO_APP_ENABLE_BLE 0
#endif
#endif

#ifndef PIANO_WIFI_SSID
#define PIANO_WIFI_SSID "iPhone"
#endif

#ifndef PIANO_WIFI_PASSWORD
#define PIANO_WIFI_PASSWORD "ZXK42619"
#endif

#ifndef PIANO_WIFI_CONNECT_TIMEOUT_MS
#define PIANO_WIFI_CONNECT_TIMEOUT_MS 15000
#endif

#ifndef PIANO_RTP_MIDI_DEVICE_NAME
#define PIANO_RTP_MIDI_DEVICE_NAME "WT99P4 Piano"
#endif

#ifndef PIANO_RTP_MIDI_PORT
#define PIANO_RTP_MIDI_PORT 5004
#endif

#ifndef PIANO_BLE_MIDI_DEVICE_NAME
#define PIANO_BLE_MIDI_DEVICE_NAME "WT99P4 BLE MIDI"
#endif

#ifndef PIANO_SYNTH_VOLUME
#define PIANO_SYNTH_VOLUME 80
#endif

#ifndef PIANO_SYNTH_MASTER_GAIN
#define PIANO_SYNTH_MASTER_GAIN 0.75f
#endif

#ifndef PIANO_SYNTH_VOICE_GAIN
#define PIANO_SYNTH_VOICE_GAIN 0.70f
#endif

#ifndef PIANO_SYNTH_BASS_GAIN
#define PIANO_SYNTH_BASS_GAIN 1.75f
#endif

#ifndef PIANO_SYNTH_TREBLE_GAIN
#define PIANO_SYNTH_TREBLE_GAIN 0.70f
#endif
