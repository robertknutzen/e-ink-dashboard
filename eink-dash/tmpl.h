#pragma once
#include <ArduinoJson.h>

// Expands ${...} placeholders in a template. `item` is the current array
// element inside a list widget (null elsewhere).
String expandTemplate(const String &tmpl, JsonVariantConst item);

// Looks up a dotted path like "weather.current.temperature_2m" or
// "item.arrivals[0].eta". The first segment is a source name or "item".
JsonVariantConst resolvePath(const String &path, JsonVariantConst item);

// True once NTP has set the clock.
bool timeValid();
