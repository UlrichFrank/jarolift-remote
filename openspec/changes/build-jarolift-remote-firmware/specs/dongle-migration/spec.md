# Spec Delta

## Purpose

Moves the crypto identity and rolling counter from the running ESP8266 dongle
(madmartin/Jarolift_MQTT, fork UlrichFrank/Jarolift_MQTT) to the new device, so that every motor
keeps responding and none has to be re-taught physically, and makes sure the two never transmit in
parallel.

## ADDED Requirements

### Requirement: Takeover from the web interface
The Settings page SHALL offer a takeover from the old dongle: the operator enters the dongle's
address, and the device reads master key, serial prefix, learn mode, device counter and channel
names over the dongle's web API (`POST /api cmd=get config`, `cmd=get channel name`). The
configuration SHALL be read twice, and the takeover SHALL NOT continue if serial prefix or counter
differ between the reads (a command was sent in between). The page SHALL show what was read —
serial prefix, learn mode, counter, channel names, and only whether a master key was found, never
the key — and apply it only after a confirmation. WiFi credentials in the dongle's answer SHALL be
discarded.

#### Scenario: Read the dongle
- **WHEN** an operator enters `192.0.2.20` and clicks "Dongle auslesen"
- **THEN** the page shows prefix, learn mode, counter and the channel names, and states that a master key was found without showing it

#### Scenario: Dongle busy during the read
- **WHEN** the dongle sends a command between the two reads
- **THEN** the page reports that the dongle changed and nothing is applied

#### Scenario: Wrong address
- **WHEN** the address does not answer or answers with something other than the dongle's API
- **THEN** the page names the problem and nothing is applied

### Requirement: Applying the takeover
On confirmation the device SHALL adopt master key, serial prefix and learn mode, optionally take
the dongle's channel names as display labels (topics stay unchanged; names of the dongle's group
channels label the groups whose topic they match, e.g. "OG Eltern" → `og-eltern`), and set its
counter to the dongle's counter plus a safety margin (default 8, at most 64). It SHALL never lower
its counter this way.

#### Scenario: Takeover applied
- **WHEN** the operator confirms the takeover of a dongle with counter 27019 and margin 8
- **THEN** the device uses the dongle's identity, its counter is at least 27027, and the shutters carry the dongle's channel names as labels

### Requirement: No parallel transmission
From the takeover until the operator finishes it, the device SHALL NOT transmit and SHALL log every
suppressed telegram. While locked it SHALL poll the dongle at least every 15 s and keep its own
counter at the dongle's latest counter plus the margin, so the first own telegram is ahead of
everything the dongle sent. The lock and the dongle's address SHALL survive restarts and updates.
Finishing the takeover SHALL be refused while the dongle still answers, unless the operator
explicitly forces it after a warning.

#### Scenario: Command while locked
- **WHEN** a shutter command arrives during the takeover
- **THEN** no telegram is sent and the log says transmission is locked

#### Scenario: Dongle still in use
- **WHEN** the dongle sends further commands during the takeover and its counter rises to N
- **THEN** the device's counter becomes at least N + margin

#### Scenario: Dongle still reachable
- **WHEN** the operator tries to finish the takeover while the dongle answers
- **THEN** it is refused with the request to switch the dongle off permanently

#### Scenario: Dongle switched off
- **WHEN** the dongle no longer answers and the operator finishes the takeover
- **THEN** the lock is released and the next command is transmitted with the tracked counter

### Requirement: Identity recorded outside the device
The key, prefix and learn mode SHALL also be stored in the encrypted secrets file used to build the
firmware. The dongle's last counter value SHALL be kept in an offline record, together with the
counter of the device after the takeover.

#### Scenario: Rebuild after device loss
- **WHEN** the device has to be replaced
- **THEN** the identity is available from the secrets file and the last known counter from the offline record

### Requirement: Staged verification
After the takeover is finished, the procedure SHALL verify one shutter and then one group before the
consumers in `home-new` are switched to the new topics. The group check also confirms that the
motors taught to the old dongle follow a bitmask group telegram; if they do not, the old group
channels stay in use until that is resolved.

#### Scenario: First channel works
- **WHEN** a single shutter responds to OPEN and CLOSE from the new device and a group then responds to CLOSE
- **THEN** the HomeKit values, `mqtt-rules` and the `mqtt-logger` allowlist are switched in the same rollout

#### Scenario: First channel fails
- **WHEN** the verified shutter does not respond
- **THEN** the consumers are not switched and the rollback procedure applies

### Requirement: Documented rollback
The procedure SHALL document a rollback. Before the dongle is switched off, it is to cancel the
takeover and keep the dongle. After that, the dongle's counter has to be set above the device's
counter before the dongle is powered on again, and the `home-new` consumers have to be reverted.

#### Scenario: Rollback after cut-over
- **WHEN** a rollback is needed after the new device has transmitted
- **THEN** the dongle's counter is set above the device's counter before the dongle transmits again
