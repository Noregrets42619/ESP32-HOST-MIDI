#include "piano_display.h"

#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"

static const int SCREEN_W = 320;
static const int SCREEN_H = 170;
static const int INFO_H = 48;
static const int PIANO_Y = INFO_H;
static const int PIANO_H = SCREEN_H - INFO_H;
static const int KEYS_SPAN = 25;
static const int WHITE_KEYS = 15;
static const int WHITE_KEY_W = 21;
static const int BLACK_KEY_W = 12;
static const int BLACK_KEY_H = static_cast<int>(PIANO_H * 0.60f);
static const int PIANO_X = (SCREEN_W - WHITE_KEYS * WHITE_KEY_W) / 2;
static const int VIEW_DEFAULT = 48;
static const int VIEW_MIN = 0;
static const int VIEW_MAX = 103;
static const char *TAG = "PianoDisplay";
static const TickType_t FLUSH_WAIT_TICKS = pdMS_TO_TICKS(120);

static const uint16_t COL_WHITE_NORMAL = 0xFFFF;
static const uint16_t COL_WHITE_ACTIVE = 0x07FF;
static const uint16_t COL_BLACK_NORMAL = 0x0841;
static const uint16_t COL_BLACK_ACTIVE = 0xFBE0;
static const uint16_t COL_KEY_BORDER = 0x0000;
static const uint16_t COL_INFO_BG = 0x1082;
static const uint16_t COL_HEADER_BG = 0x2945;
static const uint16_t COL_DIM = 0x8410;
static const uint16_t COL_TEXT = 0xFFFF;
static const uint16_t COL_ACCENT = 0x07FF;
static const uint16_t COL_NOTE = 0xFFE0;
static const uint16_t COL_WAITING = 0x8410;

static const int SEMITONE_TO_WHITE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
static const int BLACK_LEFT_NEIGHBOR[12] = {-1, 0, -1, 2, -1, -1, 5, -1, 7, -1, 9, -1};
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

class PianoCanvas {
public:
    PianoCanvas()
        : _buffer(nullptr),
          _width(0),
          _height(0),
          _fg(0xFFFF),
          _bg(0x0000),
          _clipX0(0),
          _clipY0(0),
          _clipX1(-1),
          _clipY1(-1)
    {
    }

    ~PianoCanvas()
    {
        if (_buffer != nullptr) {
            heap_caps_free(_buffer);
        }
    }

    void setColorDepth(int) {}
    void setPsram(bool) {}
    void setTextDatum(int) {}
    void setFont(const void *) {}

    bool createSprite(int w, int h)
    {
        if (_buffer != nullptr) {
            heap_caps_free(_buffer);
            _buffer = nullptr;
        }

        _width = w;
        _height = h;
        clearClipRect();

        const size_t bytes = static_cast<size_t>(w) * h * sizeof(uint16_t);
        _buffer = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (_buffer == nullptr) {
            _buffer = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (_buffer == nullptr) {
            _buffer = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (_buffer != nullptr) {
            memset(_buffer, 0, bytes);
        }
        return _buffer != nullptr;
    }

    void *getBuffer() const { return _buffer; }

    int textWidth(const char *text) const
    {
        return text == nullptr ? 0 : static_cast<int>(strlen(text)) * 6;
    }

    void setTextColor(uint16_t fg, uint16_t bg)
    {
        _fg = fg;
        _bg = bg;
    }

    void setClipRect(int x, int y, int w, int h)
    {
        _clipX0 = std::max(0, x);
        _clipY0 = std::max(0, y);
        _clipX1 = std::min(_width - 1, x + w - 1);
        _clipY1 = std::min(_height - 1, y + h - 1);
    }

    void clearClipRect()
    {
        _clipX0 = 0;
        _clipY0 = 0;
        _clipX1 = _width - 1;
        _clipY1 = _height - 1;
    }

    void fillRect(int x, int y, int w, int h, uint16_t color)
    {
        if (_buffer == nullptr || w <= 0 || h <= 0) {
            return;
        }

        const int x0 = std::max(x, _clipX0);
        const int y0 = std::max(y, _clipY0);
        const int x1 = std::min(x + w - 1, _clipX1);
        const int y1 = std::min(y + h - 1, _clipY1);
        if (x0 > x1 || y0 > y1) {
            return;
        }

        for (int yy = y0; yy <= y1; ++yy) {
            uint16_t *row = _buffer + yy * _width + x0;
            std::fill(row, row + (x1 - x0 + 1), color);
        }
    }

    void drawFastHLine(int x, int y, int w, uint16_t color)
    {
        fillRect(x, y, w, 1, color);
    }

    void drawFastVLine(int x, int y, int h, uint16_t color)
    {
        fillRect(x, y, 1, h, color);
    }

    void drawRect(int x, int y, int w, int h, uint16_t color)
    {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }

    void fillRoundRect(int x, int y, int w, int h, int, uint16_t color)
    {
        fillRect(x, y, w, h, color);
    }

    void drawString(const char *text, int x, int y)
    {
        if (text == nullptr) {
            return;
        }

        int cursor = x;
        for (const char *p = text; *p != '\0'; ++p) {
            drawChar(cursor, y, *p);
            cursor += 6;
        }
    }

private:
    static const uint8_t *glyph(char c)
    {
        static const uint8_t blank[5] = {0, 0, 0, 0, 0};
        static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
        static const uint8_t digits[10][5] = {
            {0x3E, 0x51, 0x49, 0x45, 0x3E},
            {0x00, 0x42, 0x7F, 0x40, 0x00},
            {0x42, 0x61, 0x51, 0x49, 0x46},
            {0x21, 0x41, 0x45, 0x4B, 0x31},
            {0x18, 0x14, 0x12, 0x7F, 0x10},
            {0x27, 0x45, 0x45, 0x45, 0x39},
            {0x3C, 0x4A, 0x49, 0x49, 0x30},
            {0x01, 0x71, 0x09, 0x05, 0x03},
            {0x36, 0x49, 0x49, 0x49, 0x36},
            {0x06, 0x49, 0x49, 0x29, 0x1E},
        };
        static const uint8_t letters[26][5] = {
            {0x7E, 0x11, 0x11, 0x11, 0x7E},
            {0x7F, 0x49, 0x49, 0x49, 0x36},
            {0x3E, 0x41, 0x41, 0x41, 0x22},
            {0x7F, 0x41, 0x41, 0x22, 0x1C},
            {0x7F, 0x49, 0x49, 0x49, 0x41},
            {0x7F, 0x09, 0x09, 0x09, 0x01},
            {0x3E, 0x41, 0x49, 0x49, 0x7A},
            {0x7F, 0x08, 0x08, 0x08, 0x7F},
            {0x00, 0x41, 0x7F, 0x41, 0x00},
            {0x20, 0x40, 0x41, 0x3F, 0x01},
            {0x7F, 0x08, 0x14, 0x22, 0x41},
            {0x7F, 0x40, 0x40, 0x40, 0x40},
            {0x7F, 0x02, 0x0C, 0x02, 0x7F},
            {0x7F, 0x04, 0x08, 0x10, 0x7F},
            {0x3E, 0x41, 0x41, 0x41, 0x3E},
            {0x7F, 0x09, 0x09, 0x09, 0x06},
            {0x3E, 0x41, 0x51, 0x21, 0x5E},
            {0x7F, 0x09, 0x19, 0x29, 0x46},
            {0x46, 0x49, 0x49, 0x49, 0x31},
            {0x01, 0x01, 0x7F, 0x01, 0x01},
            {0x3F, 0x40, 0x40, 0x40, 0x3F},
            {0x1F, 0x20, 0x40, 0x20, 0x1F},
            {0x3F, 0x40, 0x38, 0x40, 0x3F},
            {0x63, 0x14, 0x08, 0x14, 0x63},
            {0x07, 0x08, 0x70, 0x08, 0x07},
            {0x61, 0x51, 0x49, 0x45, 0x43},
        };
        static const uint8_t hash[5] = {0x14, 0x7F, 0x14, 0x7F, 0x14};
        static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
        static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
        static const uint8_t lt[5] = {0x08, 0x14, 0x22, 0x41, 0x00};
        static const uint8_t gt[5] = {0x00, 0x41, 0x22, 0x14, 0x08};

        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (c >= '0' && c <= '9') {
            return digits[c - '0'];
        }
        if (c >= 'A' && c <= 'Z') {
            return letters[c - 'A'];
        }
        switch (c) {
        case ' ':
            return blank;
        case '#':
            return hash;
        case '-':
            return dash;
        case '.':
            return dot;
        case '<':
            return lt;
        case '>':
            return gt;
        default:
            return question;
        }
    }

    void drawPixel(int x, int y, uint16_t color)
    {
        if (_buffer == nullptr || x < _clipX0 || x > _clipX1 || y < _clipY0 || y > _clipY1) {
            return;
        }
        _buffer[y * _width + x] = color;
    }

    void drawChar(int x, int y, char c)
    {
        fillRect(x, y, 6, 8, _bg);
        const uint8_t *g = glyph(c);
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; ++row) {
                if ((bits >> row) & 0x01) {
                    drawPixel(x + col, y + row, _fg);
                }
            }
        }
    }

    uint16_t *_buffer;
    int _width;
    int _height;
    uint16_t _fg;
    uint16_t _bg;
    int _clipX0;
    int _clipY0;
    int _clipX1;
    int _clipY1;
};

static bool lcd_color_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *user_ctx)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(user_ctx), &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

PianoDisplay::PianoDisplay()
    : _screen(nullptr),
      _lcdIo(nullptr),
      _lcdPanel(nullptr),
      _flushDone(nullptr),
      _spiHost(PIANO_TFT_SPI_HOST),
      _viewStart(VIEW_DEFAULT),
      _initialized(false),
      _spiBusInitialized(false)
{
}

bool PianoDisplay::begin()
{
    init();
    return _initialized;
}

void PianoDisplay::init()
{
    if (_initialized) {
        return;
    }

    pinMode(PIANO_TFT_BL, OUTPUT);
    digitalWrite(PIANO_TFT_BL, PIANO_TFT_BL_INVERT ? HIGH : LOW);

    if (_screen == nullptr) {
        _screen = new (std::nothrow) PianoCanvas();
    }
    if (_screen == nullptr) {
        ESP_LOGE(TAG, "sprite object allocation failed");
        return;
    }

    if (!init_lcd_panel()) {
        return;
    }

    digitalWrite(PIANO_TFT_BL, PIANO_TFT_BL_INVERT ? LOW : HIGH);

    _screen->setColorDepth(16);
    _screen->setPsram(true);
    if (!_screen->createSprite(SCREEN_W, SCREEN_H)) {
        ESP_LOGE(TAG,
                 "sprite allocation failed: %dx%d 16bpp, internal_free=%u, psram_free=%u",
                 SCREEN_W,
                 SCREEN_H,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        return;
    }
    _screen->setTextDatum(0);

    _initialized = true;
    ESP_LOGI(TAG, "display initialized: %dx%d sprite in PSRAM", SCREEN_W, SCREEN_H);
}

bool PianoDisplay::init_lcd_panel()
{
    if (_lcdPanel != nullptr) {
        return true;
    }

    _flushDone = xSemaphoreCreateBinary();
    if (_flushDone == nullptr) {
        ESP_LOGE(TAG, "flush semaphore allocation failed");
        return false;
    }
    xSemaphoreGive(_flushDone);

    spi_host_device_t host_candidates[2] = {PIANO_TFT_SPI_HOST, PIANO_TFT_SPI_HOST};
    size_t host_count = 1;
#if defined(SPI2_HOST) && defined(SPI3_HOST)
    host_candidates[1] = (PIANO_TFT_SPI_HOST == SPI2_HOST) ? SPI3_HOST : SPI2_HOST;
    host_count = 2;
#endif

    esp_err_t ret = ESP_FAIL;
    bool io_ready = false;
    for (size_t i = 0; i < host_count && !io_ready; ++i) {
        const spi_host_device_t host = host_candidates[i];
        bool bus_owned_by_this_component = false;

        spi_bus_config_t bus_config = {};
        bus_config.sclk_io_num = PIANO_TFT_SCLK;
        bus_config.mosi_io_num = PIANO_TFT_MOSI;
        bus_config.miso_io_num = PIANO_TFT_MISO;
        bus_config.quadwp_io_num = -1;
        bus_config.quadhd_io_num = -1;
        bus_config.max_transfer_sz = SCREEN_W * 40 * sizeof(uint16_t);

        ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
        if (ret == ESP_OK) {
            bus_owned_by_this_component = true;
            _spiBusInitialized = true;
            ESP_LOGI(TAG, "SPI host %d bus initialized", static_cast<int>(host));
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI host %d already initialized, reuse existing bus", static_cast<int>(host));
        } else {
            ESP_LOGW(TAG, "SPI host %d bus init failed: %s", static_cast<int>(host), esp_err_to_name(ret));
            continue;
        }

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.dc_gpio_num = PIANO_TFT_DC;
        io_config.cs_gpio_num = PIANO_TFT_CS;
        io_config.pclk_hz = PIANO_TFT_FREQ_WRITE;
        io_config.spi_mode = 0;
        io_config.trans_queue_depth = 2;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.on_color_trans_done = lcd_color_transfer_done;
        io_config.user_ctx = _flushDone;

        ret = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(host), &io_config, &_lcdIo);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "panel IO on SPI host %d failed: %s", static_cast<int>(host), esp_err_to_name(ret));
            if (bus_owned_by_this_component) {
                spi_bus_free(host);
                _spiBusInitialized = false;
            }
            continue;
        }

        _spiHost = host;
        io_ready = true;
    }

    if (!io_ready || _lcdIo == nullptr) {
        ESP_LOGE(TAG, "panel IO init failed on all SPI hosts");
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIANO_TFT_RST;
    panel_config.rgb_ele_order = PIANO_TFT_RGB_ORDER ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = PIANO_TFT_DATA_ENDIAN_LITTLE ? LCD_RGB_DATA_ENDIAN_LITTLE : LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 16;

    ret = esp_lcd_new_panel_st7789(_lcdIo, &panel_config, &_lcdPanel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(_lcdIo);
        _lcdIo = nullptr;
        return false;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(_lcdPanel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(_lcdPanel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(_lcdPanel, PIANO_TFT_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(_lcdPanel, PIANO_TFT_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(_lcdPanel, PIANO_TFT_MIRROR_X, PIANO_TFT_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(_lcdPanel, PIANO_TFT_OFFSET_X, PIANO_TFT_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(_lcdPanel, true));

    ESP_LOGI(TAG,
             "ST7789 SPI panel initialized: host=%d sclk=%d mosi=%d cs=%d dc=%d rst=%d gap=%d,%d swap=%d mirror=%d,%d endian=%s rgb=%s",
             static_cast<int>(_spiHost),
             PIANO_TFT_SCLK,
             PIANO_TFT_MOSI,
             PIANO_TFT_CS,
             PIANO_TFT_DC,
             PIANO_TFT_RST,
             PIANO_TFT_OFFSET_X,
             PIANO_TFT_OFFSET_Y,
             PIANO_TFT_SWAP_XY ? 1 : 0,
             PIANO_TFT_MIRROR_X ? 1 : 0,
             PIANO_TFT_MIRROR_Y ? 1 : 0,
             PIANO_TFT_DATA_ENDIAN_LITTLE ? "little" : "big",
             PIANO_TFT_RGB_ORDER ? "bgr" : "rgb");
    return true;
}

bool PianoDisplay::push_frame()
{
    if (_lcdPanel == nullptr || _screen == nullptr || _screen->getBuffer() == nullptr || _flushDone == nullptr) {
        return false;
    }

    while (xSemaphoreTake(_flushDone, 0) == pdTRUE) {
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(_lcdPanel, 0, 0, SCREEN_W, SCREEN_H, _screen->getBuffer());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "draw bitmap failed: %s", esp_err_to_name(ret));
        return false;
    }

    if (xSemaphoreTake(_flushDone, FLUSH_WAIT_TICKS) != pdTRUE) {
        ESP_LOGW(TAG, "frame flush timeout");
    }

    return true;
}

void PianoDisplay::shiftOctave(int delta)
{
    int next = _viewStart + delta;
    if (next < VIEW_MIN) {
        next = VIEW_MIN;
    }
    if (next > VIEW_MAX) {
        next = VIEW_MAX;
    }
    _viewStart = next;
}

void PianoDisplay::setViewStart(int midi_note)
{
    int start = (midi_note / 12) * 12;
    if (start < VIEW_MIN) {
        start = VIEW_MIN;
    }
    if (start > VIEW_MAX) {
        start = VIEW_MAX;
    }
    _viewStart = start;
}

void PianoDisplay::set_view_start(int midi_note)
{
    setViewStart(midi_note);
}

void PianoDisplay::_drawBadge(int x, int y, int w, int h, uint16_t bg, const char *text)
{
    _screen->fillRoundRect(x, y, w, h, 3, bg);
    _screen->setTextColor(0x0000, bg);
    int tw = _screen->textWidth(text);
    _screen->drawString(text, x + (w - tw) / 2, y + 2);
}

void PianoDisplay::_drawPiano(const bool active_notes[128])
{
    _screen->fillRect(0, PIANO_Y, SCREEN_W, PIANO_H, COL_KEY_BORDER);

    for (int note = _viewStart; note < _viewStart + KEYS_SPAN && note < 128; ++note) {
        int st = note % 12;
        if (SEMITONE_TO_WHITE[st] < 0) {
            continue;
        }

        int wi = ((note - _viewStart) / 12) * 7 + SEMITONE_TO_WHITE[st];
        if (wi < 0 || wi >= WHITE_KEYS) {
            continue;
        }

        int x = PIANO_X + wi * WHITE_KEY_W;
        uint16_t color = active_notes[note] ? COL_WHITE_ACTIVE : COL_WHITE_NORMAL;
        _screen->fillRect(x, PIANO_Y + 1, WHITE_KEY_W - 1, PIANO_H - 2, color);
        _screen->drawFastHLine(x, PIANO_Y + PIANO_H - 2, WHITE_KEY_W - 1, COL_KEY_BORDER);
    }

    for (int note = _viewStart; note < _viewStart + KEYS_SPAN && note < 128; ++note) {
        int st = note % 12;
        if (SEMITONE_TO_WHITE[st] >= 0) {
            continue;
        }

        int nb_st = BLACK_LEFT_NEIGHBOR[st];
        int nb_note = (note / 12) * 12 + nb_st;
        int nb_wi = ((nb_note - _viewStart) / 12) * 7 + SEMITONE_TO_WHITE[nb_st];
        int x = PIANO_X + nb_wi * WHITE_KEY_W + WHITE_KEY_W - BLACK_KEY_W / 2;
        uint16_t color = active_notes[note] ? COL_BLACK_ACTIVE : COL_BLACK_NORMAL;
        _screen->fillRect(x, PIANO_Y + 1, BLACK_KEY_W, BLACK_KEY_H, color);
        _screen->drawRect(x, PIANO_Y + 1, BLACK_KEY_W, BLACK_KEY_H, COL_KEY_BORDER);
    }
}

void PianoDisplay::_drawInfoBar(const bool active_notes[128], const PianoInfo &info)
{
    _screen->fillRect(0, 0, SCREEN_W, INFO_H, COL_INFO_BG);
    _screen->drawFastHLine(0, INFO_H - 1, SCREEN_W, COL_HEADER_BG);
    _screen->drawFastVLine(60, 2, INFO_H - 4, COL_HEADER_BG);
    _screen->drawFastVLine(264, 2, INFO_H - 4, COL_HEADER_BG);

    int view_end = _viewStart + KEYS_SPAN - 1;
    char range[16] = {};
    snprintf(range,
             sizeof(range),
             "%s%d-%s%d",
             NOTE_NAMES[_viewStart % 12],
             (_viewStart / 12) - 1,
             NOTE_NAMES[view_end % 12],
             (view_end / 12) - 1);

    _screen->setFont(nullptr);
    _screen->setTextColor(COL_DIM, COL_INFO_BG);
    _screen->drawString(range, 3, 4);

    if (info.note_count > 0) {
        char count[8] = {};
        snprintf(count, sizeof(count), "%d", info.note_count);
        _screen->setTextColor(COL_ACCENT, COL_INFO_BG);
        _screen->drawString(count, 269, 4);

        char hz[16] = {};
        snprintf(hz, sizeof(hz), "%dHz", static_cast<int>(info.root_freq + 0.5f));
        _screen->setTextColor(COL_NOTE, COL_INFO_BG);
        _screen->drawString(hz, 268, 28);
    }

    _screen->setClipRect(62, 0, 200, INFO_H);
    if (info.note_count == 0) {
        _screen->setTextColor(COL_WAITING, COL_INFO_BG);
        _screen->drawString("Waiting for MIDI...", 64, 16);
    } else if (info.note_count == 1) {
        _drawBadge(64, 3, 42, 19, COL_NOTE, info.note_text);

        char midi[16] = {};
        snprintf(midi, sizeof(midi), "MIDI %d", info.root_midi);
        _screen->setTextColor(COL_DIM, COL_INFO_BG);
        _screen->drawString(midi, 112, 5);

        _screen->setTextColor(COL_ACCENT, COL_INFO_BG);
        _screen->drawString(info.last_event, 64, 28);
    } else {
        _screen->setTextColor(COL_TEXT, COL_INFO_BG);
        _screen->drawString(info.note_text, 64, 5);
        _screen->setTextColor(COL_ACCENT, COL_INFO_BG);
        _screen->drawString(info.last_event, 64, 28);
    }
    _screen->clearClipRect();

    bool below = false;
    bool above = false;
    for (int note = 0; note < 128; ++note) {
        if (!active_notes[note]) {
            continue;
        }
        if (note < _viewStart) {
            below = true;
        }
        if (note >= _viewStart + KEYS_SPAN) {
            above = true;
        }
    }
    if (below) {
        _screen->fillRoundRect(0, 18, 14, 14, 3, 0xF800);
        _screen->setTextColor(COL_TEXT, 0xF800);
        _screen->drawString("<", 3, 20);
    }
    if (above) {
        _screen->fillRoundRect(306, 18, 14, 14, 3, 0x001F);
        _screen->setTextColor(COL_TEXT, 0x001F);
        _screen->drawString(">", 309, 20);
    }
}

void PianoDisplay::render(const bool active_notes[128], const PianoInfo &info)
{
    if (!_initialized || _screen == nullptr || _lcdPanel == nullptr) {
        return;
    }

    if (info.root_midi >= 0 && (info.root_midi < _viewStart || info.root_midi >= _viewStart + KEYS_SPAN)) {
        setViewStart(info.root_midi);
    }

    _drawPiano(active_notes);
    _drawInfoBar(active_notes, info);

    if (!push_frame()) {
        ESP_LOGW(TAG, "push frame skipped");
    }
}
