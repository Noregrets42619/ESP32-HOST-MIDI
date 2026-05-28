#include "piano_wifi.h"

#include <cstring>

#include "Arduino.h"
#include "WiFi.h"
#include "esp_log.h"
#include "piano_config.h"

#if PIANO_APP_ENABLE_WIFI

#define RTP_MIDI_DEVICE_NAME PIANO_RTP_MIDI_DEVICE_NAME
#define RTP_MIDI_PORT PIANO_RTP_MIDI_PORT
#include "RTPMIDIConnection.h"

static const char *TAG = "piano_wifi";

static RTPMIDIConnection rtp_midi;
static bool wifi_connected = false;
static bool rtp_ready = false;

static bool wifi_credentials_configured()
{
    return std::strlen(PIANO_WIFI_SSID) > 0;
}

static bool connect_wifi()
{
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        return true;
    }

    if (!wifi_credentials_configured()) {
        ESP_LOGW(TAG, "WiFi disabled: set PIANO_WIFI_SSID and PIANO_WIFI_PASSWORD in piano_config.h");
        return false;
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    ESP_LOGI(TAG, "connecting to WiFi SSID:%s", PIANO_WIFI_SSID);
    WiFi.begin(PIANO_WIFI_SSID, PIANO_WIFI_PASSWORD);

    const uint32_t start_ms = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - start_ms) >= PIANO_WIFI_CONNECT_TIMEOUT_MS) {
            ESP_LOGE(TAG, "WiFi connection timeout after %d ms", PIANO_WIFI_CONNECT_TIMEOUT_MS);
            return false;
        }
        delay(250);
    }

    wifi_connected = true;
    IPAddress ip = WiFi.localIP();
    ESP_LOGI(TAG, "WiFi connected: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

bool piano_wifi_begin()
{
    if (rtp_ready) {
        return true;
    }

    if (!connect_wifi()) {
        return false;
    }

    if (!rtp_midi.begin(PIANO_RTP_MIDI_DEVICE_NAME)) {
        ESP_LOGE(TAG, "RTP-MIDI begin failed");
        return false;
    }

    rtp_ready = true;
    ESP_LOGI(TAG,
             "RTP-MIDI ready: name=\"%s\" port=%d",
             PIANO_RTP_MIDI_DEVICE_NAME,
             PIANO_RTP_MIDI_PORT);
    return true;
}

bool piano_wifi_ready()
{
    return wifi_connected && rtp_ready;
}

MIDITransport *piano_wifi_transport()
{
    return rtp_ready ? &rtp_midi : nullptr;
}

int piano_wifi_peer_count()
{
    return rtp_ready ? rtp_midi.connectedCount() : 0;
}

#else

bool piano_wifi_begin()
{
    return false;
}

bool piano_wifi_ready()
{
    return false;
}

MIDITransport *piano_wifi_transport()
{
    return nullptr;
}

int piano_wifi_peer_count()
{
    return 0;
}

#endif
