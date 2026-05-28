#include "piano_app.h"

#include <Arduino.h>
#include <ESP32_Host_MIDI.h>
#include <USBConnection.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "piano_config.h"
#include "piano_types.h"

#if PIANO_APP_ENABLE_DISPLAY
#include "piano_display.h"
#endif

#if PIANO_APP_ENABLE_AUDIO
#include "piano_synth.h"
#endif

#if PIANO_APP_ENABLE_WIFI
#include "piano_wifi.h"
#endif

#if PIANO_APP_ENABLE_BLE
#include "piano_ble.h"
#endif

static const char *TAG = "piano_app";

static USBConnection usb_midi;
static bool active_notes[128] = {};
static PianoInfo piano_info = {};
static uint32_t last_frame_ms = 0;

#if PIANO_APP_ENABLE_DISPLAY
static PianoDisplay piano_display;
static bool display_ready = false;
#endif

#if PIANO_APP_ENABLE_AUDIO
static PianoSynth piano_synth;
static bool synth_ready = false;
#endif

#if PIANO_APP_ENABLE_WIFI
static bool wifi_midi_ready = false;
static int last_wifi_peer_count = -1;
#endif

#if PIANO_APP_ENABLE_BLE
static bool ble_midi_ready = false;
static bool last_ble_connected = false;
#endif

static float note_frequency(uint8_t note)
{
    return 440.0f * powf(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

static size_t raw_midi_length(uint8_t status)
{
    const uint8_t type = status & 0xF0;
    if (type == 0xC0 || type == 0xD0) {
        return 2;
    }
    if (type >= 0x80 && type <= 0xE0) {
        return 3;
    }

    switch (status) {
    case 0xF1:
    case 0xF3:
        return 2;
    case 0xF2:
        return 3;
    case 0xF6:
    case 0xF8:
    case 0xFA:
    case 0xFB:
    case 0xFC:
    case 0xFE:
    case 0xFF:
        return 1;
    default:
        return 0;
    }
}

static size_t usb_midi_payload_length(uint8_t cin)
{
    switch (cin) {
    case 0x02:
    case 0x06:
    case 0x0C:
    case 0x0D:
        return 2;
    case 0x03:
    case 0x04:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0E:
        return 3;
    case 0x05:
    case 0x0F:
        return 1;
    default:
        return 0;
    }
}

static size_t extract_raw_midi(const uint8_t *data, size_t length, uint8_t out[3])
{
    if (data == nullptr || out == nullptr || length == 0) {
        return 0;
    }

    if (length >= 4) {
        const size_t midi_len = usb_midi_payload_length(data[0] & 0x0F);
        if (midi_len == 0 || midi_len > 3) {
            return 0;
        }
        memcpy(out, data + 1, midi_len);
        return midi_len;
    }

    const size_t midi_len = raw_midi_length(data[0]);
    if (midi_len == 0 || midi_len > length || midi_len > 3) {
        return 0;
    }
    memcpy(out, data, midi_len);
    return midi_len;
}

#if PIANO_APP_ENABLE_WIFI
static void forward_usb_to_rtp_midi(const uint8_t *data, size_t length)
{
    if (!wifi_midi_ready || piano_wifi_peer_count() <= 0) {
        return;
    }

    uint8_t midi[3] = {};
    const size_t midi_len = extract_raw_midi(data, length, midi);
    if (midi_len == 0) {
        return;
    }

    MIDITransport *rtp = piano_wifi_transport();
    if (rtp != nullptr && rtp->sendMidiMessage(midi, midi_len)) {
        static bool bridge_logged = false;
        if (!bridge_logged) {
            bridge_logged = true;
            ESP_LOGI(TAG, "USB -> RTP-MIDI bridge active");
        }
    }
}
#endif

#if PIANO_APP_ENABLE_BLE
static void forward_usb_to_ble_midi(const uint8_t *data, size_t length)
{
    if (!ble_midi_ready || !piano_ble_connected()) {
        return;
    }

    uint8_t midi[3] = {};
    const size_t midi_len = extract_raw_midi(data, length, midi);
    if (midi_len == 0) {
        return;
    }

    MIDITransport *ble = piano_ble_transport();
    if (ble != nullptr && ble->sendMidiMessage(midi, midi_len)) {
        static bool bridge_logged = false;
        if (!bridge_logged) {
            bridge_logged = true;
            ESP_LOGI(TAG, "USB -> BLE-MIDI bridge active");
        }
    }
}
#endif

static void rebuild_piano_info(const MIDIEventData *last_event)
{
    memset(&piano_info, 0, sizeof(piano_info));
    piano_info.root_midi = -1;

    size_t used = 0;
    for (int note = 0; note < 128; ++note) {
        if (!active_notes[note]) {
            continue;
        }

        if (piano_info.root_midi < 0) {
            piano_info.root_midi = note;
            piano_info.root_freq = note_frequency(note);
        }

        char note_name[8] = {};
        MIDIHandler::noteWithOctave(static_cast<uint8_t>(note), note_name, sizeof(note_name));
        int written = snprintf(piano_info.note_text + used,
                               sizeof(piano_info.note_text) - used,
                               "%s%s",
                               used == 0 ? "" : " ",
                               note_name);
        if (written < 0) {
            break;
        }
        used += static_cast<size_t>(written);
        if (used >= sizeof(piano_info.note_text)) {
            piano_info.note_text[sizeof(piano_info.note_text) - 1] = '\0';
            break;
        }

        ++piano_info.note_count;
    }

    if (last_event != nullptr) {
        char event_note[8] = {};
        MIDIHandler::noteWithOctave(last_event->noteNumber, event_note, sizeof(event_note));
        snprintf(piano_info.last_event,
                 sizeof(piano_info.last_event),
                 "%s %s ch%u v%u",
                 MIDIHandler::statusName(last_event->statusCode),
                 event_note,
                 static_cast<unsigned>(last_event->channel0 + 1),
                 static_cast<unsigned>(last_event->velocity7));
    } else {
        snprintf(piano_info.last_event, sizeof(piano_info.last_event), "Waiting for MIDI");
    }
}

static void apply_event_to_state(const MIDIEventData &event)
{
    if (event.statusCode == MIDI_NOTE_ON && event.velocity7 > 0) {
        active_notes[event.noteNumber] = true;
    } else if (event.statusCode == MIDI_NOTE_OFF ||
               (event.statusCode == MIDI_NOTE_ON && event.velocity7 == 0)) {
        active_notes[event.noteNumber] = false;
    }
}

static void handle_usb_midi_data(void *, const uint8_t *data, size_t length)
{
    midiHandler.handleMidiMessage(data, length);

#if PIANO_APP_ENABLE_WIFI
    forward_usb_to_rtp_midi(data, length);
#endif

#if PIANO_APP_ENABLE_BLE
    forward_usb_to_ble_midi(data, length);
#endif
}

static void clear_all_notes()
{
    midiHandler.clearActiveNotesNow();
    memset(active_notes, 0, sizeof(active_notes));
    rebuild_piano_info(nullptr);

#if PIANO_APP_ENABLE_AUDIO
    if (synth_ready) {
        for (uint8_t note = 0; note < 128; ++note) {
            piano_synth.noteOff(note);
        }
    }
#endif
}

static void handle_usb_disconnected(void *)
{
    clear_all_notes();
}

void piano_app_begin(void)
{
    MIDIHandlerConfig config;
    config.maxEvents = 64;
    config.chordTimeWindow = 0;
    config.historyCapacity = 0;

    usb_midi.setMidiCallback(handle_usb_midi_data, nullptr);
    usb_midi.setConnectionCallbacks(nullptr, handle_usb_disconnected, nullptr);

    if (!usb_midi.begin()) {
        ESP_LOGE(TAG, "USB MIDI host init failed: %s", usb_midi.getLastError().c_str());
    } else {
        ESP_LOGI(TAG, "USB MIDI host initialized");
    }

#if PIANO_APP_ENABLE_WIFI
    wifi_midi_ready = piano_wifi_begin();
    if (wifi_midi_ready) {
        midiHandler.addTransport(piano_wifi_transport());
        ESP_LOGI(TAG, "RTP-MIDI transport initialized");
    } else {
        ESP_LOGW(TAG, "RTP-MIDI transport disabled");
    }
#else
    ESP_LOGW(TAG, "RTP-MIDI transport disabled by build configuration");
#endif

#if PIANO_APP_ENABLE_BLE
    ble_midi_ready = piano_ble_begin();
    if (ble_midi_ready) {
        midiHandler.addTransport(piano_ble_transport());
        ESP_LOGI(TAG, "BLE-MIDI transport initialized");
    } else {
        ESP_LOGW(TAG, "BLE-MIDI transport disabled");
    }
#else
    ESP_LOGW(TAG, "BLE-MIDI transport disabled by build configuration");
#endif

    midiHandler.begin(config);
    rebuild_piano_info(nullptr);

#if PIANO_APP_ENABLE_DISPLAY
    if (piano_display.begin()) {
        display_ready = true;
        piano_display.render(active_notes, piano_info);
        ESP_LOGI(TAG, "Piano display initialized");
    } else {
        display_ready = false;
        ESP_LOGE(TAG, "Piano display init failed");
    }
#else
    ESP_LOGW(TAG, "Piano display disabled by build configuration");
#endif

#if PIANO_APP_ENABLE_AUDIO
    synth_ready = piano_synth.begin();
    if (synth_ready) {
        ESP_LOGI(TAG, "Piano synth initialized");
    } else {
        ESP_LOGW(TAG, "Piano synth disabled");
    }
#else
    ESP_LOGW(TAG, "Piano synth disabled by build configuration");
#endif

    ESP_LOGI(TAG, "Piano app initialized");
}

void piano_app_loop(void)
{
    usb_midi.task();
    midiHandler.task();

#if PIANO_APP_ENABLE_WIFI
    if (wifi_midi_ready) {
        const int peer_count = piano_wifi_peer_count();
        if (peer_count != last_wifi_peer_count) {
            last_wifi_peer_count = peer_count;
            ESP_LOGI(TAG, "RTP-MIDI peers=%d", peer_count);
        }
    }
#endif

#if PIANO_APP_ENABLE_BLE
    if (ble_midi_ready) {
        const bool connected = piano_ble_connected();
        if (connected != last_ble_connected) {
            last_ble_connected = connected;
            ESP_LOGI(TAG, "BLE-MIDI %s", connected ? "connected" : "disconnected");
            if (!connected) {
                clear_all_notes();
            }
        }
    }
#endif

    bool changed = false;
    const MIDIEventData *last_event = nullptr;
    const auto &queue = midiHandler.getQueue();
    for (const auto &event : queue) {
        apply_event_to_state(event);
        last_event = &event;
        changed = true;

#if PIANO_APP_ENABLE_AUDIO
        if (synth_ready) {
            if (event.statusCode == MIDI_NOTE_ON && event.velocity7 > 0) {
                piano_synth.noteOn(event.noteNumber, event.velocity7);
            } else if (event.statusCode == MIDI_NOTE_OFF ||
                       (event.statusCode == MIDI_NOTE_ON && event.velocity7 == 0)) {
                piano_synth.noteOff(event.noteNumber);
            }
        }
#endif

        char note[8] = {};
        ESP_LOGI(TAG,
                 "%s note=%s ch=%u velocity=%u",
                 MIDIHandler::statusName(event.statusCode),
                 MIDIHandler::noteWithOctave(event.noteNumber, note, sizeof(note)),
                 static_cast<unsigned>(event.channel0 + 1),
                 static_cast<unsigned>(event.velocity7));
    }

    if (changed) {
        rebuild_piano_info(last_event);
        midiHandler.clearQueue();
    }

#if PIANO_APP_ENABLE_DISPLAY
    const uint32_t now = millis();
    if (display_ready && (changed || (now - last_frame_ms) >= 33)) {
        last_frame_ms = now;
        piano_display.render(active_notes, piano_info);
    }
#endif

    delay(1);
}
