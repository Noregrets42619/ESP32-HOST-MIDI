#include "piano_ble.h"

#include "esp_log.h"
#include "piano_config.h"

#if PIANO_APP_ENABLE_BLE

#include "BLEConnection.h"

static const char *TAG = "piano_ble";

static BLEConnection ble_midi;
static bool ble_ready = false;

bool piano_ble_begin()
{
    if (ble_ready) {
        return true;
    }

    if (!ble_midi.begin(PIANO_BLE_MIDI_DEVICE_NAME)) {
        ESP_LOGE(TAG, "BLE MIDI begin failed");
        return false;
    }

    ble_ready = true;
    ESP_LOGI(TAG, "BLE MIDI ready: name=\"%s\"", PIANO_BLE_MIDI_DEVICE_NAME);
    return true;
}

bool piano_ble_ready()
{
    return ble_ready;
}

bool piano_ble_connected()
{
    return ble_ready && ble_midi.isConnected();
}

MIDITransport *piano_ble_transport()
{
    return ble_ready ? &ble_midi : nullptr;
}

#else

bool piano_ble_begin()
{
    return false;
}

bool piano_ble_ready()
{
    return false;
}

bool piano_ble_connected()
{
    return false;
}

MIDITransport *piano_ble_transport()
{
    return nullptr;
}

#endif
