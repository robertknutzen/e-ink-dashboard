#pragma once
#include "config.h"

// Sets up SPI and the GxEPD2 driver (pins match the Waveshare ESP32 driver board).
void displayInit();

// Draws all widgets. `full` = full refresh (slow, flashes, clears ghosting);
// otherwise a fast partial refresh. `notice` is shown in a footer bar
// (config errors, setup-mode hints); empty for none.
void renderAll(const Config &cfg, const String &notice, bool full);

// Draws a plain message screen (used at boot / setup).
void renderMessage(const Config &cfg, const String &title, const String &body);

// Draws edge rulers and a box at margins m = {left, top, right, bottom}.
void renderCalibration(const Config &cfg, const int m[4], bool full);

bool fontExists(const String &name);
