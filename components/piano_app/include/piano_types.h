#pragma once

#include <stdint.h>

struct PianoInfo {
    int note_count;
    int root_midi;
    char note_text[80];
    char last_event[40];
    float root_freq;
};
