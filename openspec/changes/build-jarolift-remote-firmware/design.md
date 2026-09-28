# Design

## Context

See proposal.md for motivation.

**Radio protocol.** Jarolift TDEF is unidirectional: motors accept Keeloq rolling-code telegrams and
never report state. Every position this device publishes is an estimate. One telegram takes about
117 ms (13-pulse preamble at 780 µs, 3.5 ms gap, 72 bits at 1200 µs, 16 ms inter-repetition delay);
UP/DOWN/STOP send two repetitions (~234 ms). Measured from `../Jarolift_MQTT/Jarolift_MQTT.ino`.

**Crypto identity.** `cmd_generate_serials()` derives every channel serial from one prefix: channel
*i* has serial `(prefix << 8) + i`. Together with the 64-bit master key and the learn-mode flag,
that is everything that must survive migration.

**Channels in this house** (`docs/topic-tree.md` §4b of the home server configuration). There are nine shutters and five
group channels, not sixteen shutters:

```
  ch  name                 ch  group        members
  --  -------------------  --  -----------  -------
   0  kueche                9  alle         0..8
   1  terrasse             10  eg           0..3
   2  wohnzimmer-links     11  og           4..8
   3  wohnzimmer-rechts    12  og-eltern    7, 8
   4  kind3-balkontuer     13  og-kinder    4, 5, 6
   5  kind3-fenster
   6  kind4-fenster
   7  eltern-links
   8  eltern-rechts
```

A group is a Jarolift feature: its own channel and serial, taught into each member motor, so one
frame moves all members at once. The nightly schedules drive channel 9.

**Hardware** is fixed by the `../ESP32POE_SomfyRTS` shield: Olimex ESP32-PoE, CC1101 (E07-433M20S)
on SCLK 14 / CSN 13 / MOSI 15 / MISO 16, radio TX on GPIO 4, RX on GPIO 36 (input-only), Ethernet
LAN8720 on clock 17 / power 12 / MDC 23 / MDIO 18.

**Consumers** (home server). Home Assistant is retired (D5). The broker is Mosquitto on the pinned
MetalLB address `192.0.2.10:1883`. Consumers are the HomeKit bridge (mqtt-homekit: `{{value}}`
templates, `factor`/`offset`, JSON `path`; no expression engine), `mqtt-rules`, `mqtt-logger`, and
`mqtt-alerting` (watches `+/bridge/state` for `offline`). Precedent for a device that is its own
gateway: an existing ESPHome node publishing `owserver/heating/*` directly.

## Goals / Non-Goals

**Goals:**

- No Jarolift component in the cluster: the device speaks the topic contract itself.
- Drive all nine shutters and five groups without re-teaching any motor.
- A retained, honest position estimate per shutter: bounded drift, exact at endpoints, updated by
  group commands and, where decodable, by hand transmitters.
- Hand-written C++ only for the radio layer.
- Travel-time calibration at runtime, not by recompile.
- The old dongle's web interface rebuilt, with network, broker and identity configurable at
  runtime and the broker given as a URL (`mqtt`, `mqtts`, `ws`, `wss`; IP or hostname).

**Non-Goals:**

- Home Assistant discovery or the ESPHome native API as the integration path.
- Percentage positioning of a group with a single frame (see Decision 4).
- Using the motor's taught shade position as a step of the position model. SHADE / SETSHADE remain
  available as explicit commands.
- WiFi and an access-point fallback.
- Running the old dongle alongside the new device.

## Decisions

### Decision 1: The device is the gateway — no cluster pod

`hs100-to-mqtt-gw` needs a pod because TP-Link plugs do not speak MQTT. The ESP32 does, so a
translator in the cluster would only add a hop, a deployment and a failure mode. The firmware
publishes the contract directly, like other ESPHome nodes and ESPSomfy RTS already do.

*Consequence.* Everything the `shutterState` rule does today — mirroring the echo retained, fanning
group channels out — moves into the firmware. The cluster keeps only configuration that points at
the new topics.

### Decision 2: ESPHome with a custom external component

ESPHome supplies Ethernet for the ESP32-PoE, OTA with safe mode, the log stream, the HTTP server
base, NVS-backed preferences and — most importantly — the `time_based` cover. MQTT and the web
interface move into the custom component, see Decision 9.
Custom code is the CC1101 setup, Keeloq frame construction and decoding, per-channel counters and
the transmit queue, estimated at 400–500 lines against 3000–5000 for standalone firmware.

*Alternatives considered.* Porting the sketch to Arduino-ESP32 or ESP-IDF. Rejected: it means
hand-writing the position state machine, which is the reason for the rebuild. With Home Assistant
retired, ESPHome's discovery is no longer an argument; the `time_based` cover and OTA alone carry
the decision.

### Decision 3: `time_based` cover per shutter

ESPHome's `time_based` cover estimates position from elapsed time: given `open_duration` and
`close_duration`, a move to 50 % from open calls `close_action`, runs a timer, publishes the
interpolated position, and calls `stop_action` when the target is reached. The radio component only
has to provide *send UP / DOWN / STOP on channel N*.

- `has_built_in_endstop: true` — moves to 0 % or 100 % send no STOP; the motor stops at its limit
  and the estimate is reset exactly.
- `manual_control: false` — with `true`, ESPHome first jumps the estimate to the opposite endpoint
  before an endpoint move (CLOSE at 60 % would show 100 % and then count down). Recalibration does
  not need it: `has_built_in_endstop` re-sends an endpoint command even when the estimate already
  says the endpoint is reached.
- Travel times per shutter and direction live in template `number`s (`restore_value: true`) and are
  pushed in through the public `set_open_duration` / `set_close_duration` setters, only while the
  cover is idle (`recompute_position_()` reads them mid-travel). An `on_boot:` step applies them.

The error budget from the earlier analysis holds: ~0.5 s systematic error on a ~20 s shutter
(~2.5 %).

### Decision 4: Groups are channel bitmasks; one telegram, member estimates follow

A Jarolift telegram carries a 16-bit channel mask in its discrimination bits. A group is therefore
not a channel of its own but the mask of its members' channels, sent with the serial of channel 0 —
exactly how dewenni's ESP32-Jarolift-Controller implements groups, which supports migrating from
the madmartin dongle this house runs. Groups need no teaching and use up no channels; up to eight
are configurable. A group OPEN / CLOSE / STOP / SHADE sends **one** telegram, then drives every
member cover's state machine *without* transmitting — the member's own frame is suppressed while a
group command is being applied. Every member then times its own travel with its own durations and
publishes its own position.

```
  jarolift/alle/set CLOSE
        |
        v
  group "alle" --1 DOWN telegram, serial ch 0, mask 0x01FF--> motors 0..8 move
        |
        +--> covers 0..8 start silently, each with its own close_duration
```

A group **percentage** cannot be done with one telegram: members travel at different speeds, so one
shared STOP would leave them at different positions. A percentage on a group topic is fanned out
as individual member commands in sequence (~234 ms each). A group STOP is safe: every member stops
and every estimate is correct because each cover measured its own time.

The old dongle used taught group channels (9–13 here: every member motor also learned that
channel's serial). Those serials stay in the motors after the takeover; a channel that is reused
for a single shutter must be UNLEARNed first, and the bitmask telegram must be confirmed on the
existing motors in the spike before the old group channels are given up.

*Alternatives considered.* Group channels as in the old dongle — every member must be taught into
every group, channels run out, and a membership list has to be kept in sync with what the motors
learned. Dropping groups and letting `mqtt-rules` drive members individually — nine telegrams
instead of one.

### Decision 5: Topic layout follows `hs100-to-mqtt-gw` under `jarolift/`

Name-keyed topics under the `jarolift/` root, conforming to `docs/topic-tree.md` (roots per source,
command and state separated, state retained, commands not):

```
  jarolift/bridge/state        -> online | offline                  retained, LWT (ESPHome birth/will)
  jarolift/<name>/state        -> open | closed | opening | closing retained
  jarolift/<name>/position     -> 0..100 (100 = open)               retained
  jarolift/<name>/set          <- OPEN | CLOSE | STOP | 0..100 | SHADE | ...
  jarolift/<name>/get          <- republish current state
  <name> = shutter or group name from the channel table above
```

Payloads are **scalars, Velux style** (`velux/<id>/state`, `velux/<id>/position`,
`velux/<id>/set` in `docs/topic-tree.md`), not hs100's single JSON object. The hs100 structure
contributes the name-keyed paths, `bridge/state`, `/set` and `/get`; the payload shape follows the
other covers in the house. The HomeKit bridge reads `position` directly without `path`, and ESPHome
publishes both values natively through the cover's per-entity `state_topic` and
`position_state_topic` — no parallel JSON publisher.

One `/set` topic accepts both keywords and numbers, as Velux does. ESPHome splits these onto
`command_topic` and `position_command_topic`; whether both may point at the same topic, or `/set`
needs an `mqtt: on_message` dispatcher, is settled during implementation — the contract does not
change either way.

Groups publish no `state` / `position` of their own; only their members' topics change. Their
`/set` accepts `OPEN` / `CLOSE` / `STOP` and fans a number out per Decision 4.

`jarolift/bridge/state` is what `mqtt-alerting`'s `bridge-offline` rule already matches, so
liveness alerting needs no change. There is no per-shutter `/available`: one-way RF cannot know
whether a motor is reachable.

### Decision 6: HomeKit position semantics

`100` = open, `0` = closed — HomeKit's convention and ESPHome's cover model. The HomeKit bridge
loses its `factor: -1, offset: 100` mapping. Breaking for anything reading the old
percent-closed values; accepted as a one-time migration cost.

### Decision 7: One shared rolling counter

Like the old dongle and dewenni's controller, the device keeps **one** 16-bit counter for all
channels and groups. The motors identify the sender by its serial base (a group telegram with the
serial of channel 0 is accepted by motors taught to other channels), so they expect a single rising
sequence from this device; per-channel counters would let a rarely used channel fall behind and be
rejected as a replay. The counter is persisted before each telegram. Counters stored per channel by
earlier builds of this firmware are merged by taking the highest.

### Decision 8: Ethernet only, no access-point fallback

ESPHome forbids `wifi:` and `ethernet:` together. On a PoE device, loss of network equals loss of
power, so a fallback AP could not start when it would be needed. Recovery: static default address
from the build, OTA over Ethernet, ESPHome safe mode, USB serial flash.

The web interface can switch between DHCP and a static address with DNS server. ESPHome's
`manual_ip` is compile-time, so the build uses DHCP-capable Ethernet and the custom component
applies the saved static configuration to the network interface at boot, before anything else
connects. A setting that makes the device unreachable is fixed by USB flash of a build with a
"reset network settings" flag.

### Decision 9: Runtime settings, own MQTT client, own web interface

ESPHome's `mqtt:` component takes broker, port and TLS at compile time and does not speak MQTT over
WebSocket. The broker URL from the web interface (`mqtt`, `mqtts`, `ws`, `wss`, IP or hostname,
port, path, CA certificate) therefore needs a client that is configured at runtime. The custom
component wraps ESP-IDF's `esp-mqtt` directly, which accepts exactly this URI form and supports all
four transports, TLS with a custom CA or the certificate bundle, and hostname verification. It owns
birth/will, the topic contract from Decision 5 and republishing after reconnect. The covers
publish through callbacks from the component instead of per-entity `state_topic`s.

The web interface is a single page (Home, Shutter, System, Log) compiled into the firmware, plus a
small JSON API, behind HTTP basic auth, on ESP-IDF's HTTPS server (Decision 11); ESPHome's
`web_server_base` cannot do TLS. Settings live in one
versioned NVS record; build-time `secrets.yaml` values are defaults only.

URL parsing is strict and happens before saving: scheme whitelist, IPv4 or RFC 1123 hostname,
port 1..65535, path only for WebSocket, empty = disabled. The parser is plain C++ without device
dependencies so it is unit-tested on the host.

*Alternatives considered.* Keeping ESPHome's `mqtt:` and calling its setters at runtime — covers
host and port, but not WebSocket and not a runtime CA. Keeping ESPHome's `web_server` — cannot
show the configuration pages. Both rejected.

*Consequence.* The ESPHome share shrinks to Ethernet, OTA, logging, preferences and the
`time_based` cover; the custom part grows by the MQTT client wrapper and the web interface.

### Decision 10: Sixteen shutter slots, runtime layout, settings in dewenni's style

The old dongle had sixteen channels with editable names, and new motors must be added without a
reflash. The firmware compiles sixteen slots, one `time_based` cover and two duration numbers per
channel, and keeps the layout in NVS: per channel "active", topic name and display label; per group
(up to eight) "active", topic name, label and member mask. The YAML's channel table is only the
initial layout.

- An inactive slot keeps its cover idle and invisible; only active shutters publish state.
- The topic name is the MQTT level. On rename or deactivation the device publishes empty retained
  messages to the old `state`, `position`, `open_duration` and `close_duration` topics.
- The web interface follows dewenni's controller: cards with ▲ ■ ▼ and shade for shutters and
  groups; a settings list of sixteen rows (label, topic, travel times, LEARN, UNLEARN, SETSHADE,
  active switch) saved field by field; groups with a member selection dialog. Channels are shown
  as 1–16.

*Alternative considered.* A layout table with a single save — rejected in review as unintuitive
compared with the old dongle and dewenni.

### Decision 11: HTTPS on the device with a pushed Let's Encrypt certificate

The page and its API (with basic auth) are served by ESP-IDF's `esp_https_server` on 443; a plain
`esp_http_server` on 80 only redirects. The certificate chain and key live in NVS. Without a stored
pair the device generates a self-signed ECDSA P-256 certificate on first start, so HTTPS works from
the beginning.

The trusted certificate comes from the cluster: the home server gets a cert-manager `Certificate` for
`jarolift.example.com` only (not the wildcard key, which must not sit on an IoT device), issued by
the existing netcup DNS-01 issuer, and a small CronJob (every 6 h) that compares the device's
`GET /api/tls` (expiry, self-signed) with the issued certificate and posts chain and key to
`POST /api/tls` when they differ. The device validates the pair with mbedTLS (parses,
key matches the certificate) before storing it and restarts only its HTTPS listener.
`jarolift.example.com` must resolve to the device inside the LAN, because the public wildcard
record points elsewhere.

*Trade-offs.* One TLS session costs about 40 kB of heap, so the HTTPS server allows at most three
concurrent connections. The CronJob is a generic "push certificate to device" job, not a Jarolift
translator; Decision 1 still holds.

### Decision 12: Takeover from the running dongle with a transmit lock

The device reads the dongle's identity over its unauthenticated web API (the same
`POST /api cmd=get config` its own page uses), twice, and adopts it on confirmation. From then on
it does not transmit until the operator finishes the takeover, and finishing is refused while the
dongle still answers. A background task polls the dongle every 15 s and keeps the device's counter
at the dongle's counter plus a margin (default 8), so the dongle may keep running during the
changeover without the device falling behind. This replaces the manual "read, seed, power off"
sequence as the normal path; `tools/read_dongle.py` remains for a record outside the device.

## Risks / Trade-offs

- [Rolling counter loss makes every motor ignore the device; re-teaching is physical] → seed above
  the old dongle's counter with a margin, persist in NVS, never `erase-flash`, record counter values
  at commissioning.
- [No motor accepts a frame built by the new component] → the assumption everything rests on; a
  spike sends one frame on one channel before anything else is built. Failure reopens Decision 2.
- [A `time_based` cover cannot be started without triggering its action] → Decision 4 depends on
  suppressing the member frame from inside the radio component; verify in the same early spike.
- [Group and member commands overlap] → one radio; the component owns a queue in which STOP wins
  over queued drive commands, so no shutter overshoots.
- [Hand transmitters are not reliably decodable] → RX then only marks an estimate stale instead of
  correcting it.
- [Drift on runs that never touch an endpoint] → the nightly `alle` CLOSE is an endpoint trip for
  all nine shutters, which bounds drift to one day.
- [ESPHome upgrades change `time_based` internals] → pin the ESPHome version; re-verify the two
  setters and `has_built_in_endstop` after upgrades.
- [Master key exposure] → `secrets.yaml`, SOPS-encrypted, never an entity;
  write-only in the web interface.
- [A wrong network or broker setting locks the operator out] → the web interface stays on the
  Ethernet address regardless of the broker; static-address mistakes are fixed by USB flash with a
  network reset flag.
- [TLS needs heap] → HTTPS server limited to three connections; MQTT uses one TLS session at most.
- [Certificate push fails or the device misses renewals] → the System page shows expiry; the
  self-signed fallback keeps the page reachable, only with a browser warning.
- [Operator lowers a counter by mistake] → confirmation dialog; lowering is logged.
- [Cluster consumers switch at a different time than the device] → cut over HomeKit values,
  `mqtt-rules` and `mqtt-logger` in the same rollout as the device.

## Migration Plan

1. **Read out the identity** from the running dongle: `POST /api` with `cmd=get config` yields
   master key, serial prefix, learn mode, device counter and channel names; cross-check via
   `cmd/jarolift/sendconfig`.
2. **Record** key, prefix and learn mode in `secrets.yaml`; keep an offline copy of the counter.
3. **Seed** the shared counter at the old counter plus a safety margin (done by the takeover, which
   keeps tracking the dongle's counter until it is switched off).
4. **Power off the old dongle permanently.**
5. **Verify one channel**, then one group.
6. **Switch the consumers** on the home server: HomeKit values, `mqtt-rules` (schedules to the group
   topic, retire `shutterState`), `mqtt-logger` allowlist, `docs/topic-tree.md` §4b.
7. **Calibrate travel times** per shutter and direction at runtime.

**Rollback.** Until step 4: power the old dongle back on. After step 4 the new device has advanced
the counters; the old dongle's counter must be set above the new one via its System page, and the
cluster consumers reverted.

## Open Questions

- Does a hand transmitter that was never taught to the gateway decode reliably enough to correct
  position, or only to invalidate it?
- Is a per-shutter soft-start offset needed on top of the two durations? Answered by calibration.
- Does `on_value` fire when a `number` restores from NVS at boot? The explicit `on_boot:` makes the
  answer irrelevant.
