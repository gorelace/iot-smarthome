// ESP32-S3 edge node: DHT22 + MQ-2 + VS1838B IR receiver -> MQTT
//
// Publishes (all under <MQTT_BASE>, default "home"):
//   home/livingroom/sensors        telemetry JSON every 3 s
//   home/livingroom/status         "online" / "offline" (retained, LWT)
//   home/livingroom/config         current settings (retained)
//   home/livingroom/ir/rx          every IR code the receiver decodes
//   home/livingroom/ir/learn/state learning progress / result
//   home/safety/alert              gas alarm / clear (retained, QoS 1)
// Subscribes:
//   home/livingroom/config/set     {"gas_threshold":1200,"interval_ms":3000} or {"cmd":"calibrate"|"reboot"|"clear_ir"}
//   home/livingroom/ir/learn       {"device":"fan","cmd":"power"} -> next IR code is saved to flash
//
// The gas safety rule runs locally, so it keeps working when Wi-Fi or the broker is down.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <Preferences.h>
#include <IRremote.hpp>
#include <lwip/dns.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy firmware/include/secrets.example.h to firmware/include/secrets.h and fill in your Wi-Fi + broker."
#endif

// ---------------------------------------------------------------- topics
static const String T_ROOM      = String(MQTT_BASE) + "/" + ROOM;
static const String T_SENSORS   = T_ROOM + "/sensors";
static const String T_STATUS    = T_ROOM + "/status";
static const String T_CONFIG    = T_ROOM + "/config";
static const String T_CONFIGSET = T_ROOM + "/config/set";
static const String T_IR_RX     = T_ROOM + "/ir/rx";
static const String T_IR_LEARN  = T_ROOM + "/ir/learn";
static const String T_IR_LSTATE = T_ROOM + "/ir/learn/state";
static const String T_ALERT     = String(MQTT_BASE) + "/safety/alert";

// ---------------------------------------------------------------- globals
#if MQTT_USE_TLS
WiFiClientSecure net;
#else
WiFiClient net;
#endif
MQTTClient mqtt(1024);
DHTesp dht;
Preferences prefs;
String clientId;

// settings (persisted in NVS)
float gasThreshold = DEFAULT_GAS_THRESHOLD_PPM;
uint32_t publishMs = DEFAULT_PUBLISH_MS;
float mq2R0 = 0;  // kOhm, 0 = not calibrated yet

// latest readings
float tempC = NAN, humidity = NAN, heatIndex = NAN;
const char *dhtStatus = "init";
float mq2Samples[MQ2_AVG_SAMPLES];
int mq2Idx = 0, mq2Count = 0;
float gasMv = 0, gasRs = 0, gasRatio = 0, gasPpm = 0;
bool gasReady = false, gasAlarm = false;

// IR learning
String learnDevice, learnCmd;
uint32_t learnUntil = 0;

// incoming MQTT messages are queued: the library forbids publishing inside its callback
struct Msg { String topic, payload; };
Msg inbox[4];
volatile int inboxCount = 0;

uint32_t lastPublish = 0, lastMq2 = 0, lastDht = 0, lastMqttTry = 0;

// ---------------------------------------------------------------- helpers
void led(uint8_t r, uint8_t g, uint8_t b) {
#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, r, g, b);
#endif
}

void updateLed() {
  if (gasAlarm) led(40, 0, 0);
  else if (!mqtt.connected()) led(0, 0, 20);
  else led(0, 8, 0);
}

bool publishJson(const String &topic, JsonDocument &doc, bool retained = false, int qos = 0) {
  if (!mqtt.connected()) return false;
  String out;
  serializeJson(doc, out);
  return mqtt.publish(topic, out, retained, qos);
}

void saveSettings() {
  prefs.begin("cfg", false);
  prefs.putFloat("gas_thr", gasThreshold);
  prefs.putUInt("pub_ms", publishMs);
  prefs.putFloat("r0", mq2R0);
  prefs.end();
}

void loadSettings() {
  prefs.begin("cfg", true);
  gasThreshold = prefs.getFloat("gas_thr", DEFAULT_GAS_THRESHOLD_PPM);
  publishMs = prefs.getUInt("pub_ms", DEFAULT_PUBLISH_MS);
  mq2R0 = prefs.getFloat("r0", 0);
  prefs.end();
}

void publishConfig() {
  JsonDocument d;
  d["gas_threshold"] = gasThreshold;
  d["interval_ms"] = publishMs;
  d["mq2_r0_kohm"] = mq2R0;
  d["pins"]["ir_rx"] = PIN_IR_RX;
  d["pins"]["mq2"] = PIN_MQ2_AO;
  d["pins"]["dht22"] = PIN_DHT22;
  d["fw"] = __DATE__ " " __TIME__;
  d["ip"] = WiFi.localIP().toString();
  publishJson(T_CONFIG, d, true, 1);
}

// ---------------------------------------------------------------- safety (edge rule)
void publishAlert() {
  JsonDocument d;
  d["type"] = "gas";
  d["level"] = gasAlarm ? "alarm" : "clear";
  d["gas_ppm"] = roundf(gasPpm);
  d["threshold"] = gasThreshold;
  d["room"] = ROOM;
  d["uptime_s"] = millis() / 1000;
  publishJson(T_ALERT, d, true, 1);
}

void setAlarm(bool on) {
  if (on == gasAlarm) return;
  gasAlarm = on;
  Serial.printf("[SAFETY] gas %s (%.0f ppm, threshold %.0f)\n", on ? "ALARM" : "clear", gasPpm, gasThreshold);
#if PIN_BUZZER >= 0
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#endif
  // relay_safe_off(), fan_on(), curtain_open() go here once those actuators are wired.
  publishAlert();
  updateLed();
}

// ---------------------------------------------------------------- MQ-2
void calibrateMq2() {
  if (gasRs <= 0) return;
  mq2R0 = gasRs / MQ2_CLEAN_AIR_RATIO;
  saveSettings();
  Serial.printf("[MQ2] calibrated R0 = %.2f kOhm (Rs %.2f)\n", mq2R0, gasRs);
  publishConfig();
}

void sampleMq2() {
  mq2Samples[mq2Idx] = analogReadMilliVolts(PIN_MQ2_AO);
  mq2Idx = (mq2Idx + 1) % MQ2_AVG_SAMPLES;
  if (mq2Count < MQ2_AVG_SAMPLES) mq2Count++;

  float sum = 0;
  for (int i = 0; i < mq2Count; i++) sum += mq2Samples[i];
  gasMv = sum / mq2Count;

  float vout = gasMv / 1000.0f * MQ2_DIVIDER;  // voltage at the module's AO pin
  if (vout < 0.01f) vout = 0.01f;
  if (vout > MQ2_VC - 0.01f) vout = MQ2_VC - 0.01f;
  gasRs = MQ2_RL_KOHM * (MQ2_VC - vout) / vout;

  bool warm = millis() > MQ2_WARMUP_MS;
  if (warm && mq2R0 <= 0) calibrateMq2();  // first boot: assume clean air after warm-up
  gasReady = warm && mq2R0 > 0;
  if (!gasReady) return;

  gasRatio = gasRs / mq2R0;
  gasPpm = powf(10.0f, (log10f(gasRatio) - MQ2_CURVE_B) / MQ2_CURVE_M);
  if (gasPpm > 100000) gasPpm = 100000;

  if (!gasAlarm && gasPpm > gasThreshold) setAlarm(true);
  else if (gasAlarm && gasPpm < gasThreshold * GAS_HYSTERESIS) setAlarm(false);
}

// ---------------------------------------------------------------- DHT22
void readDht() {
  TempAndHumidity v = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE && !isnan(v.temperature)) {
    tempC = v.temperature + TEMP_OFFSET_C;
    humidity = constrain(v.humidity + HUM_OFFSET_PCT, 0.0f, 100.0f);
    heatIndex = dht.computeHeatIndex(tempC, humidity, false);
    dhtStatus = "ok";
  } else {
    dhtStatus = dht.getStatusString();
    Serial.printf("[DHT] read failed: %s\n", dhtStatus);
  }
}

// ---------------------------------------------------------------- telemetry
void publishTelemetry() {
  JsonDocument d;
  if (!isnan(tempC)) {
    d["temp"] = roundf(tempC * 10) / 10;
    d["hum"] = roundf(humidity * 10) / 10;
    d["heat_index"] = roundf(heatIndex * 10) / 10;
  }
  d["dht_status"] = dhtStatus;
  d["gas_mv"] = roundf(gasMv);
  d["gas_ready"] = gasReady;
  if (gasReady) {
    d["gas_ppm"] = roundf(gasPpm);
    d["gas_ratio"] = roundf(gasRatio * 100) / 100;
  } else {
    d["warmup_s"] = millis() < MQ2_WARMUP_MS ? (MQ2_WARMUP_MS - millis()) / 1000 : 0;
  }
  d["gas_alarm"] = gasAlarm;
  d["gas_threshold"] = gasThreshold;
  d["rssi"] = WiFi.RSSI();
  d["uptime_s"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();

  Serial.printf("[TELE] T=%.1fC H=%.1f%% gas=%.0fmV %s%.0fppm rssi=%d\n", tempC, humidity, gasMv,
                gasReady ? "" : "(warming) ", gasPpm, WiFi.RSSI());
  publishJson(T_SENSORS, d);
}

// ---------------------------------------------------------------- IR receiver
String learnKey(const String &dev, const String &cmd) {
  // NVS keys are max 15 chars
  return dev.substring(0, 7) + "." + cmd.substring(0, 7);
}

void handleIr() {
  if (!IrReceiver.decode()) return;
  IRData &ir = IrReceiver.decodedIRData;
  bool repeat = ir.flags & IRDATA_FLAGS_IS_REPEAT;
  // Electrical noise decodes as UNKNOWN with almost no bits; real remote frames have many.
  bool noise = ir.protocol == UNKNOWN && ir.numberOfBits < 8;
  static uint32_t noiseCount = 0, lastNoiseLog = 0;
  if (noise) {
    noiseCount++;
    if (millis() - lastNoiseLog > 30000) {
      lastNoiseLog = millis();
      Serial.printf("[IR] ignored %lu noise frames (check VS1838B wiring/3V3 power)\n", (unsigned long)noiseCount);
      noiseCount = 0;
    }
  }

  if (!repeat && !noise) {
    char raw[20], addr[8], cmd[8];
    snprintf(raw, sizeof raw, "0x%llX", (unsigned long long)ir.decodedRawData);
    snprintf(addr, sizeof addr, "0x%X", ir.address);
    snprintf(cmd, sizeof cmd, "0x%X", ir.command);
    const char *proto = getProtocolString(ir.protocol);
    Serial.printf("[IR] %s addr=%s cmd=%s raw=%s bits=%d\n", proto, addr, cmd, raw, ir.numberOfBits);

    JsonDocument d;
    d["protocol"] = proto;
    d["address"] = addr;
    d["command"] = cmd;
    d["raw"] = raw;
    d["bits"] = ir.numberOfBits;
    publishJson(T_IR_RX, d);

    if (learnUntil && ir.protocol != UNKNOWN) {
      String key = learnKey(learnDevice, learnCmd);
      String val = String(proto) + "," + addr + "," + cmd + "," + raw + "," + ir.numberOfBits;
      prefs.begin("ir", false);
      prefs.putString(key.c_str(), val);
      prefs.end();
      Serial.printf("[IR] learned %s = %s\n", key.c_str(), val.c_str());

      JsonDocument s;
      s["state"] = "saved";
      s["device"] = learnDevice;
      s["cmd"] = learnCmd;
      s["key"] = key;
      s["code"] = d;
      publishJson(T_IR_LSTATE, s);
      learnUntil = 0;
    }
  }
  IrReceiver.resume();
}

void checkLearnTimeout() {
  if (learnUntil && (int32_t)(millis() - learnUntil) > 0) {
    learnUntil = 0;
    JsonDocument s;
    s["state"] = "timeout";
    s["device"] = learnDevice;
    s["cmd"] = learnCmd;
    publishJson(T_IR_LSTATE, s);
  }
}

// ---------------------------------------------------------------- MQTT
void onMqtt(String &topic, String &payload) {
  if (inboxCount < 4) inbox[inboxCount++] = {topic, payload};
}

void handleMessage(const Msg &m) {
  JsonDocument d;
  if (deserializeJson(d, m.payload)) {
    Serial.printf("[MQTT] bad JSON on %s\n", m.topic.c_str());
    return;
  }
  if (m.topic == T_CONFIGSET) {
    if (d["gas_threshold"].is<float>()) gasThreshold = constrain(d["gas_threshold"].as<float>(), 100.0f, 20000.0f);
    if (d["interval_ms"].is<uint32_t>()) publishMs = constrain(d["interval_ms"].as<uint32_t>(), 1000u, 60000u);
    const char *cmd = d["cmd"] | "";
    if (!strcmp(cmd, "calibrate")) calibrateMq2();
    else if (!strcmp(cmd, "clear_ir")) { prefs.begin("ir", false); prefs.clear(); prefs.end(); }
    else if (!strcmp(cmd, "reboot")) { mqtt.publish(T_STATUS, "offline", true, 1); delay(200); ESP.restart(); }
    saveSettings();
    publishConfig();
  } else if (m.topic == T_IR_LEARN) {
    learnDevice = d["device"] | "dev";
    learnCmd = d["cmd"] | "cmd";
    learnUntil = millis() + IR_LEARN_TIMEOUT_MS;
    JsonDocument s;
    s["state"] = "waiting";
    s["device"] = learnDevice;
    s["cmd"] = learnCmd;
    s["timeout_s"] = IR_LEARN_TIMEOUT_MS / 1000;
    publishJson(T_IR_LSTATE, s);
    Serial.printf("[IR] learning %s/%s: point the remote at the receiver\n", learnDevice.c_str(), learnCmd.c_str());
  }
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMqttTry < 5000 && lastMqttTry) return;
  lastMqttTry = millis();

  Serial.printf("[MQTT] connecting to %s:%d as %s ... ", MQTT_HOST, MQTT_PORT, clientId.c_str());
  bool ok = strlen(MQTT_USER) ? mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)
                              : mqtt.connect(clientId.c_str());
  if (!ok) {
    Serial.printf("failed (err %d, rc %d)\n", mqtt.lastError(), mqtt.returnCode());
    updateLed();
    return;
  }
  Serial.println("ok");
  mqtt.publish(T_STATUS, "online", true, 1);
  mqtt.subscribe(T_CONFIGSET, 1);
  mqtt.subscribe(T_IR_LEARN, 1);
  publishConfig();
  publishAlert();  // refresh the retained alert so it matches the real state
  updateLed();
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 smart-home edge node ===");

  loadSettings();
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ2_AO, ADC_11db);  // ~0-3.1 V range
  dht.setup(PIN_DHT22, DHTesp::DHT22);
  IrReceiver.begin(PIN_IR_RX, DISABLE_LED_FEEDBACK);
#if PIN_BUZZER >= 0
  pinMode(PIN_BUZZER, OUTPUT);
#endif
  led(0, 0, 20);

  uint64_t mac = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof id, "esp32s3-%06llx", (unsigned long long)(mac >> 24) & 0xFFFFFF);
  clientId = id;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] connecting to %s\n", WIFI_SSID);

#if MQTT_USE_TLS
  net.setInsecure();  // dev only: skips certificate check. Pin the CA for production.
#endif
  mqtt.begin(MQTT_HOST, MQTT_PORT, net);
  mqtt.onMessage(onMqtt);
  mqtt.setKeepAlive(30);
  mqtt.setTimeout(3000);
  mqtt.setWill(T_STATUS.c_str(), "offline", true, 1);

  Serial.printf("Topics: %s/* and %s\n", T_ROOM.c_str(), T_ALERT.c_str());
}

void loop() {
  static bool wifiWasUp = false;
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp != wifiWasUp) {
    wifiWasUp = wifiUp;
    if (wifiUp) {
      Serial.printf("[WiFi] connected, IP %s, gateway %s, DNS %s, RSSI %d\n", WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str(), WiFi.RSSI());
      // Some hotspots (e.g. Windows Mobile Hotspot) hand out a DNS server that doesn't answer.
      // Use public resolvers instead of the one from DHCP.
      ip_addr_t d1, d2;
      IP_ADDR4(&d1, 8, 8, 8, 8);
      IP_ADDR4(&d2, 1, 1, 1, 1);
      dns_setserver(0, &d1);
      dns_setserver(1, &d2);
    }
    else Serial.println("[WiFi] disconnected");
  }

  // Wi-Fi diagnostics: every 20 s while disconnected, say why and whether the SSID is visible
  static uint32_t lastWifiDiag = 0;
  if (!wifiUp && millis() - lastWifiDiag > 20000) {
    lastWifiDiag = millis();
    Serial.printf("[WiFi] not connected (status %d), scanning...\n", WiFi.status());
    WiFi.disconnect();  // a scan fails (-2) while a connect attempt is in progress
    delay(100);
    int n = WiFi.scanNetworks();
    bool found = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == WIFI_SSID) {
        found = true;
        Serial.printf("[WiFi] '%s' visible: ch %d, RSSI %d, %s\n", WIFI_SSID, WiFi.channel(i), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
      }
    }
    if (!found) {
      Serial.printf("[WiFi] '%s' NOT visible among %d networks (5 GHz only? wrong name?). Visible:\n", WIFI_SSID, n);
      for (int i = 0; i < n && i < 12; i++) Serial.printf("[WiFi]   '%s' ch %d RSSI %d\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));
    }
    WiFi.scanDelete();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  connectMqtt();
  mqtt.loop();
  while (inboxCount > 0) {
    Msg m = inbox[--inboxCount];
    handleMessage(m);
  }

  uint32_t now = millis();
  if (now - lastMq2 >= MQ2_SAMPLE_MS) { lastMq2 = now; sampleMq2(); }
  if (now - lastDht >= 2500 || !lastDht) { lastDht = now ? now : 1; readDht(); }
  if (now - lastPublish >= publishMs) { lastPublish = now; publishTelemetry(); }

  handleIr();
  checkLearnTimeout();
  delay(5);
}
