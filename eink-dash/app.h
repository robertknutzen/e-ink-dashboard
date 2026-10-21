#pragma once
#include "config.h"

// Shared state and actions implemented in eink-dash.ino.

extern Config gConfig;
extern String gConfigText;
extern String gConfigErrors;  // errors in the running config (shown on screen)

// Parses `text`; if valid, saves it to flash and makes it live.
// Returns false (and fills `errors`) without changing anything if invalid.
bool applyConfigText(const String &text, String &errors);

// Ask the main loop to refetch all sources and redraw on its next pass.
void requestRefresh(bool full);

// Calibration mode: shows edge rulers and a box at margins m (left, top,
// right, bottom) instead of the dashboard. Ends on save, cancel, or after 10 min.
void calibrationShow(const int m[4]);
void calibrationEnd();
bool calibrationActive();
const int *calibrationMargins();
