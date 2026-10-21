#pragma once

// Written to flash as /config.txt the first time the board boots (or when the
// stored config is missing). After that, edit it from the web page.
static const char DEFAULT_CONFIG[] = R"CFG(# eink-dash config. Edit me at http://eink.local/ and press Save.
# Visible area is 695 x 440 (inside the frame). 0,0 = top-left you can see.
# Negative x/y count from the right/bottom edge. Text x,y = left end of baseline.

margin 30 40 75 0   # left top right bottom (set by /calibrate)
rotation 2   # upside down
timezone CST6CDT,M3.2.0,M11.1.0   # America/Chicago
redraw 60s
full_refresh_every 10

# ---------- data ----------
source weather "https://api.open-meteo.com/v1/forecast?latitude=41.88&longitude=-87.63&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=1" every 15m
# Get a free key at ctabustracker.com and set your stop id:
source bus "http://www.ctabustracker.com/bustime/api/v2/getpredictions?key=YOUR_KEY&stpid=1530&format=json" every 1m

# ================= left column (x 15-335) =================
# ---------- time & date ----------
text 15 80 font=sansbold24 size=2 "${time %I|int}:${time %M}"
text 17 112 font=sans12 "${time %A, %B %d}"
line 15 130 335 130

# ---------- current weather ----------
text 15 210 font=sansbold24 size=2 "${weather.current.temperature_2m|round}°"
text 180 168 font=sans12 "${weather.current.weather_code|wmo}"
text 180 192 font=sans9 "Feels like ${weather.current.apparent_temperature|round}°"
text 180 212 font=sans9 "Wind ${weather.current.wind_speed_10m|round} mph"
line 15 235 335 235

# ---------- today ----------
text 15 268 font=sansbold12 "Today"
text 15 305 font=sans18 "High ${weather.daily.temperature_2m_max[0]|round}°  Low ${weather.daily.temperature_2m_min[0]|round}°"
text 15 340 font=sans12 "${weather.daily.precipitation_probability_max[0]}% chance of rain"

# ================= right column (x 370-680): bus arrivals =================
line 350 15 350 -15
text 370 45 font=sansbold18 "Next buses"
list 370 88 items=bus.bustime-response.prd max=6 spacing=54 font=sansbold12 "${item.rt}  ${item.des|trunc:14}"
list -15 88 items=bus.bustime-response.prd max=6 spacing=54 font=sansbold12 align=right "${item.prdctdn|unit: min}"
list 370 109 items=bus.bustime-response.prd max=6 spacing=54 font=sans9 "${item.rtdir} - arrives ${item.prdtm|time:%I:%M}"
text 370 88 unless=bus.bustime-response.prd font=sans12 "${bus.bustime-response.error[0].msg|default:No arrival info}"

text 15 -8 font=sans9 "${ip}"
)CFG";
