#!/usr/bin/env python3
"""Read the crypto identity and counter out of the running ESP8266 Jarolift dongle.

Reads the configuration twice - over the dongle's web API (`POST /api cmd=get config`) and over
MQTT (`cmd/<devicetopic>/sendconfig` -> `stat/<devicetopic>/config/<n>`) - and compares serial
prefix, device counter and learn mode. On a mismatch it exits with status 2 and writes nothing,
because the migration must not continue on inconsistent input (docs/migration.md).

On success it writes the migration record (master key, serial prefix, learn mode, device counter,
channel names) as JSON with file mode 600, and prints a summary with the key masked.

    read_dongle.py --dongle 192.0.2.20 --out dongle-record.json [--broker mqtt.example.com]

Needs paho-mqtt (ESPHome's Python has it, see README.md).
"""
import argparse
import datetime
import json
import os
import sys
import time
import urllib.parse
import urllib.request

import paho.mqtt.client as mqtt


def api(dongle, cmd, timeout):
    data = urllib.parse.urlencode({"cmd": cmd}).encode()
    with urllib.request.urlopen(f"http://{dongle}/api", data=data, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def parse_config(text):
    """`key=value`, `checkbox=<name>=0|1` and `text=<name>=...` lines of the dongle's API."""
    values, checkboxes = {}, {}
    for line in text.splitlines():
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key == "checkbox" and "=" in value:
            name, state = value.split("=", 1)
            checkboxes[name] = state.strip() == "1"
        elif key != "text":
            values[key] = value.strip()
    return values, checkboxes


def hex_value(text, bits):
    number = int(text, 16)
    if not 0 <= number < (1 << bits):
        raise ValueError(f"{text} does not fit into {bits} bits")
    return number


def read_mqtt(broker, port, devicetopic, seconds):
    messages = []
    client_id = f"jarolift-read-dongle-{int(time.time())}"
    if hasattr(mqtt, "CallbackAPIVersion"):  # paho-mqtt 2.x
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    else:
        c = mqtt.Client(client_id=client_id)
    c.on_message = lambda _c, _u, m: messages.append(m.payload.decode("utf-8", "replace"))
    c.connect(broker, port)
    c.subscribe(f"stat/{devicetopic}/config/#", qos=1)
    c.loop_start()
    time.sleep(1)
    c.publish(f"cmd/{devicetopic}/sendconfig", "", qos=1)
    time.sleep(seconds)
    c.loop_stop()
    c.disconnect()
    merged = {}
    for payload in messages:
        try:
            merged.update(json.loads(payload))
        except json.JSONDecodeError:
            pass
    return merged


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--dongle", required=True, help="IP address or host name of the old dongle")
    p.add_argument("--broker", default="mqtt.example.com")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--out", required=True, help="migration record to write (JSON, mode 600)")
    p.add_argument("--seconds", type=float, default=5, help="how long to collect the MQTT answer")
    p.add_argument("--timeout", type=float, default=10)
    a = p.parse_args()

    values, checkboxes = parse_config(api(a.dongle, "get config", a.timeout))
    names_raw, _ = parse_config(api(a.dongle, "get channel name", a.timeout))
    devicetopic = values.get("mqtt_devicetopic") or "jarolift"

    problems = []
    try:
        record = {
            "read_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "dongle": a.dongle,
            "master_msb": f"0x{hex_value(values['master_msb'], 32):08x}",
            "master_lsb": f"0x{hex_value(values['master_lsb'], 32):08x}",
            "serial_prefix": f"0x{hex_value(values['serial'], 24):06x}",
            "learn_mode_new": checkboxes.get("learn_mode", True),
            "device_counter": int(values["devicecounter"]),
            "channel_names": {k.split("_", 1)[1]: v for k, v in names_raw.items() if k.startswith("channel_") and v},
        }
    except (KeyError, ValueError) as err:
        print(f"web API answer incomplete or malformed: {err}", file=sys.stderr)
        return 2

    mq = read_mqtt(a.broker, a.port, devicetopic, a.seconds)
    if not mq:
        problems.append(f"no answer on stat/{devicetopic}/config/# after cmd/{devicetopic}/sendconfig")
    else:
        try:
            if hex_value(mq["serialprefix"], 24) != int(record["serial_prefix"], 16):
                problems.append(f"serial prefix: web {record['serial_prefix']} vs MQTT {mq['serialprefix']}")
            if int(mq["devicecounter"]) != record["device_counter"]:
                problems.append(f"device counter: web {record['device_counter']} vs MQTT {mq['devicecounter']}")
            if bool(int(mq["new_learn_mode"])) != record["learn_mode_new"]:
                problems.append(f"learn mode: web {record['learn_mode_new']} vs MQTT {mq['new_learn_mode']}")
        except (KeyError, ValueError) as err:
            problems.append(f"MQTT answer incomplete or malformed: {err}")

    if problems:
        print("MISMATCH - do not continue the migration:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 2

    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(record, f, indent=2)
        f.write("\n")

    print(f"consistent; record written to {a.out}")
    print("  master key     set (not shown; see the record file)")
    print(f"  serial prefix  {record['serial_prefix']}")
    print(f"  learn mode     {'new' if record['learn_mode_new'] else 'old'}")
    print(f"  device counter {record['device_counter']}")
    print(f"  channels       {', '.join(f'{k}={v}' for k, v in record['channel_names'].items())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
