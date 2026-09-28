#!/usr/bin/env python3
"""Watch or poke the jarolift/ topics on a broker.

  mqtt_probe.py dump  [--host H] [--seconds N]         print retained + live jarolift/# messages
  mqtt_probe.py send  TOPIC PAYLOAD [--host H] [--seconds N]  publish, then print what follows
"""
import argparse
import time

import paho.mqtt.client as mqtt


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=["dump", "send"])
    p.add_argument("topic", nargs="?")
    p.add_argument("payload", nargs="?")
    p.add_argument("--host", default="192.0.2.10")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--seconds", type=float, default=3)
    p.add_argument("--filter", default="jarolift/#")
    a = p.parse_args()

    start = time.time()
    client_id = f"jarolift-probe-{int(start)}"
    if hasattr(mqtt, "CallbackAPIVersion"):  # paho-mqtt 2.x
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    else:
        c = mqtt.Client(client_id=client_id)

    def on_message(_c, _u, m):
        flag = "R" if m.retain else " "
        print(f"{time.time() - start:6.2f} {flag} {m.topic} = {m.payload.decode(errors='replace')}")

    c.on_message = on_message
    c.connect(a.host, a.port)
    c.subscribe(a.filter, qos=1)
    c.loop_start()
    time.sleep(1)
    if a.mode == "send":
        print(f"{time.time() - start:6.2f} > {a.topic} = {a.payload}")
        c.publish(a.topic, a.payload, qos=1, retain=False)
    time.sleep(a.seconds)
    c.loop_stop()
    c.disconnect()


if __name__ == "__main__":
    main()
