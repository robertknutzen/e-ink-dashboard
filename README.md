# e-ink-dashboard

A conky-style dashboard for a Waveshare 7.5" e-paper display driven by an ESP32. One text config file controls the whole screen: the data sources (weather, bus arrivals, any JSON API) and where everything goes. The config lives on the board, and you edit it from your browser at `http://eink.local/`. No reflashing needed.

```
source weather "https://api.open-meteo.com/v1/forecast?...&current=temperature_2m" every 15m
text 15 80  font=sansbold24 size=2 "${time %I|int}:${time %M}"
text 15 210 font=sansbold24 size=2 "${weather.current.temperature_2m|round}°"
list 370 88 items=bus.bustime-response.prd max=6 "${item.rt}  ${item.prdctdn|unit: min}"
```

**Features:**
- **Web editor:** edit the config in the browser, with errors reported by line number.
- **Data sources:** any JSON or plain-text URL, each refreshed on its own schedule, with `${template}` placeholders and filters to show the values.
- **Picture frames:** a calibration screen measures how much a frame hides and lays everything out inside the visible area. One-click rotation.
- **Fast refreshes:** the clock updates once a minute with a fast partial refresh, and a periodic full refresh clears ghosting.
- **Wi-Fi setup:** if Wi-Fi isn't configured, the board creates its own network so you can set it up from your phone.

**Everything is in [`eink-dash/`](eink-dash/).** The [README there](eink-dash/README.md) covers hardware, build and flash steps, and the full config reference.
