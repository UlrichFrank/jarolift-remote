# Proposal

## Why

The Jarolift shutters are driven by a 2017-era ESP8266 dongle (`../Jarolift_MQTT`) that only knows
open / shade / closed, publishes its command echo un-retained on Tasmota-style topics
(`cmd/…`, `stat/…`, `tele/…`), and needs a rule in the cluster (`shutterState` in
`UlrichFrank/mqtt-rules`) to turn that echo into a usable position — including fanning the Jarolift
group channels out onto their members. The homeserver (`../home-new`) is moving to k0s with a
normative topic contract (`docs/topic-tree.md`); the dongle is the last device that does not fit it.

Rebuilding the firmware on the Olimex ESP32-PoE lets the device itself be the gateway: it speaks the
topic contract directly, owns the position estimate and the group semantics, and needs no pod in the
cluster.

## What Changes

- **BREAKING**: New hardware — Olimex ESP32-PoE + CC1101 (E07-433M20S) on the
  `../ESP32POE_SomfyRTS` shield, wired Ethernet + PoE. The ESP8266 NodeMCU, its WiFi setup and the
  double-reset access-point admin mode are not carried over.
- **BREAKING**: MQTT topic layout follows the `hs100-to-mqtt-gw` convention under the `jarolift/`
  root: `jarolift/bridge/state` (LWT), name-keyed `jarolift/<name>/…` for state and commands. The
  `cmd/jarolift/shutter/<N>`, `stat/jarolift/…` and `tele/jarolift/LWT` topics disappear.
- **BREAKING**: Position semantics follow HomeKit (`100` = open, `0` = closed), inverting the old
  dongle's percent-closed convention.
- The dongle's web interface (Home, Shutter, System, Log) is rebuilt. Network, broker, Jarolift
  identity and counters are configurable at runtime. The broker is entered as a URL with protocol
  (`mqtt`, `mqtts`, `ws`, `wss`), an IP address or hostname/domain, and an optional port and path,
  instead of an IPv4 address only.
- Firmware is an ESPHome node plus a custom external component. Hand-written C++ is limited to the
  radio layer (CC1101, Keeloq, rolling counters, transmit queue); the time-based position logic,
  MQTT, OTA and logging come from ESPHome; the web interface is served by the custom component.
- Shutters gain a retained, time-based position estimate that recalibrates on every endpoint trip.
  Travel times are runtime-settable and persisted.
- Jarolift **group channels** become first-class: one RF frame per group command, and the firmware
  advances the position estimate of every member shutter.
- Received telegrams from the original hand transmitters are decoded and used to correct (or at
  least invalidate) the position estimate.
- The rolling code counter becomes per channel instead of one shared counter.
- The crypto identity (master key, serial prefix, learn mode) and counter are migrated from the
  running dongle so no motor has to be re-taught.
- No Home Assistant discovery: Home Assistant is retired in `home-new` (design D5); the consumers are
  the HomeKit bridge and `mqtt-rules`.

## Capabilities

### New Capabilities

- `jarolift-radio`: CC1101 control, Keeloq frame construction and decoding, per-channel rolling
  counter persistence, a transmit queue with STOP priority, and the actions the covers call.
- `shutter-position`: Time-based position estimation per shutter, endpoint recalibration, runtime
  travel times, group channels fanning out onto member estimates, and correction from received
  hand-transmitter telegrams.
- `mqtt-interface`: The topic and payload contract on the broker — bridge liveness, per-shutter and
  per-group state and command topics, retain and QoS rules — conforming to `home-new`'s
  `docs/topic-tree.md`.
- `device-runtime`: The ESPHome node itself — Ethernet, broker connection, settings persistence,
  OTA and safe mode, logging, and recovery paths.
- `web-interface`: The rebuilt web interface over HTTPS — shutter and group control, sixteen
  configurable channels with teaching of new shutters and groups, travel times, network settings,
  the broker as a URL with protocol and hostname, TLS trust, the HTTPS certificate, Jarolift
  identity and counters, restart, and the live log.
- `dongle-migration`: Extracting the crypto identity and counter from the running ESP8266 dongle
  and seeding the new device so existing motors keep responding.

### Modified Capabilities

None. This repository has no existing specs.

## Impact

- **Repository**: greenfield; this change is the first content besides `openspec/`. Supersedes the
  earlier draft `../jarolift-to-mqtt-gw/openspec/changes/rebuild-gateway-on-esphome`, whose
  analysis (telegram timing, serial derivation, counter risk, ESPHome constraints) is carried over.
- **Hardware**: Olimex ESP32-PoE, CC1101 shield from `../ESP32POE_SomfyRTS`, 433 MHz antenna, PoE
  port. The device's USB port must stay physically reachable.
- **Dependencies**: ESPHome (version pinned), a Keeloq implementation.
- **`../home-new` (configuration only, no new service)**:
  - `charts/mqtt/homekit/values/values.yaml` — the nine window coverings point at the new topics;
    the `factor: -1, offset: 100` inversion goes away.
  - `UlrichFrank/mqtt-rules` — schedules move from `cmd/jarolift/shutter/9` to the group topic; the
    `shutterState` rule (v1.5.3) and its group fan-out are retired; the `cover.Cover` abstraction
    gets the new topics.
  - `charts/observability/mqtt-logger` — allowlist `jarolift/#` replaces the two legacy wildcards.
  - `docs/topic-tree.md` §4b and `docs/device-inventory.md` are rewritten.
  - `mqtt-alerting` needs no change: its `bridge-offline` rule already matches `+/bridge/state`.
- **Decommissioned**: the ESP8266 dongle. It must run until its identity is read out and must never
  be powered on again on the same serials.
- **Licensing**: the original project is GPLv3 and the Keeloq algorithm is licensed only to TI
  microcontrollers. This build stays private/educational, as the original does.
