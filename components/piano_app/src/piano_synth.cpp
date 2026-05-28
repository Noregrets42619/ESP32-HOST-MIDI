#include "piano_synth.h"

#include <cmath>
#include <cstring>

#include "bsp/wt99p4c5_s1_board.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "piano_config.h"

static const char *TAG = "PianoSynth";

static const int SYNTH_SAMPLE_RATE = 44100;
static const int SYNTH_BUFFER_FRAMES = 256;
static const int SYNTH_VOICES = 8;
static const int SYNTH_SIN_LEN = 512;
static const float SYNTH_TWO_PI = 6.2831853071795864769f;
static const float SYNTH_MASTER_GAIN = PIANO_SYNTH_MASTER_GAIN;
static const int SYNTH_VOLUME = PIANO_SYNTH_VOLUME;
static const float SYNTH_VOICE_GAIN = PIANO_SYNTH_VOICE_GAIN;

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float note_gain(uint8_t note)
{
    if (note <= 48) {
        return PIANO_SYNTH_BASS_GAIN;
    }
    if (note < 60) {
        return lerp(PIANO_SYNTH_BASS_GAIN, 1.0f, static_cast<float>(note - 48) / 12.0f);
    }
    if (note <= 72) {
        return 1.0f;
    }
    if (note < 96) {
        return lerp(1.0f, PIANO_SYNTH_TREBLE_GAIN, static_cast<float>(note - 72) / 24.0f);
    }
    return PIANO_SYNTH_TREBLE_GAIN;
}

static bool pin_is_codec_i2c(int pin)
{
    return pin == static_cast<int>(BSP_I2C_SDA) || pin == static_cast<int>(BSP_I2C_SCL);
}

PianoSynth::PianoSynth()
    : _codec(nullptr),
      _queue(nullptr),
      _voices{},
      _sinTable{},
      _ready(false)
{
}

bool PianoSynth::begin()
{
    if (_ready) {
        return true;
    }

    if (pin_is_codec_i2c(PIANO_TFT_SCLK) || pin_is_codec_i2c(PIANO_TFT_MOSI) ||
        pin_is_codec_i2c(PIANO_TFT_CS) || pin_is_codec_i2c(PIANO_TFT_DC) ||
        pin_is_codec_i2c(PIANO_TFT_RST)) {
        ESP_LOGE(TAG,
                 "audio disabled: display SPI uses GPIO7/8, but ES8311 needs I2C SDA=7 SCL=8");
        ESP_LOGE(TAG,
                 "move display SPI pins away from GPIO7/8 and rebuild to enable audio");
        return false;
    }

    buildSinTable();
    memset(_voices, 0, sizeof(_voices));

    _queue = xQueueCreate(64, sizeof(NoteMsg));
    if (_queue == nullptr) {
        ESP_LOGE(TAG, "note queue allocation failed");
        return false;
    }

    _codec = bsp_audio_codec_speaker_init();
    if (_codec == nullptr) {
        ESP_LOGE(TAG, "ES8311 speaker codec init failed");
        return false;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 2;
    fs.channel_mask = 0;
    fs.sample_rate = SYNTH_SAMPLE_RATE;
    fs.mclk_multiple = 256;

    int ret = esp_codec_dev_open(_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed: %d", ret);
        return false;
    }

    esp_codec_dev_set_out_vol(_codec, SYNTH_VOLUME);
    esp_codec_dev_set_out_mute(_codec, false);

    BaseType_t task_ret = xTaskCreate(taskEntry,
                                      "piano_synth",
                                      6144,
                                      this,
                                      10,
                                      nullptr);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "audio task creation failed");
        return false;
    }

    _ready = true;
    ESP_LOGI(TAG,
            "audio initialized: ES8311 %d Hz stereo 16-bit, volume=%d, master_gain=%d%%, voice_gain=%d%%, bass_gain=%d%%, treble_gain=%d%%, pa_gpio=%d",
             SYNTH_SAMPLE_RATE,
             SYNTH_VOLUME,
             static_cast<int>(SYNTH_MASTER_GAIN * 100.0f),
             static_cast<int>(SYNTH_VOICE_GAIN * 100.0f),
             static_cast<int>(PIANO_SYNTH_BASS_GAIN * 100.0f),
             static_cast<int>(PIANO_SYNTH_TREBLE_GAIN * 100.0f),
             static_cast<int>(BSP_POWER_AMP_IO));
    return true;
}

void PianoSynth::noteOn(uint8_t note, uint8_t velocity)
{
    if (!_ready || _queue == nullptr) {
        return;
    }
    if (velocity == 0) {
        noteOff(note);
        return;
    }
    NoteMsg msg = {note, velocity};
    xQueueSend(static_cast<QueueHandle_t>(_queue), &msg, 0);
}

void PianoSynth::noteOff(uint8_t note)
{
    if (!_ready || _queue == nullptr) {
        return;
    }
    NoteMsg msg = {note, 0};
    xQueueSend(static_cast<QueueHandle_t>(_queue), &msg, 0);
}

void PianoSynth::buildSinTable()
{
    for (int i = 0; i < SYNTH_SIN_LEN; ++i) {
        _sinTable[i] = sinf(SYNTH_TWO_PI * static_cast<float>(i) / static_cast<float>(SYNTH_SIN_LEN));
    }
}

float PianoSynth::noteFrequency(uint8_t note)
{
    return 440.0f * powf(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

void PianoSynth::activateVoice(uint8_t note, uint8_t velocity)
{
    for (int i = 0; i < SYNTH_VOICES; ++i) {
        if (_voices[i].active && _voices[i].note == note) {
            _voices[i].amp = (static_cast<float>(velocity) / 127.0f) * SYNTH_VOICE_GAIN * note_gain(note);
            _voices[i].release = 0.0f;
            return;
        }
    }

    int voice_index = -1;
    float min_amp = 999.0f;
    for (int i = 0; i < SYNTH_VOICES; ++i) {
        if (!_voices[i].active) {
            voice_index = i;
            break;
        }
        if (_voices[i].amp < min_amp) {
            min_amp = _voices[i].amp;
            voice_index = i;
        }
    }

    if (voice_index < 0) {
        return;
    }

    Voice &voice = _voices[voice_index];
    voice.note = note;
    voice.phase = 0.0f;
    voice.inc = noteFrequency(note) / static_cast<float>(SYNTH_SAMPLE_RATE);
    voice.amp = (static_cast<float>(velocity) / 127.0f) * SYNTH_VOICE_GAIN * note_gain(note);
    voice.release = 0.0f;
    voice.active = true;
}

void PianoSynth::releaseVoice(uint8_t note)
{
    for (int i = 0; i < SYNTH_VOICES; ++i) {
        if (_voices[i].active && _voices[i].note == note && _voices[i].release == 0.0f) {
            _voices[i].release = _voices[i].amp / (static_cast<float>(SYNTH_SAMPLE_RATE) * 0.15f);
        }
    }
}

void PianoSynth::taskEntry(void *arg)
{
    static_cast<PianoSynth *>(arg)->audioTask();
}

void PianoSynth::audioTask()
{
    int16_t buffer[SYNTH_BUFFER_FRAMES * 2];
    NoteMsg msg = {};

    while (true) {
        while (xQueueReceive(static_cast<QueueHandle_t>(_queue), &msg, 0) == pdTRUE) {
            if (msg.velocity > 0) {
                activateVoice(msg.note, msg.velocity);
            } else {
                releaseVoice(msg.note);
            }
        }

        for (int i = 0; i < SYNTH_BUFFER_FRAMES; ++i) {
            float sample = 0.0f;

            for (int voice_index = 0; voice_index < SYNTH_VOICES; ++voice_index) {
                Voice &voice = _voices[voice_index];
                if (!voice.active) {
                    continue;
                }

                const int table_index = static_cast<int>(voice.phase * SYNTH_SIN_LEN) & (SYNTH_SIN_LEN - 1);
                sample += voice.amp * _sinTable[table_index];

                voice.phase += voice.inc;
                if (voice.phase >= 1.0f) {
                    voice.phase -= 1.0f;
                }

                if (voice.release > 0.0f) {
                    voice.amp -= voice.release;
                    if (voice.amp <= 0.0f) {
                        voice.amp = 0.0f;
                        voice.active = false;
                    }
                }
            }

            sample *= SYNTH_MASTER_GAIN;
            if (sample > 0.95f) {
                sample = 0.95f;
            } else if (sample < -0.95f) {
                sample = -0.95f;
            }

            const int16_t sample_i16 = static_cast<int16_t>(sample * 30000.0f);
            buffer[i * 2] = sample_i16;
            buffer[i * 2 + 1] = sample_i16;
        }

        const int ret = esp_codec_dev_write(_codec, buffer, sizeof(buffer));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "codec write failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
