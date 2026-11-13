// Hardware + behaviour settings. Wi-Fi/broker settings live in secrets.h.
#pragma once

// ---------------- Pins (ESP32-S3) ----------------
#define PIN_IR_RX   5   // VS1838B OUT
#define PIN_IR_TX  15   // IR blaster: IR LED (via ~100R + NPN/MOSFET driver, or a ready-made KY-005 module) signal pin
#define PIN_MQ2_AO  6   // MQ-2 analog out (ADC1_CH5) -- see voltage warning below
#define PIN_DHT22   7   // DHT22 data (needs a 4.7k-10k pull-up; most modules have one)
#define PIN_I2C_SDA 9   // light sensor (BH1750) SDA
#define PIN_I2C_SCL 8   // light sensor (BH1750) SCL
#define PIN_MMWAVE_RX 18  // LD2410C TX -> ESP32 RX (UART1)
#define PIN_MMWAVE_TX 17  // ESP32 TX -> LD2410C RX (UART1; only needed to configure the module)
#define PIN_BUZZER -1   // not wired yet; set a GPIO to enable the local gas siren
#define PIN_SERVO   4   // servo signal (orange/yellow wire). Power the servo from 5V, NOT the 3V3 pin; share GND.
#define SERVO_MIN_US  500    // pulse width at 0 deg   (SG90: 500, MG90S: 500)
#define SERVO_MAX_US 2400    // pulse width at 180 deg (SG90: 2400)
#define SERVO_LEDC_CH 2      // LEDC channel used for the servo PWM

// Room name used in topics: <MQTT_BASE>/<ROOM>/sensors
#define ROOM "livingroom"

// ---------------- Timing ----------------
#define DEFAULT_PUBLISH_MS   3000UL   // telemetry period (slides: 2-5 s); changeable from dashboard
#define MQ2_SAMPLE_MS         100UL   // ADC sample period
#define MQ2_AVG_SAMPLES        20     // moving average window (20 x 100 ms = 2 s)
#define MQ2_WARMUP_MS       60000UL   // heater warm-up before readings are trusted
#define IR_LEARN_TIMEOUT_MS 15000UL
#define IR_DEBOUNCE_MS        300UL   // identical IR code within this gap of the previous frame = same button press

// ---------------- LD2410C mmWave radar ----------------
// The module reports continuously at 256000 baud (factory default) in normal mode.
#define MMWAVE_BAUD        256000UL
#define MMWAVE_TIMEOUT_MS    5000UL   // no frame for this long = sensor missing / wrong wiring

// ---------------- DHT22 calibration ----------------
// Added to every reading. Compare against a trusted thermometer placed next to the sensor
// for ~15 min, then set e.g. -1.2 if the DHT22 reads 1.2 C too high.
#define TEMP_OFFSET_C   0.0f
#define HUM_OFFSET_PCT  0.0f

// ---------------- MQ-2 ----------------
// WARNING: ESP32-S3 pins are 3.3 V max. If the MQ-2 module is powered from 5 V its AO pin
// can reach ~4-5 V with gas present. Put a divider between AO and GPIO6, e.g.
// AO --[10k]--+--[20k]-- GND, junction -> GPIO6, and set MQ2_DIVIDER to (10+20)/20 = 1.5.
// If you are sure AO never exceeds 3.3 V (module on 3.3 V), use 1.0.
#define MQ2_DIVIDER   1.5f
#define MQ2_VC        5.0f   // supply voltage of the MQ-2 module (5.0 or 3.3)
#define MQ2_RL_KOHM   1.0f   // load resistor on the module (most blue modules: 1k "102")
#define MQ2_CLEAN_AIR_RATIO 9.83f   // Rs/R0 in clean air (datasheet)
// LPG curve from the datasheet, log10(Rs/R0) = M*log10(ppm) + B, fitted through
// (200 ppm, 1.6) and (10000 ppm, 0.26). Readings are an *estimate* (+-30% or worse).
#define MQ2_CURVE_M  -0.464f
#define MQ2_CURVE_B   1.272f

#define DEFAULT_GAS_THRESHOLD_PPM 1000.0f  // alarm above this (LPG ~10% LEL is 2000 ppm)
#define GAS_HYSTERESIS 0.8f                // clear when below threshold * this
