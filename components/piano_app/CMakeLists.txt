set(PIANO_APP_SRCS
    "src/piano_app.cpp"
    "src/piano_display.cpp"
    "src/piano_synth.cpp"
    "src/piano_wifi.cpp"
    "src/piano_ble.cpp"
)

set(PIANO_APP_REQUIRES
    AppleMIDI
    ESP32_Host_MIDI
    MIDI
    espressif__arduino-esp32
    espressif__esp_wifi_remote
    espressif__mdns
    esp_event
    esp_netif
    esp_wifi
    esp_driver_gpio
    esp_driver_spi
    espressif__esp_codec_dev
    esp_lcd
    heap
    wt99p4c5_s1_board
)

set(PIANO_APP_ENABLE_DISPLAY 1)

idf_component_register(
    SRCS ${PIANO_APP_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${PIANO_APP_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PUBLIC PIANO_APP_ENABLE_DISPLAY=${PIANO_APP_ENABLE_DISPLAY})

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-unused-parameter
)
