// Copy this file to secrets.h (same folder) and fill it in.
// secrets.h is git-ignored, so your Wi-Fi password never ends up on GitHub.
#pragma once

// ESP32 only supports 2.4 GHz Wi-Fi.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// --- Option A: public HiveMQ broker (zero setup, but anyone can read your topics) ---
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USE_TLS 0
#define MQTT_USER ""
#define MQTT_PASS ""

// --- Option B: Mosquitto on your laptop (see broker/README in the repo root) ---
// #define MQTT_HOST "192.168.1.23"   // laptop's IPv4 on the same Wi-Fi (ipconfig)
// #define MQTT_PORT 1883
// #define MQTT_USE_TLS 0
// #define MQTT_USER ""
// #define MQTT_PASS ""

// --- Option C: HiveMQ Cloud free cluster (private, TLS) ---
// #define MQTT_HOST "xxxxxxxx.s1.eu.hivemq.cloud"
// #define MQTT_PORT 8883
// #define MQTT_USE_TLS 1
// #define MQTT_USER "your-cluster-user"
// #define MQTT_PASS "your-cluster-password"

// Topic root. On the PUBLIC broker make this unique (e.g. "nhom5-iot/home"),
// otherwise you will see other people's "home/..." data and they will see yours.
// The dashboard's "Topic prefix" field must match.
#define MQTT_BASE "home"
