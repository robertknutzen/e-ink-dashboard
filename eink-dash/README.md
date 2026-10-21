# eink-dash

A conky-style dashboard for the Waveshare 7.5" e-paper (GDEY075T7, 800×480, black/white) on the Waveshare ESP32 driver board. A text config file decides what is drawn and where the data comes from. The file lives on the ESP32, and you edit it in a browser at **http://eink.local/** (or the board's IP address). Press **Save** and the screen updates. No reflashing needed.

## Build and flash

```sh
cp secrets.example.h secrets.h      # put your Wi-Fi details in it (or use setup mode, below)
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_ota eink-dash
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=no_ota -p /dev/ttyACM0 eink-dash
```

The `no_ota` partition scheme is required because the firmware is too big for the default one. In the Arduino IDE, choose *Tools → Partition Scheme → No OTA (2MB APP/2MB SPIFFS)*. Libraries: GxEPD2, Adafruit GFX, ArduinoJson 7.

**Setup mode:** if Wi-Fi isn't set or can't connect, the board creates a Wi-Fi network called `eink-setup`. Join it, open http://192.168.4.1/ and add a line `wifi "network" "password"`.

**From the command line:**
```sh
curl http://eink.local/config.txt > config.txt          # download
curl --data-binary @config.txt http://eink.local/config.txt   # upload + apply
```
A config with errors is rejected and the reply lists each bad line by number. The old config keeps running.

## Fitting a picture frame (calibration)

If a frame or matte hides the edges of the screen, open **http://eink.local/calibrate**. The display switches to a calibration screen with a ruler on each edge. Each ruler counts pixels in from its edge, and a box is drawn at the margins you enter.

0. Set the orientation first using **Flip 180°** or **Rotate 90°** on that page. The rulers turn with it.
1. On each side, read the first number or tick you can see and type it into the matching box. Press **Preview**.
2. Use the −5 / +5 buttons until all four sides of the box are **just** visible inside the frame.
3. Press **Save margins**. This writes a `margin` line into your config, and from then on `0 0` is the top-left corner you can see.

Calibration mode pauses the dashboard. It ends when you save or cancel, or by itself after 10 minutes.

## Config syntax

One statement per line. `#` starts a comment. Text with spaces goes in `"quotes"`, and `\n` inside quotes starts a new line.

### Settings
| line | meaning |
|---|---|
| `wifi "ssid" "password"` | overrides secrets.h (the board restarts when this changes) |
| `timezone CST6CDT,M3.2.0,M11.1.0` | POSIX TZ string ([list](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv)) |
| `ntp pool.ntp.org` | time server |
| `redraw 60s` | how often to redraw (`s`/`m`/`h`). Redraws land on the clock: every whole minute, every 5 minutes, and so on |
| `full_refresh_every 10` | number of fast partial refreshes before a full one, to clear ghosting |
| `rotation 2` | 0 = upright, 2 = upside down, 1 / 3 = portrait (the layout area becomes 480 wide × 800 tall). The **Flip 180°** and **Rotate 90°** buttons on the web page set this for you and move any `margin` values with it, so they still match the frame |
| `margin 80 10 20 15` | pixels hidden by a frame or matte: left, top, right, bottom (one number sets all four). Measure them with **/calibrate**, see below |

### Data sources
```
source NAME "URL" every 15m [header "Name: value"]...
```
Fetches a URL on its own schedule. JSON responses can be read field by field. Any other response is kept as plain text, which `${NAME}` shows. If a fetch fails, the last good data stays on screen and `!NAME` appears in the top-right corner. The web page shows each source's status and a preview of its data, which helps you find the paths you need.

### Widgets
```
text X Y [options] "template"
list X Y items=PATH [max=10] [spacing=PX] [options] "template using ${item...}"
line X1 Y1 X2 Y2 [color=black|white]
rect X Y W H [fill] [radius=R] [color=black|white]
```
Coordinates are measured from the top-left of the **visible area**, i.e. inside the `margin`. **Negative X/Y count from the right/bottom visible edge**, so `text -10 -10 align=right "${ip}"` hugs the bottom-right corner. A `rect` width or height of 0 or less stretches to that many pixels before the far edge: `rect 0 0 0 0` outlines the whole visible area.
For text, X,Y is the **left end of the baseline**. With `align=center` or `align=right`, X is the centre or right edge instead.
Options: `font=` `size=` (1–10, scales the font up) `align=left|center|right` `color=black|white`.
A `list` repeats its template for each element of a JSON array, moving down one row each time. If the array is missing or empty, it draws nothing.
Any widget can take `if=PATH`, so it's drawn only when PATH has data (not missing, false, empty text or an empty list), or `unless=PATH` for the opposite. For example, to show a message when there are no buses:
`text 370 85 unless=bus.bustime-response.prd "${bus.bustime-response.error[0].msg|default:No buses}"`

**Fonts:** `sans`, `sansbold`, `serif`, `serifbold`, `mono`, `monobold`, each in sizes `9`, `12`, `18`, `24`. Examples: `sansbold24`, `mono12`. Use `mono` for lists that need lined-up columns. The fonts only cover plain ASCII. `°` is drawn specially, a few common symbols are replaced with lookalikes, and anything else shows as `?`.

### Templates
| placeholder | value |
|---|---|
| `${time %H:%M}` | current time, [strftime](https://cplusplus.com/reference/ctime/strftime/) format |
| `${weather.current.temperature_2m}` | a field from source `weather` |
| `${src.list[0].name}`, `${src.list[-1]}` | array items (negative counts from the end) |
| `${src["key.with.dots"]}` | keys that contain dots |
| `${item.field}` | the current element inside a `list` |
| `${ip}`, `${rssi}` | the board's IP address and Wi-Fi signal strength |

A missing value shows as `?`. Filters are chained with `|`:

| filter | effect |
|---|---|
| `round`, `round:1` | round a number to 0 or N decimals |
| `int` | whole number (`"08"` → `8`) |
| `upper`, `lower` | change case |
| `trunc:N`, `lpad:N`, `rpad:N` | cut or pad to N characters (useful for columns) |
| `unit: min` | append text only when the value is a number (`5` → `5 min`, `DUE` stays `DUE`) |
| `wmo` | Open-Meteo `weather_code` → text such as "Light rain" |
| `mins` | timestamp → minutes from now (for arrival times) |
| `time:%a %H:%M` | reformat a timestamp |
| `default:text` | use this text when the value is missing |

`mins` and `time:` accept unix seconds or milliseconds, ISO-8601 (`2026-10-21T14:32:00Z`, `+02:00` offsets, or no zone, which means local time), plain dates (`2026-10-21`), CTA-style `20261021 14:32`, and bare `14:32`.

## Examples

**Weather** (Open-Meteo, no API key): see `default_config.h`. You can build the URL at https://open-meteo.com/en/docs.

**CTA bus arrivals** (free key from ctabustracker.com):
```
source bus "http://www.ctabustracker.com/bustime/api/v2/getpredictions?key=KEY&stpid=1836&format=json" every 1m
list 45 390 items=bus.bustime-response.prd max=3 spacing=26 font=mono12 "${item.rt|rpad:4} ${item.des|trunc:24|rpad:25} ${item.prdctdn} min"
```
Other transit APIs work the same way as long as they return JSON. GTFS-Realtime (protobuf) feeds can't be read directly. Put a small JSON proxy in front of them.

**Text from your own server:**
```
source note "http://10.0.0.5:8000/note.txt" every 5m
text 400 440 align=center font=serif18 "${note}"
```

## Limits
- Responses over about 160 KB are refused. Choose API options that return less data.
- HTTPS certificates aren't checked, since the data is only displayed.
- The board stays awake to serve the editor, so it expects USB power. Don't run it from a battery.

## Files
`eink-dash.ino` (main loop, Wi-Fi, scheduling) · `config.cpp` (parser) · `sources.cpp` (HTTP fetching) · `tmpl.cpp` (`${}` templates and filters) · `render.cpp` (drawing, fonts, display pins) · `web.cpp` (editor) · `default_config.h` (config written on first boot)
