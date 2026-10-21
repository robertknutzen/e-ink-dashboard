#pragma once
#include <ArduinoJson.h>
#include "config.h"

// Applies a new list of sources. Data already fetched for a source with the
// same name and URL is kept, so saving the config doesn't blank the screen.
void sourcesConfigure(const std::vector<SourceDef> &defs);

// Fetches every source whose interval has elapsed (all of them when force).
// Returns true if anything changed in a way worth redrawing for.
bool sourcesPoll(bool force);

// Root of a source's last good data (null if unknown or never fetched).
// Non-JSON responses are stored as a single string.
JsonVariantConst sourceRoot(const String &name);

// Names of sources whose last fetch failed, separated by spaces.
String sourcesFailing();

// One HTML table row per source, for the web editor.
String sourcesStatusHtml();
