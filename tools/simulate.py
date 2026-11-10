"""Fake ESP32 node: publishes the same topics/payloads as the firmware.

Use it to test the dashboard without hardware:
    pip install paho-mqtt
    python tools/simulate.py --prefix home                      # public HiveMQ
    python tools/simulate.py --host localhost --prefix home     # local Mosquitto
    python tools/simulate.py --gas-spike                        # trigger a gas alarm after 20 s
"""
import argparse
import json
import math
import random
import time

import paho.mqtt.client as mqtt

p = argparse.ArgumentParser()
p.add_argument("--host", default="broker.hivemq.com")
p.add_argument("--port", type=int, default=1883)
p.add_argument("--prefix", default="home")
p.add_argument("--room", default="livingroom")
p.add_argument("--interval", type=float, default=3.0)
p.add_argument("--gas-spike", action="store_true", help="ramp gas above the threshold after 20 s")
a = p.parse_args()

room = f"{a.prefix}/{a.room}"
T = {k: f"{room}/{v}" for k, v in dict(sensors="sensors", status="status", config="config",
                                         config_set="config/set", ir_rx="ir/rx", learn="ir/learn",
                                         learn_state="ir/learn/state").items()}
T["alert"] = f"{a.prefix}/safety/alert"

threshold, interval_ms = 1000.0, int(a.interval * 1000)
alarm = False
learning = None
start = time.time()

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"sim-{random.randrange(1 << 24):06x}")
c.will_set(T["status"], "offline", qos=1, retain=True)


def pub_config():
    c.publish(T["config"], json.dumps({"gas_threshold": threshold, "interval_ms": interval_ms, "mq2_r0_kohm": 3.21,
                                       "pins": {"ir_rx": 5, "mq2": 6, "dht22": 7}, "fw": "simulator", "ip": "sim"}),
              qos=1, retain=True)


def pub_alert(ppm):
    c.publish(T["alert"], json.dumps({"type": "gas", "level": "alarm" if alarm else "clear", "gas_ppm": round(ppm),
                                      "threshold": threshold, "room": a.room}), qos=1, retain=True)


def on_connect(cl, *_):
    print(f"connected to {a.host}, publishing under {room}/*")
    cl.publish(T["status"], "online", qos=1, retain=True)
    cl.subscribe([(T["config_set"], 1), (T["learn"], 1)])
    pub_config()
    pub_alert(0)


def on_message(cl, _ud, msg):
    global threshold, interval_ms, learning
    d = json.loads(msg.payload or b"{}")
    print("<-", msg.topic, d)
    if msg.topic == T["config_set"]:
        threshold = float(d.get("gas_threshold", threshold))
        interval_ms = int(d.get("interval_ms", interval_ms))
        pub_config()
    elif msg.topic == T["learn"]:
        learning = (d.get("device", "dev"), d.get("cmd", "cmd"), time.time() + 3)
        cl.publish(T["learn_state"], json.dumps({"state": "waiting", "device": learning[0], "cmd": learning[1], "timeout_s": 15}))


c.on_connect, c.on_message = on_connect, on_message
c.connect(a.host, a.port, 30)
c.loop_start()

try:
    while True:
        el = time.time() - start
        temp = 29 + 1.5 * math.sin(el / 60) + random.uniform(-0.1, 0.1)
        hum = 68 + 5 * math.sin(el / 90) + random.uniform(-0.5, 0.5)
        ppm = 180 + random.uniform(-20, 20)
        if a.gas_spike and el > 20:
            ppm += min(2500, (el - 20) * 120) if el < 60 else max(0, 2500 - (el - 60) * 150)
        ready = el > 10
        if ready and not alarm and ppm > threshold:
            alarm = True; pub_alert(ppm)
        elif alarm and ppm < threshold * 0.8:
            alarm = False; pub_alert(ppm)
        d = {"temp": round(temp, 1), "hum": round(hum, 1), "heat_index": round(temp + 3.2, 1), "dht_status": "ok",
             "gas_mv": round(300 + ppm / 5), "gas_ready": ready, "gas_alarm": alarm, "gas_threshold": threshold,
             "rssi": random.randint(-62, -55), "uptime_s": int(el), "heap": 210000}
        if ready:
            d.update(gas_ppm=round(ppm), gas_ratio=round(9.83 * (180 / max(ppm, 1)) ** 0.46, 2))
        else:
            d["warmup_s"] = int(10 - el)
        c.publish(T["sensors"], json.dumps(d))
        if random.random() < 0.15:
            cmd = random.choice([0x45, 0x46, 0x47, 0x44])
            c.publish(T["ir_rx"], json.dumps({"protocol": "NEC", "address": "0x0", "command": hex(cmd).upper().replace("0X", "0x"),
                                              "raw": f"0x{(~cmd & 0xFF) << 24 | cmd << 16 | 0xFF00:X}", "bits": 32}))
        if learning and time.time() > learning[2]:
            code = {"protocol": "NEC", "address": "0x0", "command": "0x45", "raw": "0xBA45FF00", "bits": 32}
            c.publish(T["ir_rx"], json.dumps(code))
            c.publish(T["learn_state"], json.dumps({"state": "saved", "device": learning[0], "cmd": learning[1], "code": code}))
            learning = None
        print(f"-> T={d['temp']} H={d['hum']} ppm={d.get('gas_ppm', '-')} alarm={alarm}")
        time.sleep(interval_ms / 1000)
except KeyboardInterrupt:
    c.publish(T["status"], "offline", qos=1, retain=True).wait_for_publish(2)
    c.disconnect()
