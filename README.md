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

## Running the kit

Everything is already flashed and wired — power it up and open the dashboard. You only need a PC
for the two things below: changing the Wi-Fi credentials, and looking at serial logs while flashing.

### 1. Power it on

The 5 V power board feeds the ESP32-S3 and the 5 V sensors (MQ-2, LD2410C, servo). Turn it on and
the node boots in ~3 s.

- The RGB status LED stays **off** until the radar sees someone, then turns **white**.
- A gas alarm turns it **red**.
- USB-C into a PC is **not needed for normal use** — it is the data link for flashing and logs.
  If you plug it in while the 5 V board is on, you have two 5 V sources in parallel; prefer one.
- Powering the board from USB-C alone also works: put the sensors' 5 V on the board's `5V` pin
  (a 2 A port is comfortable, because Wi-Fi bursts draw ~350–500 mA).

### 2. Open the dashboard

**https://gorelace.github.io/iot-smarthome/?prefix=iot-smarthome/home**

It runs on a phone or a laptop, needs internet, and can be installed as an app (Chrome/Edge:
install icon in the address bar; iPhone: Share → Add to Home Screen). In **Settings** the
**Topic prefix** must be `iot-smarthome/home` — it is set by the `?prefix=` link above and then
remembered.

### 3. Change the Wi-Fi credentials

The SSID and password are compiled into the firmware, so a different network needs one rebuild.
Only a 2.4 GHz network works (the ESP32-S3 cannot see 5 GHz).

1. Install PlatformIO: `pip install platformio` (or the PlatformIO IDE extension in VS Code).
2. Make the secrets file and edit it:
   ```bash
   cd firmware
   cp include/secrets.example.h include/secrets.h
   ```
   Set `WIFI_SSID` and `WIFI_PASS`. Keep `MQTT_BASE` as `iot-smarthome/home` (or change it
   *and* the dashboard's Topic prefix to match).
3. Connect the USB-C cable to the port labelled **USB** (the native port, no driver needed on
   Windows 10/11 or Linux). A COM port appears — on this machine it was `COM3`.
4. Flash:
   ```bash
   pio run -e esp32s3-usb -t upload            # native "USB" port
   pio run -e esp32s3 -t upload                # "UART/COM" port instead
   ```
   If it hangs on "Connecting…", hold **BOOT**, tap **RST**, release **BOOT**.
5. **Press RST once** after the upload. On the native USB port the toolchain leaves the chip in
   ROM download mode; the RST press is what actually starts the firmware. Not a defect.
6. Watch it boot if you want serial logs: `pio device monitor` (115200 baud). You should see
   `[WiFi] connected, IP …` then `[MQTT] … ok`.

### 4. Start-up check (about a minute)

1. Dashboard shows **ESP32 online** within ~10 s.
2. Wave at the radar (≤ 4 m): LED turns white, the presence tile shows the distance.
3. Temperature/humidity appear in ~3 s; gas needs a **60 s warm-up** before ppm is trusted.
4. Clean air should read a few ppm. If it reads high, run 5–10 min in clean air and press
   **Calibrate MQ-2** in Settings (the calibration value survives reboots).

### 5. If something is not working

| Symptom | Cause | Fix |
|---|---|---|
| PWR LED on, but never appears online, no serial output | chip parked in ROM download mode after flashing | press **RST** once |
| No Wi-Fi after moving it to another network | credentials are compiled in, or the network is 5 GHz | rebuild with `secrets.h` (step 3) |
| Presence stays “no sensor”, gas reads `0 mV` | 5 V rail off | check the power board and the radar/MQ-2 `5V`/GND wires |
| Gas ppm high in clean air | `R0` calibrated on a cold sensor | 5–10 min clean air → **Calibrate MQ-2** |
| Dashboard empty but the node is online | Topic prefix mismatch | Settings → Topic prefix = `MQTT_BASE` |
| Servo hums but does not turn | servo on 3V3, or a weak 5 V supply | servo VCC on 5 V, GND shared |
| LED dark while someone is in front of the radar | wired as common-anode, or too dim | `LED_COMMON_ANODE 1` in `config.h`, rebuild |

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
| RGB status LED | R | **GPIO 36** (through a 220–330 Ω resistor to GND) |
| | G | **GPIO 37** (through a 220–330 Ω resistor to GND) |
| | B | **GPIO 38** (through a 220–330 Ω resistor to GND) |

> **Status LED:** common-cathode RGB LED, one GPIO per channel. It lights white while the
> LD2410C reports presence (the "presence scenario"), goes red during a gas alarm, and is off
> when the room is empty. Common-anode LED? Set `LED_COMMON_ANODE 1` in
> `firmware/include/config.h`. Boards with **octal PSRAM** (N8R8/N16R8) reserve GPIO 33–37 for
> flash/PSRAM — on those, pick free pins (e.g. 40/41/42) and update the `PIN_LED_*` defines.

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

Hosted, nothing to install: **https://gorelace.github.io/iot-smarthome/?prefix=iot-smarthome/home**
(see §4 for the Pages setup). Choose the broker preset once in **Settings**, set
**Topic prefix** = your `MQTT_BASE`, press Connect. Settings are remembered; they can also be
passed in the URL: `?prefix=nhom5-iot/home`.

Shows: gas-safety banner, temperature / humidity / gas / presence / light / Wi-Fi tiles, live charts (5 min / 15 min / 1 h),
IR codes received, IR "learn" button, gas threshold, publish interval, MQ-2 calibration. VI/EN and light/dark toggles.

Working offline or on a LAN-only broker? `dashboard/index.html` also opens directly from the
filesystem (`?prefix=…` works there too) — for `ws://localhost:9001` that is the only option,
because an https page cannot use a plain `ws://` broker.

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
`.github/workflows/page.yml` publishes `dashboard/` on every push to `main`.

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
- The on-board RGB LED (GPIO 36/37/38) is the local presence indicator: white while someone is in
  the room, off when it is empty, red on a gas alarm. It refreshes when `presence` changes and turns
  off 5 s after the radar stops reporting.

## Next steps (from the slides)

- Relay / buzzer for the gas safety chain → call them from `setAlarm()`.
- Adaptive lighting: LD2410C presence + BH1750 lux → relay or lamp (both already publish telemetry).
- Telegram alert and Home Assistant / Node-RED (they can subscribe to the same topics).
