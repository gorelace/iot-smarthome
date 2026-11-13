# IoT Smart Home Automation (ESP32-S3 · MQTT · IR)

Edge node + web dashboard for the 4-layer smart-home design in the slides:
sensors → ESP32-S3 (edge rules) → MQTT broker → dashboard.

Current hardware: **ESP32-S3 + DHT22 + MQ-2 + BH1750 + LD2410C mmWave + VS1838B IR receiver + IR blaster + servo**. Relay and buzzer are planned, not wired yet.

```
firmware/    PlatformIO project for the ESP32-S3
dashboard/   index.html: single-file web dashboard (MQTT over WebSockets)
broker/      mosquitto.conf for a local broker
tools/       simulate.py: fake ESP32 for testing the dashboard without hardware
```

## Wiring

| Sensor | Pin on sensor | ESP32-S3 |
|---|---|---|
| VS1838B IR receiver | OUT | **GPIO 5** |
| | VCC / GND | 3V3 / GND |
| MQ-2 | AO | **GPIO 6** (through a divider, see below) |
| | VCC / GND | 5V / GND |
| DHT22 | DATA | **GPIO 7** (10k pull-up to 3V3 if it's a bare sensor, modules have one) |
| | VCC / GND | 3V3 / GND |
| LD2410C mmWave | VCC / GND | 5V / GND |
| | TX | **GPIO 18** (ESP32 RX, UART1) |
| | RX | **GPIO 17** (ESP32 TX, UART1 — only needed to configure the module) |

> **LD2410C:** the module streams target frames at **256000 baud** (factory default) and its
> UART is 3.3 V logic, so it connects directly. ESP32 RX (18) ← module TX, ESP32 TX (17) → module RX.
> Power it from 5 V; the TX pin still only swings to 3.3 V.

> **MQ-2 voltage:** powered from 5 V, its AO pin can go above 3.3 V and damage the ESP32-S3.
> Use a divider: `AO → 10k → GPIO6 → 20k → GND`. The firmware assumes this (`MQ2_DIVIDER 1.5` in
> `firmware/include/config.h`). If you wired AO straight to GPIO6, set `MQ2_DIVIDER 1.0` and
> ideally add the divider soon.

## 1. Flash the firmware

Needs [PlatformIO](https://platformio.org/) (`pip install platformio`, or the VS Code extension).

1. Copy `firmware/include/secrets.example.h` → `firmware/include/secrets.h`, fill in Wi-Fi (2.4 GHz only)
   and the broker. On the public HiveMQ broker, change `MQTT_BASE` to something unique like `nhom5-iot/home`.
2. Plug the board into the USB-C port marked **UART/COM** and run:
   ```bash
   cd firmware
   pio run -t upload
   pio device monitor
   ```
   Using the port marked **USB** instead? Use `pio run -e esp32s3-usb -t upload`.
   If upload hangs on "Connecting…", hold **BOOT**, tap **RST**, release BOOT.

Serial output looks like:
```
[WiFi] connected, IP 192.168.1.50, RSSI -58
[MQTT] connecting to broker.hivemq.com:1883 as esp32s3-a1b2c3 ... ok
[TELE] T=29.8C H=68.2% gas=412mV (warming) 0ppm pres=clear rssi=-58
[IR] NEC addr=0x0 cmd=0x45 raw=0xBA45FF00 bits=32
```

## 2. Pick a broker

| | Setup | ESP32 `MQTT_HOST` | Dashboard URL |
|---|---|---|---|
| **HiveMQ public** (default) | none | `broker.hivemq.com`, 1883 | `wss://broker.hivemq.com:8884/mqtt` |
| **Mosquitto on laptop** | `winget install EclipseFoundation.Mosquitto`, then `"C:\Program Files\mosquitto\mosquitto.exe" -c broker\mosquitto.conf -v` | laptop's Wi-Fi IPv4 (`ipconfig`), 1883 | `ws://localhost:9001` |
| **HiveMQ Cloud** (free, private) | create a cluster + user at console.hivemq.cloud | cluster host, 8883, `MQTT_USE_TLS 1` | `wss://<cluster>:8884/mqtt` + user/pass |

The public broker is fine for demos, but anyone can read and publish to your topics.
With Mosquitto, Windows Firewall must allow inbound TCP 1883 (Windows asks the first time
mosquitto.exe listens; choose "Private networks").

## 3. Open the dashboard

Open `dashboard/index.html` in a browser (double-click works). Open **Kết nối broker / Broker
connection**, choose the preset, set **Topic prefix** = your `MQTT_BASE`, press Connect.
Settings are remembered. You can also pass them in the URL: `index.html?prefix=nhom5-iot/home`.

Shows: gas-safety banner, temperature / humidity / gas / presence / light / Wi-Fi tiles, live charts (5 min / 15 min / 1 h),
IR codes received, IR "learn" button, gas threshold, publish interval, MQ-2 calibration. VI/EN and light/dark toggles.

No hardware yet? Run the simulator and point the dashboard at the same broker/prefix:
```bash
pip install paho-mqtt
python tools/simulate.py --prefix nhom5-iot/home --gas-spike
```

## 4a. Local app (no deploy)

Double-click `start-dashboard.bat` → opens `http://localhost:8000` (Python's built-in server, this PC only).
In Chrome/Edge click the **Install** icon in the address bar → the dashboard gets its own window,
Start-menu entry and desktop icon. After that the app opens even without the .bat running
(the service worker caches it); run the .bat again only after you change `index.html`.
Always use port 8000: the installed app is tied to `http://localhost:8000`.
Works with Mosquitto on the same laptop (`ws://localhost:9001`) as well as HiveMQ.

## 4. Web app (PWA) on GitHub Pages

The dashboard is an installable web app (`manifest.webmanifest`, `sw.js`, `icons/`).
`.github/workflows/pages.yml` publishes `dashboard/` on every push to `main`.

1. One time: GitHub repo → **Settings → Pages → Source = GitHub Actions**.
2. `git push` → after ~1 min it's live at **https://gorelace.github.io/iot-smarthome/**
   (link with your prefix: `https://gorelace.github.io/iot-smarthome/?prefix=nhom5-iot/home`).
3. Install on a phone: Android Chrome → ⋮ → *Install app*; iPhone Safari → Share → *Add to Home Screen*.
   On a PC, Chrome/Edge show an install icon in the address bar.

Notes:
- The page is https, so the broker must be **wss://** (HiveMQ public or HiveMQ Cloud).
  `ws://<laptop-ip>:9001` (local Mosquitto) is blocked by the browser on the hosted page; for that,
  serve it locally instead: `cd dashboard && python -m http.server 8000` → `http://localhost:8000`.
- After changing `index.html`, bump `VERSION` in `sw.js` so installed apps fetch the new version.
- Anyone with the link can open the page; with the public broker they can also send commands.
  For a real deployment use HiveMQ Cloud with a username/password.

## MQTT topics

`<base>` = `MQTT_BASE` (default `home`), room = `livingroom`.

| Topic | Direction | Payload |
|---|---|---|
| `<base>/livingroom/sensors` | ESP32 → | every 3 s: `{"temp":29.8,"hum":68.2,"heat_index":33.1,"lux":420,"gas_mv":412,"gas_ppm":180,"gas_ready":true,"gas_alarm":false,"gas_threshold":1000,"presence":true,"presence_moving":false,"presence_stationary":true,"presence_dist":81,"presence_energy":59,"mmwave_ok":true,"rssi":-58,"uptime_s":120}` |
| `<base>/livingroom/status` | ESP32 → | `online` / `offline` (retained; `offline` is the Last Will) |
| `<base>/livingroom/config` | ESP32 → | current settings (retained) |
| `<base>/livingroom/config/set` | → ESP32 | `{"gas_threshold":1200}`, `{"interval_ms":5000}`, `{"cmd":"calibrate"}`, `{"cmd":"reboot"}`, `{"cmd":"clear_ir"}` |
| `<base>/livingroom/ir/rx` | ESP32 → | each decoded remote press: `{"protocol":"NEC","address":"0x0","command":"0x45","raw":"0xBA45FF00","bits":32}` |
| `<base>/livingroom/ir/learn` | → ESP32 | `{"device":"fan","cmd":"power"}`: the next code received within 15 s is saved to flash |
| `<base>/livingroom/ir/learn/state` | ESP32 → | `waiting` / `saved` / `timeout` |
| `<base>/safety/alert` | ESP32 → | `{"type":"gas","level":"alarm"\|"clear","gas_ppm":2100,"threshold":1000}` (retained, QoS 1) |

Watch everything from a terminal: `mosquitto_sub -h broker.hivemq.com -t "home/#" -v`.

## MQ-2 notes

- First 60 s after power-on the heater warms up; `gas_ready` is false and no ppm is reported.
- On first boot, after warm-up, the firmware calibrates R0 assuming **clean air** and saves it to flash.
  If you powered it up near gas or smoke, press **Calibrate** on the dashboard later in clean air.
- ppm is an LPG-equivalent *estimate* from the datasheet curve. Fine for alarms and trends, not for measurement.
- The alarm logic runs on the ESP32 itself (edge-first), so it still fires if Wi-Fi or the broker is down.
  Add a buzzer by setting `PIN_BUZZER` in `config.h`.

## LD2410C notes

- `presence` is true when the radar sees a moving and/or stationary target; `presence_dist` is the
  detection distance in cm, `presence_moving` / `presence_stationary` say which kind of target it is.
- The firmware reads the frames the module streams by itself (header `F4 F3 F2 F1`), so nothing has to be
  configured first. `mmwave_ok` turns false when no frame arrived for 5 s (wiring, 5 V power or wrong baud).
- Distances are the module's own measurements (gate resolution 0.75 m by default, interpolated by the
  module), so treat them as room-scale, not precise.
- Engineering mode (per-gate energy) is off by default; if you enable it in the vendor app the firmware
  still reads the target fields (the gate energies are ignored).

## Next steps (from the slides)

- Relay / buzzer for the gas safety chain → call them from `setAlarm()`.
- Adaptive lighting: LD2410C presence + BH1750 lux → relay or lamp (both already publish telemetry).
- Telegram alert and Home Assistant / Node-RED (they can subscribe to the same topics).
