#pragma once

#include "MIDITransport.h"

bool piano_wifi_begin();
bool piano_wifi_ready();
MIDITransport *piano_wifi_transport();
int piano_wifi_peer_count();
