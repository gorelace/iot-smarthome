# Handover / run book

Everything needed to run the kit without the original author. Two power modes, one
network requirement, and a short list of failure modes with fixes.

---

## 0. What is in the box

| Item | Notes |
|---|---|
| ESP32-S3 DevKitC-1 | already flashed, firmware version shown in the dashboard (`fw`) |
| DHT22 | GPIO 7 (3V3) |
| MQ-2 gas sensor | GPIO 6 through the 10k/20k divider, **5 V** |
| LD2410C mmWave radar | module TX → GPIO 18, RX → GPIO 17, **5 V**, 256000 baud |
| VS1838B IR receiver | GPIO 5 (3V3) |
| IR blaster LED | GPIO 15 (via driver) |
| Servo | signal GPIO 4, **power from 5 V**, shared GND |
| RGB status LED | R 36 / G 37 / B 38, 220–330 Ω per channel, common → GND |
| BH1750 light sensor | **not fitted** — the firmware logs `[I2C] devices: none` and retries every 10 s |
| 5 V power board | feeds the radar, MQ-2, servo (and the board itself if running without USB) |
| USB-C cable | flashing + serial logs + alternative power |

---

## 1. Power

### A. With USB-C (recommended for a demo)
- Any USB port on a laptop, ideally 2 A. The board and the 5 V sensors can all run from it
  (sensors on the board's `5V` pin).
- You get a COM port, so logs and re-flashing work.
- If Wi-Fi bursts cause resets, use a powered hub or a USB-C charger instead of a weak laptop port.

### B. Without USB-C (5 V supply only)
- Regulated **5 V, ≥ 1 A** to the board's `5V` pin, **GND shared** with the sensors.
- Wi-Fi and MQTT keep working; there is simply **no COM port** (that is normal — USB carries data).
- Use **one** 5 V source at a time. The `5V` pin is tied to VBUS, so feeding it while USB-C is
  plugged in puts two supplies in parallel.
- After changing cables, press **RST** once (see §6, download mode).

---

## 2. Network — the only thing that must match before handover

The Wi-Fi SSID and password are **compiled into the firmware** (`firmware/include/secrets.h`,
git-ignored). The board will not connect to a different network until it is rebuilt for it.

- Wi-Fi must be **2.4 GHz** (the ESP32-S3 cannot see 5 GHz networks).
- Default broker is the **public HiveMQ** (`broker.hivemq.com:1883`), so the node needs internet.
- Topic root is `iot-smarthome/home` and must equal the dashboard's **Topic prefix**.

So one of these must be true before the box leaves the lab:

1. the receiver runs a 2.4 GHz hotspot with the **same SSID/password** that is compiled in, or
2. the firmware is **rebuilt** for their network (§5), or
3. the firmware is rebuilt for a **local Mosquitto** on their laptop (§7) — no internet needed.

---

## 3. Start-up check (about a minute)

1. Power on. The RGB LED is **off** until the radar sees someone.
2. Within ~10 s the dashboard shows **ESP32 online** and the Wi-Fi tile gets an RSSI.
3. Wave a hand in front of the radar (≤ 4 m): the LED turns **white**, the presence tile
   shows the distance in cm.
4. Temperature/humidity appear in ~3 s. Gas needs a **60 s warm-up**; the tile says so.
5. Gas readings are live after the warm-up (`gas_ready: true`). Clean air ≈ a few ppm.
6. A gas alarm turns the LED **red** and raises the safety banner.

If step 2 fails, see §6.

---

## 4. Dashboard

Windows: double-click **`start-dashboard.bat`** → opens `http://localhost:8000`.

macOS / Linux:

```bash
cd dashboard
python3 -m http.server 8000
# then open http://localhost:8000
```

In **Settings** make sure:

- **Preset**: `HiveMQ public (wss)` (or `Mosquitto local` in LAN mode)
- **Topic prefix**: `iot-smarthome/home` (must match `MQTT_BASE` in the firmware)
- **Room**: `livingroom`

The dashboard is a PWA: “Install app” in the address bar. History/charts are browser-only,
the device is the source of truth.

---

## 5. Rebuilding the firmware for a different Wi-Fi

```bash
pip install platformio                     # or the VS Code PlatformIO extension
cd firmware
cp include/secrets.example.h include/secrets.h
# edit secrets.h: WIFI_SSID, WIFI_PASS, and a unique MQTT_BASE
pio run -e esp32s3-usb -t upload           # cable in the port marked USB (native)
pio run -e esp32s3 -t upload               # cable in the port marked UART/COM
```

Then **press RST once** (mandatory on the native USB port, see §6).

Keep `MQTT_BASE` unique if using the public broker, otherwise other people's nodes appear in
the dashboard and yours appear in theirs.

---

## 6. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Compiles and flashes, but never gets Wi-Fi | wrong SSID/password, or a 5 GHz-only network | rebuild with the right 2.4 GHz credentials |
| PWR LED is on, but offline and no serial output | chip parked in the ROM **download mode** after flashing | press **RST** once (USB replug also works) |
| `mmwave_ok: false`, gas `0 mV` | 5 V rail to the sensors is off | turn the power board on, check `5V`/GND to radar and MQ-2 |
| Gas ppm high in clean air | `R0` calibrated on a cold/unpowered sensor | run 5–10 min in clean air, press **Calibrate** in Settings |
| Dashboard loads, no data | topic prefix mismatch, or the device is offline | match `Topic prefix` to `MQTT_BASE`, check `…/status` |
| Servo hums but does not move | powered from 3V3, or a weak supply | servo VCC on 5 V, GND shared with the board |
| LED dark while presence is true | LED wired as common-anode, or too dim | set `LED_COMMON_ANODE 1` in `config.h`, rebuild |
| Radar reports presence with nothing there | reflective surfaces / fans in the beam | remount, or lower the range in the Hi-Link app |

Notes that save time:

- `R0` (MQ-2 calibration) is stored in flash and survives reboots and re-flashes; it does **not**
  survive a full chip erase.
- On the native USB port the tool chain leaves the chip in download mode — a physical **RST**
  press after every upload is normal, not a fault.
- Editing `dashboard/index.html`? Bump `VERSION` in `dashboard/sw.js`, otherwise installed PWAs
  keep serving the cached copy.

---

## 7. Offline / classroom LAN mode (no internet)

1. Install Mosquitto, then from the repo root:
   ```bash
   mosquitto -c broker/mosquitto.conf -v
   ```
   It listens on `1883` (MQTT, for the ESP32) and `9001` (WebSockets, for the dashboard).
2. In `secrets.h` set `MQTT_HOST` to the laptop's Wi-Fi IPv4 (`ipconfig`), then rebuild and flash.
3. In the dashboard: **Preset → `Mosquitto local (ws://localhost:9001)`**, keep the same prefix.
4. Windows Firewall must allow inbound 1883/9001 on the private network.

---

## 8. Topics the assistant/dashboard uses

```
iot-smarthome/home/livingroom/sensors          telemetry every 3 s
iot-smarthome/home/livingroom/status           online / offline (retained, LWT)
iot-smarthome/home/livingroom/config           settings + pins (retained)
iot-smarthome/home/livingroom/config/set       {"gas_threshold":1000,"interval_ms":3000,"cmd":"calibrate"}
iot-smarthome/home/livingroom/led/set          {"r":0,"g":0,"b":0} | {"auto":true}
iot-smarthome/home/livingroom/led/state        retained: r,g,b,auto,presence,alarm
iot-smarthome/home/livingroom/servo/set        {"angle":0..180} | {"sweep":true}
iot-smarthome/home/livingroom/ir/send          {"key":"fan.power"}
iot-smarthome/home/safety/alert                gas alarm / clear (retained)
```
