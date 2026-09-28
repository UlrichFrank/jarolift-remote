# Spec Delta

## Purpose

Sends and receives Jarolift TDEF Keeloq telegrams on 433 MHz, so that the existing motors, which
were taught to the old dongle, respond to this device without being re-taught.

## ADDED Requirements

### Requirement: Channel serials compatible with the old dongle
The device SHALL address sixteen channels, 0 to 15, like the old dongle. Channel *i* SHALL use the serial
`(prefix << 8) + i`, where `prefix` is the migrated serial prefix. Every frame SHALL be encrypted
with the migrated 64-bit master key, and the migrated learn-mode flag SHALL be applied, exactly as
the old dongle did it.

#### Scenario: Motor taught to the old dongle responds
- **WHEN** the device sends DOWN on channel 3 after migration
- **THEN** the motor taught to channel 3 of the old dongle closes, and it was not re-taught

#### Scenario: Channel outside the configured range
- **WHEN** a transmission is requested for a channel outside 0 to 15
- **THEN** no frame is sent and the request is logged as rejected

### Requirement: Supported telegram commands
The device SHALL send UP, DOWN, STOP, SHADE, SETSHADE, LEARN and UNLEARN on any configured channel. UP,
DOWN and STOP SHALL be sent as two repetitions of the telegram. SHADE SHALL be sent as STOP with 20
repetitions (a long press), SETSHADE as four single STOP telegrams 300 ms apart, each with its own
counter value, followed by a 2 s pause before the next telegram. LEARN SHALL follow the configured
learn mode: in new learn mode it sends UP and DOWN together followed by STOP, in old learn mode the
LEARN telegram, as the old dongle did. UNLEARN SHALL send UP+DOWN, six STOP and one UP telegram,
300 ms apart and two repetitions each, as dewenni's ESP32-Jarolift-Controller does; every motor that
knows the channel forgets it. The timing of preamble, bits and
repetitions SHALL match the old dongle: 13-pulse preamble at 780 µs, 3.5 ms gap, 72 bits at
1200 µs, 16 ms between repetitions.

#### Scenario: UP on a shutter channel
- **WHEN** UP is requested on channel 0
- **THEN** two repetitions of one UP telegram for the channel 0 serial are sent, taking about 234 ms in total

#### Scenario: Teach a motor in new learn mode
- **WHEN** LEARN is requested on channel 3 with new learn mode configured
- **THEN** the UP+DOWN telegram and then a STOP telegram for the channel 3 serial are sent

#### Scenario: Unlearn a channel
- **WHEN** UNLEARN is requested on channel 9
- **THEN** UP+DOWN, six STOP and one UP telegram for the channel 9 serial are sent, each with its own counter value

#### Scenario: Shade position taught through the device
- **WHEN** SETSHADE is requested on channel 5
- **THEN** four STOP telegrams for the channel 5 serial are sent 300 ms apart, using four consecutive counter values

### Requirement: Shared rolling counter
The device SHALL use one 16-bit rolling counter for all channels and groups, like the old dongle's
device counter and dewenni's controller: the motors identify the sender by its serial base, so every
telegram SHALL carry a higher counter value than the one before, whatever channel it addresses.
Repetitions of one telegram share a value. The counter SHALL be persisted in non-volatile storage no
later than the moment the frame is sent, so no value is reused after a power loss, reboot, OTA update
or regular reflash. Per-channel counters stored by earlier firmware SHALL be merged into the shared
counter by taking the highest value.

#### Scenario: Counter survives a power cut
- **WHEN** the last telegram used counter value N and the device loses power and boots again
- **THEN** the next telegram, on any channel, uses a counter value greater than N

#### Scenario: Channels share the counter
- **WHEN** a telegram on channel 9 uses value N and the next one goes to channel 4
- **THEN** the telegram on channel 4 uses a value greater than N

#### Scenario: Counter is readable for commissioning
- **WHEN** an operator opens the Jarolift settings of the web interface
- **THEN** the current counter value is shown

### Requirement: Serialized transmit queue with STOP priority
The device has one radio, so it SHALL send one telegram at a time from a queue. A STOP request
SHALL be sent before any queued UP, DOWN or SHADE request for the same channel, and SHALL remove
those queued drive requests, so that no shutter is driven past the point where it was told to stop.
Requests for different channels SHALL keep the order in which they were requested, except for STOP
priority.

#### Scenario: STOP overtakes a queued drive command
- **WHEN** DOWN on channel 7 is queued behind other frames and STOP on channel 7 is requested
- **THEN** STOP on channel 7 is sent next and the queued DOWN on channel 7 is never sent

#### Scenario: Group fan-out keeps its order
- **WHEN** individual commands for channels 4, 5 and 6 are requested in that order
- **THEN** their frames are sent in the order 4, 5, 6

### Requirement: Group telegrams by channel bitmask
A group SHALL be sent as one telegram that carries the serial of channel 0 and, in its
discrimination bits, the bitmask of all member channels, as dewenni's controller does; motors taught
to any of those channels react to it. While a group command is being applied to its member
shutters, the members' own frames SHALL be suppressed, so that only the one group telegram is sent.

#### Scenario: Group close sends one telegram
- **WHEN** CLOSE is applied to group `og-eltern` (members on channels 7 and 8)
- **THEN** exactly one DOWN telegram is sent, with the channel 0 serial and the mask `0x0180`, and no telegram on channel 7 or 8

#### Scenario: Motors taught to the old dongle follow a group telegram
- **WHEN** a DOWN telegram with the mask of channels 1 and 2 is sent after migration
- **THEN** the motors taught to channels 1 and 2 of the old dongle close (verified in the spike before the old group channels are given up)

### Requirement: Receiving hand-transmitter telegrams
The device SHALL listen on 433 MHz whenever it is not transmitting and SHALL decode received
Jarolift telegrams whose serial it can attribute to a configured shutter or group, reporting the
serial and command (UP, DOWN, STOP, SHADE) to the position logic. Telegrams that cannot be
decrypted or attributed SHALL be ignored and SHALL NOT trigger a transmission. Its own transmitted
frames SHALL NOT be reported as received telegrams.

#### Scenario: Known hand transmitter pressed
- **WHEN** a hand transmitter configured for shutter `kueche` sends DOWN
- **THEN** a received DOWN for `kueche` is reported to the position logic

#### Scenario: Unknown transmitter nearby
- **WHEN** a telegram from a serial that is not configured is received
- **THEN** no position changes and nothing is transmitted

### Requirement: Master key confidentiality
The master key SHALL NOT appear in any log line, MQTT message, web server page or entity state.

#### Scenario: Verbose logging
- **WHEN** the log level is set to the most verbose level and frames are sent
- **THEN** the log shows channel, command and counter, but not the master key
