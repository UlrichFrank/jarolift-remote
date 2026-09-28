# Spec Delta

## Purpose

Defines the topics and payloads the device uses on the house broker under the `jarolift/` root,
following the house topic contract (`docs/topic-tree.md`), for the HomeKit bridge, `mqtt-rules`, `mqtt-logger`
and `mqtt-alerting`.

## ADDED Requirements

### Requirement: Bridge liveness
The device SHALL publish `online` to `jarolift/bridge/state`, retained, on every connection to the
broker, and SHALL register `offline` on the same topic, retained, as its last will.

#### Scenario: Device powered off
- **WHEN** the device loses power while connected
- **THEN** the broker publishes the retained `offline` on `jarolift/bridge/state`, and `mqtt-alerting`'s `bridge-offline` rule matches it without change

#### Scenario: Reconnect
- **WHEN** the device reconnects after a broker restart
- **THEN** `jarolift/bridge/state` is `online` again, retained

### Requirement: Per-shutter state topics
For each channel configured as a shutter, with its configured name `<name>` (see the web-interface
capability; initially the design's channel table), the device SHALL publish, retained:
- `jarolift/<name>/state`: `opening` or `closing` while moving; when idle, `closed` at position 0
  and `open` otherwise.
- `jarolift/<name>/position`: the estimate as an integer from 0 to 100, where 100 is open.

Both SHALL be published on every state change, periodically during travel, and after each
(re)connect. Payloads SHALL be plain scalars, not JSON.

#### Scenario: Shutter finishes closing
- **WHEN** `kueche` reaches its closed endpoint
- **THEN** `jarolift/kueche/state` is `closed` and `jarolift/kueche/position` is `0`, both retained

#### Scenario: Shutter stopped half way
- **WHEN** `terrasse` stops at an estimate of 50
- **THEN** `jarolift/terrasse/state` is `open` and `jarolift/terrasse/position` is `50`

### Requirement: Command topic
The device SHALL subscribe to `jarolift/<name>/set` for every shutter and group name. It SHALL
accept `OPEN`, `CLOSE`, `STOP`, `SHADE` and an integer `0` to `100`; shutters SHALL also accept
`SETSHADE`. Keywords SHALL be matched case-insensitively. Any other payload SHALL be logged and
SHALL cause no transmission and no state change. The device SHALL NOT publish to any `/set` topic.

#### Scenario: Numeric command
- **WHEN** `30` is published to `jarolift/wohnzimmer-links/set`
- **THEN** `wohnzimmer-links` moves to estimate 30

#### Scenario: Keyword command on a group
- **WHEN** `CLOSE` is published to `jarolift/alle/set`
- **THEN** group `alle` closes as defined by the shutter-position capability

#### Scenario: Invalid payload
- **WHEN** `half` or `150` is published to `jarolift/kueche/set`
- **THEN** nothing is sent and the retained state topics of `kueche` are unchanged

### Requirement: State refresh topic
The device SHALL subscribe to `jarolift/<name>/get` for every shutter and group. Any message there
SHALL republish the current state and position of that shutter, or of every member of that group.

#### Scenario: Refresh a group
- **WHEN** any payload is published to `jarolift/og-eltern/get`
- **THEN** state and position of `eltern-links` and `eltern-rechts` are republished

### Requirement: Groups publish no state of their own
Groups SHALL have `/set` and `/get` topics but SHALL NOT publish `state` or `position`; their
effect is visible only on the member shutters' topics.

#### Scenario: Group topic tree
- **WHEN** a consumer subscribes to `jarolift/eg/#`
- **THEN** it receives no retained `state` or `position` message from the device

### Requirement: Travel-time topics
For each shutter the device SHALL publish its open and close durations in seconds, retained, on
`jarolift/<name>/open_duration` and `jarolift/<name>/close_duration`, and SHALL accept new values
on `jarolift/<name>/open_duration/set` and `jarolift/<name>/close_duration/set`. Values that are not
a positive number within the configured limits SHALL be rejected and logged.

#### Scenario: Calibrate over MQTT
- **WHEN** `21.4` is published to `jarolift/eltern-links/close_duration/set`
- **THEN** `jarolift/eltern-links/close_duration` is republished retained as `21.4` and the next close uses 21.4 s

### Requirement: Retain and QoS rules
State, position, duration and bridge topics SHALL be published retained at QoS 1. Command topics
SHALL be subscribed at QoS 1. The device SHALL NOT publish Home Assistant discovery messages and
SHALL NOT publish or subscribe to the legacy `cmd/jarolift/…`, `stat/jarolift/…` or
`tele/jarolift/…` topics. All device topics SHALL be under `jarolift/`, so the `mqtt-logger`
allowlist `jarolift/#` covers them.

#### Scenario: No legacy or discovery traffic
- **WHEN** a consumer subscribes to `#` while the device boots and operates
- **THEN** every message published by the device has a topic starting with `jarolift/`
