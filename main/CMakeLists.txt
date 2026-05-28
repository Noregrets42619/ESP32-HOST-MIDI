idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        piano_app
        espressif__arduino-esp32
        nvs_flash
)

set(PROJECT_SPIFFS_DIR "${CMAKE_CURRENT_LIST_DIR}/../spiffs")
if(EXISTS "${PROJECT_SPIFFS_DIR}" AND IS_DIRECTORY "${PROJECT_SPIFFS_DIR}")
    spiffs_create_partition_image(storage "${PROJECT_SPIFFS_DIR}" FLASH_IN_PROJECT)
else()
    message(STATUS "SPIFFS image skipped: ${PROJECT_SPIFFS_DIR} not found")
endif()
