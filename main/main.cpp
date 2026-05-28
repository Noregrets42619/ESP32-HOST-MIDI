/*
 * WT99P4C5-S1 MIDI migration entry point.
 *
 * This is intentionally small: it proves the ESP-IDF + Arduino mixed build,
 * ESP32_Host_MIDI, and USB Host MIDI transport compile together before the
 * piano UI/audio layers are ported.
 */

#include "esp_err.h"
#include "nvs_flash.h"
#include "Arduino.h"
#include "piano_app.h"

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

extern "C" void app_main(void)
{
    init_nvs();
    initArduino();

    piano_app_begin();

    while (true) {
        piano_app_loop();
    }
}
