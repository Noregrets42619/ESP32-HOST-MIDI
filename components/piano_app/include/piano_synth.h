#pragma once

#include <stdint.h>

class PianoSynth {
public:
    PianoSynth();

    bool begin();
    bool ready() const { return _ready; }
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);

private:
    struct Voice {
        float phase;
        float inc;
        float amp;
        float release;
        uint8_t note;
        bool active;
    };

    struct NoteMsg {
        uint8_t note;
        uint8_t velocity;
    };

    void buildSinTable();
    void activateVoice(uint8_t note, uint8_t velocity);
    void releaseVoice(uint8_t note);
    static float noteFrequency(uint8_t note);
    static void taskEntry(void *arg);
    void audioTask();

    void *_codec;
    void *_queue;
    Voice _voices[8];
    float _sinTable[512];
    bool _ready;
};
