#pragma once

#include "MIDITransport.h"

bool piano_ble_begin();
bool piano_ble_ready();
bool piano_ble_connected();
MIDITransport *piano_ble_transport();
