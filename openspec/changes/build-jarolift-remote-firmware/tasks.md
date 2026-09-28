# Tasks

## 1. Project scaffolding

- [x] 1.1 Create the ESPHome node `jarolift-remote.yaml` for the Olimex ESP32-PoE (ESP-IDF framework, LAN8720 pins from the design, logger, OTA with password, safe mode, preferences) with a pinned ESPHome version in `requirements.txt`, and verify `esphome config jarolift-remote.yaml` succeeds
- [x] 1.2 Add `secrets.example.yaml` with every secret key (master MSB/LSB, serial prefix, learn mode, OTA password, web credentials, broker URL/user/password, default static network), a `.sops.yaml`, and a `.gitignore` that excludes the plain `secrets.yaml`; verify `esphome config` resolves all `!secret` references against the example
- [x] 1.3 Create the external component skeleton `components/jarolift/` (`__init__.py`, header, source) that ESPHome loads, and verify `esphome compile jarolift-remote.yaml` builds
- [x] 1.4 Set up host unit tests (`tests/` with CMake and doctest) that compile the device-independent C++ sources, and verify `cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests` runs one passing placeholder test
- [x] 1.5 Write `README.md` with build, flash (without `erase-flash`), OTA and host-test commands, and verify each documented command runs as written

## 2. Keeloq frames and counters (host-tested)

- [x] 2.1 Implement Keeloq encrypt/decrypt as plain C++ and verify host tests against the test vectors of `../Jarolift_MQTT/KeeloqLib`
- [x] 2.2 Implement channel serial derivation `(prefix << 8) + channel` and the 72-bit TDEF frame builder (serial, command code, counter, discrimination bits, learn-mode handling) ported from `Jarolift_MQTT.ino`, and verify host tests reproduce frames computed by the original sketch's algorithm for the same key, serial and counter
- [x] 2.3 Implement the frame decoder for received telegrams (decrypt, check discrimination, return serial, command, counter), and verify host tests round-trip every command through builder and decoder and reject corrupted frames
- [x] 2.4 Implement the per-channel counter store (14 channels, strictly increasing, persisted before send, explicit set with lower-value flag) behind a storage interface, and verify host tests with a fake store cover increment, independence between channels, restore after "reboot" and refusal to lower without the flag
- [x] 2.5 Implement the transmit queue (FIFO, STOP for a channel removes queued UP/DOWN/SHADE for that channel and jumps ahead) and the command set UP, DOWN, STOP, SHADE, SETSHADE, LEARN (new and old learn mode), and verify host tests for STOP priority, ordering across channels, and the LEARN sequences
- [x] 2.6 Replace the per-channel counters by one shared rolling counter (Decision 7), merging counters stored per channel by earlier builds into the highest; verify host tests for merge, strict increase across channels, persistence and lowering
- [x] 2.7 Build group telegrams by channel bitmask with the serial of channel 0, and verify host tests reproduce dewenni's `JaroliftController::cmdGroup()` bit for bit
- [x] 2.8 Add UNLEARN (UP+DOWN, 6× STOP, UP as in dewenni's `cmdUnlearn()`) and group targets in the transmit queue (STOP cancels the same target's drive only), and verify host tests
- [x] 2.9 Parse the old dongle's `get config` / `get channel name` answers (WiFi credentials ignored), and verify host tests with the dongle's answer format, CRLF and malformed input

## 3. Radio on hardware (spike)

- [ ] 3.1 Implement CC1101 setup over SPI (SCLK 14, CSN 13, MOSI 15, MISO 16) and the bit-banged transmit on GPIO 4 with the timings from the design (preamble 13 × 780 µs, 3.5 ms gap, 72 bits × 1200 µs, 16 ms between repetitions), and verify with an SDR or logic analyser that one telegram lasts about 117 ms
- [ ] 3.2 Wire counter store to ESPHome preferences (NVS) and log channel, command and counter (never the key) for every frame; verify on the device that counters survive a power cycle
- [ ] 3.3 Spike on a test channel before migration: send DOWN on a channel taught to a test motor (or on a scratch serial taught to one motor), and verify the motor moves; record the result in `docs/spike.md`; also send a group telegram with the mask of two channels taught to the old dongle and verify both motors move (Decision 4), before the old group channels are given up
- [ ] 3.4 Implement RX on GPIO 36: capture pulses while not transmitting, decode telegrams, ignore own frames, map configured hand-transmitter serials to shutters/groups, and verify on the device that pressing a configured hand transmitter logs the decoded command
- [ ] 3.5 Implement member-frame suppression during group application and verify on the device that a group CLOSE sends exactly one frame and leaves member counters unchanged

## 4. Shutter positions and groups

- [ ] 4.1 Add sixteen `time_based` cover slots (one per channel; shutter slots move, group and unused slots stay idle) (`has_built_in_endstop: true`, `manual_control: true`, restore state) whose open/close/stop actions call the radio component, and verify on the device that a move to 50 % sends drive + STOP and a move to 0 sends only DOWN
- [ ] 4.2 Add per-shutter open/close duration `number`s (`restore_value: true`), apply them through `set_open_duration`/`set_close_duration` on boot and when the cover is idle (deferred while moving), and verify after reboot that a changed duration is used for the next move
- [ ] 4.3 Implement the five groups from the design's channel table as channel bitmasks: OPEN/CLOSE/STOP/SHADE as one mask telegram with silent member actuation, percentages fanned out as member commands, and verify on the device that `alle` CLOSE brings all nine estimates to 0 and `og-eltern` 50 moves 7 and 8 individually (logic verified without radio; telegram on air pending the CC1101)
- [ ] 4.4 Implement SHADE/SETSHADE (estimate unchanged) and RX correction (received UP/DOWN/STOP drive the member covers silently), and verify on the device that a hand-transmitter UP makes the estimate rise without a frame being sent
- [ ] 4.5 Verify that a reboot restores every estimate and sends no frame (power-cycle test with logging)

## 5. Settings store and broker URL (host-tested)

- [x] 5.1 Implement the broker URL parser (schemes `mqtt`/`mqtts`/`ws`/`wss` case-insensitive, default `mqtt`, IPv4 or RFC 1123 hostname, port 1..65535 with scheme defaults 1883/8883/80/443, path only for `ws`/`wss` defaulting to `/mqtt`, empty = disabled, error message naming the problem), and verify host tests cover every scenario of the "System page — broker as URL" requirement
- [x] 5.2 Implement validators for IPv4 address, contiguous netmask, 32-bit and 24-bit hex values and PEM certificates, and verify host tests including `255.0.255.0` and malformed hex
- [x] 5.3 Implement the versioned settings record (network, broker URL/user/password/client ID/CA, identity, learn mode, labels) with build-time defaults and NVS persistence, and verify host tests that unsaved fields fall back to defaults and saved fields survive a reload

## 6. MQTT client and topic contract

- [ ] 6.1 Wrap ESP-IDF `esp-mqtt`, configured at runtime from the parsed URL (transport, host, port, path, credentials, client ID from MAC by default, CA or certificate bundle, hostname check), with reconnect back-off and DNS resolution per attempt, and verify on the device against Mosquitto with `mqtt://IP`, `mqtt://hostname` and `mqtts://hostname` with a private CA
- [x] 6.2 Implement birth/will on `jarolift/bridge/state` (retained, QoS 1), and verify with `mosquitto_sub -v -t 'jarolift/#'` that power loss yields `offline` and reconnect yields `online`
- [x] 6.3 Publish `state`, `position`, `open_duration`, `close_duration` per shutter (retained, QoS 1, scalars) on change, during travel and after reconnect, and verify with `mosquitto_sub` during a move
- [x] 6.4 Subscribe to `/set` and `/get` for shutters and groups, `/open_duration/set` and `/close_duration/set` per shutter, parse payloads per the mqtt-interface spec (case-insensitive keywords, integer 0..100, invalid payloads logged and ignored), and verify with `mosquitto_pub` for every command including `half` and `150`
- [x] 6.5 Apply broker setting changes by disconnecting and reconnecting without restart, and verify on the device that switching from IP to hostname URL moves `online` and all retained state to the new broker
- [x] 6.6 Verify with `mosquitto_sub -v -t '#'` over boot and operation that every device message is under `jarolift/` and no discovery or legacy topic appears
- [ ] 6.7 Test WebSocket transports `ws://` and `wss://…/path` against a Mosquitto listener with `protocol websockets`, and document the tested broker listener config in `docs/broker.md`

## 7. Web interface

- [x] 7.1 Build the static pages Home, Shutter, System, Log (HTML/CSS/JS, no external assets) embedded into the firmware, served from `web_server_base` behind HTTP basic auth, and verify in a browser that all four pages load with credentials and are refused without
- [x] 7.2 Implement the JSON API: status (firmware, IP, link, broker URL, connection state, last error), shutter/group list with positions and states, commands, durations, labels, and polling endpoints for positions (/api/status) and log lines (/log?after=N); verify with `curl -u` that every endpoint works and none returns the master key or a password
- [x] 7.3 Implement the Shutter page: controls for OPEN/STOP/CLOSE/position/SHADE/SETSHADE per shutter, group controls, editable durations and labels, LEARN with a confirmation naming the channel, live position updates; verify in a browser against the "Shutter page", "Teaching a motor" and "Channel labels" scenarios
- [ ] 7.4 Implement the System page: network settings (DHCP/static, IP, mask, gateway, DNS), broker URL with inline validation messages from the parser, username, password (write-only), client ID, CA certificate, Jarolift identity (write-only key, learn mode, serial prefix with regenerate confirmation), per-channel counters with lower-value confirmation, restart; verify in a browser against every System page scenario
- [x] 7.5 Implement the Home and Log pages (status incl. last broker error; live log without secrets), and verify in a browser that a DNS failure shows on Home and a command shows on Log within a few seconds
- [x] 7.6 Document the web interface and its API in `docs/web-interface.md`, and verify each documented `curl` call works as written
- [x] 7.7 Extend to sixteen channels (serials, sixteen cover slots in YAML) and verify on the device that channels 14 and 15 accept commands once configured
- [x] 7.8 Implement the runtime layout: sixteen shutter channels (active, topic, label) and up to eight groups (active, topic, label, member mask); validation across both; NVS persistence with the YAML table as initial layout; clearing retained topics on rename and deactivation; host tests for the validation and topic-from-label; verify on the device with `tools/mqtt_probe.py` against every scenario of "Shutter settings" and "Group settings"
- [x] 7.9 Rebuild the web interface in dewenni's style: Shutters and Groups pages with cards (▲ ■ ▼ shade, target position), Settings with sixteen shutter rows saved field by field (label, topic, travel times, LEARN, UNLEARN, SETSHADE with confirmation, active switch, learn instructions) and group rows with the member selection dialog and mask, System and Log pages; verify in a browser that a shutter on channel 15 and a group can be added, renamed and deactivated
- [x] 7.10 Replace `web_server_base` by ESP-IDF `esp_https_server` on 443 (basic auth, max three connections) and a redirect-only HTTP server on 80, with the certificate pair in NVS and a self-signed ECDSA P-256 certificate generated when none is stored; verify with `curl -v` that 80 redirects, 443 presents the self-signed certificate and all pages and API calls still work
- [x] 7.11 Implement `GET/POST /api/tls` (chain + key in PEM, mbedTLS validation, key must match, applied without restart, key never returned) and the certificate info on the System page; verify with a locally generated certificate and a mismatched key that valid pairs are served on new connections and invalid ones are rejected

## 8. Device runtime

- [x] 8.1 Apply the saved network settings (DHCP or static IP, mask, gateway, DNS) to the Ethernet interface at boot, with DHCP when nothing is saved and a build flag that resets network settings, and verify on the device that switching to static and back to DHCP works after restart
- [x] 8.2 Verify that an OTA update preserves counters, settings, durations and estimates (compare values before and after)
- [x] 8.3 Verify recovery from a faulty update: an OTA build that aborts at the end of every boot is rolled back by the bootloader to the previous firmware after the first crash (reset reason `crash (panic)`, previous build time), and the device stays reachable. Safe mode itself (repeated failures of an accepted image) cannot be provoked this way and is covered by ESPHome's `safe_mode` component
- [ ] 8.4 Document USB recovery without `erase-flash` and the network-reset build flag in `README.md`, and verify a USB reflash keeps the counters

## 9. Dongle migration

- [ ] 9.1 Write `tools/read_dongle.py` that fetches `cmd=get config` from the dongle's `/api` and the `cmd/<devicetopic>/sendconfig` response, compares prefix, counter and learn mode, exits with status 2 on a mismatch and writes the record (mode 600) otherwise; verified against a fake dongle (consistent and mismatching counter), still to be verified against the running dongle
- [ ] 9.2 Record key, prefix and learn mode in SOPS-encrypted `secrets.yaml` and the dongle counter plus chosen seed in the offline record; verify `sops -d secrets.yaml` shows the values and the offline record exists
- [x] 9.3 Write the migration and rollback runbook `docs/migration.md` (readout, record, seed, power off dongle, verify one channel then one group, switch consumers, calibrate), and verify it covers every step of the design's migration plan
- [ ] 9.5 Implement the takeover in the device (Decision 12): read the dongle twice over its web API, preview without the key, apply identity, labels (group labels by matching topic) and counter + margin, transmit lock that survives restarts, 15 s tracking of the dongle's counter, finish refused while the dongle answers (force with warning); verify against the running dongle and, for finishing, with the dongle switched off
- [ ] 9.4 Execute the migration: Settings → takeover from `192.0.2.20` (read, apply), power off the dongle permanently, finish the takeover, verify one shutter then group `alle`; record the counter in the offline record

## 10. Consumers on the home server

- [ ] 10.1 Point the nine HomeKit window coverings in `charts/mqtt/homekit/values/values.yaml` at `jarolift/<name>/position`, `/state`, `/set` and drop `factor: -1, offset: 100`; verify the rendered chart with `helm template`
- [ ] 10.2 Move the `mqtt-rules` schedules to `jarolift/alle/set`, retire `shutterState` and update `cover.Cover` to the new topics; verify the rules repo's tests pass
- [ ] 10.3 Replace the legacy wildcards in `charts/observability/mqtt-logger` with `jarolift/#`, and rewrite `docs/topic-tree.md` §4b and `docs/device-inventory.md`; verify `helm template` renders and the docs list all shutter and group topics
- [ ] 10.4 Add `charts/infrastructure/device-cert-push` to the home server configuration: a cert-manager `Certificate` for `jarolift.example.com` (ECDSA P-256, netcup DNS-01 issuer) and a CronJob that compares the device's `GET /api/tls` with the issued certificate and pushes it via `POST /api/tls` when they differ; plus a LAN DNS record and a fixed address for the device. Verified so far: `helm template` renders, and the push script against the device (skip when current, push on renewal, chain verifies with hostname). Still open: deployment and a browser showing the Let's Encrypt certificate

## 11. Integration checks

- [ ] 11.1 Calibrate open and close durations for all nine shutters via the Shutter page and verify a move to 50 % lands visibly at half height for each
- [ ] 11.2 End-to-end: HomeKit moves a shutter, `mqtt-rules` runs the nightly `alle` CLOSE, a hand transmitter corrects an estimate, and `mqtt-alerting` fires on unplugging the device; record the results in `docs/commissioning.md`
