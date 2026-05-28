idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        piano_app
        espressif__arduino-esp32
        nvs_flash
)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
