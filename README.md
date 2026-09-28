# jarolift-remote

ESPHome firmware for an Olimex ESP32-PoE with a CC1101 433 MHz module (shield from
`../ESP32POE_SomfyRTS`) that drives Jarolift TDEF shutters over RF and speaks the house MQTT topic
contract (`jarolift/…`) directly. It replaces the ESP8266 dongle from `../Jarolift_MQTT` without
re-teaching any motor. Planning and requirements live in
`openspec/changes/build-jarolift-remote-firmware/`.

| Path | Content |
|---|---|
| `jarolift-remote.yaml` | The ESPHome node: Ethernet, OTA, safe mode, the sixteen channel slots and the initial channel layout |
| `packages/channel.yaml` | One channel slot: time-based cover and two travel-time numbers |
| `components/jarolift/` | The external component: Keeloq, frames, counters, transmit queue, MQTT client, HTTPS web interface |
| `components/jarolift/core_*.{h,cpp}` | Device-independent code, unit-tested on the host |
| `components/jarolift/www/index.html` | The web interface (embedded gzip-compressed at build time) |
| `tests/` | Host unit tests (CMake + doctest) |
| `tools/mqtt_probe.py` | Watch or poke `jarolift/#` on a broker |
| `docs/` | Web interface and API, migration runbook |

## Prerequisites

- ESPHome as pinned in `requirements.txt` (`pip install -r requirements.txt`, or Homebrew's
  `esphome` at that version). Re-verify `set_open_duration` / `set_close_duration` and
  `has_built_in_endstop` of the `time_based` cover before bumping it.
- `sops` with the age key listed in `.sops.yaml` (the same key as `../heatingmonitor`).
- CMake and a C++17 compiler for the host tests.

## Secrets

The real values live SOPS-encrypted in `secrets.enc.yaml`; `secrets.yaml` is the decrypted copy
and is never committed. `secrets.example.yaml` lists every key.

```sh
sops -d secrets.enc.yaml > secrets.yaml      # before building
sops secrets.enc.yaml                        # edit in place
```

All values are build-time defaults. What is saved in the web interface (broker, network, channel
layout, Jarolift identity) overrides them and survives OTA updates and reflashes.

## Build and test

```sh
esphome config jarolift-remote.yaml          # validate
esphome compile jarolift-remote.yaml         # build

cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
```

## Flash

Normal updates go over the network (OTA, port 3232, password `ota_password`):

```sh
esphome upload jarolift-remote.yaml --device 192.0.2.30
```

A new image that crashes before it has booted successfully is rolled back to the previous one by
the bootloader, so a broken build does not lock the device out.

### USB recovery

Only when the network path is gone. Connect the board's USB port and flash without erasing:

```sh
esphome upload jarolift-remote.yaml --device /dev/cu.usbserial-XXXX
```

**Never run `esptool erase-flash`** (or `esphome run` with a factory image after an erase): the NVS
partition holds the rolling counters, and losing them means re-teaching every motor. A regular
USB upload writes only the application image and keeps NVS.

Do not connect USB while the board is powered over PoE unless the PoE side is isolated; the
ESP32-PoE (non-ISO) shares ground between both.

### Network reset

If a saved static network configuration made the device unreachable, build once with

```yaml
jarolift:
  network:
    reset: true
```

flash it (USB, or OTA if the device is still reachable somewhere), and the device forgets the
static configuration and uses DHCP again. Flash the normal build afterwards.

## Using it

- Web interface: `https://<device>/` (port 80 redirects), login `web_username` /
  `web_password`. See `docs/web-interface.md` for the pages and the API.
- MQTT: `jarolift/bridge/state`, `jarolift/<name>/state|position|open_duration|close_duration`,
  commands on `jarolift/<name>/set` (`OPEN`, `CLOSE`, `STOP`, `SHADE`, `SETSHADE`, `0`–`100`).
- Watch the device's topics with `tools/mqtt_probe.py`. It needs `paho-mqtt`, which ESPHome's own
  Python already has:

  ```sh
  "$(head -1 "$(which esphome)" | cut -c3-)" tools/mqtt_probe.py dump --host mqtt.example.com
  ```
