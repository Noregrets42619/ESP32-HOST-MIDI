#pragma once

#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_common.h"

#include "piano_config.h"
#include "piano_types.h"

class PianoCanvas;

class PianoDisplay {
public:
    PianoDisplay();

    bool begin();
    void init();
    void render(const bool active_notes[128], const PianoInfo &info);
    void shiftOctave(int delta);
    void setViewStart(int midi_note);
    void set_view_start(int midi_note);
    int view_start() const { return _viewStart; }
    int getViewStart() const { return _viewStart; }

private:
    bool init_lcd_panel();
    bool push_frame();
    void _drawPiano(const bool active_notes[128]);
    void _drawInfoBar(const bool active_notes[128], const PianoInfo &info);
    void _drawBadge(int x, int y, int w, int h, uint16_t bg, const char *text);

    PianoCanvas *_screen;
    esp_lcd_panel_io_handle_t _lcdIo;
    esp_lcd_panel_handle_t _lcdPanel;
    SemaphoreHandle_t _flushDone;
    spi_host_device_t _spiHost;
    int _viewStart;
    bool _initialized;
    bool _spiBusInitialized;
};
