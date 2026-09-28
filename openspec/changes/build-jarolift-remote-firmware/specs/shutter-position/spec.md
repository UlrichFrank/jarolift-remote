# Spec Delta

## Purpose

Keeps an estimate of each shutter's position based on travel time, because Jarolift motors never
report their state, and keeps that estimate correct across group commands and hand-transmitter use.

## ADDED Requirements

### Requirement: Position estimate per shutter
Each channel configured as a shutter (initially the nine from the design's channel table) SHALL
have a position estimate from 0 to 100, where 100 is fully open
and 0 is fully closed. While a shutter moves, the estimate SHALL advance in proportion to elapsed
time over the shutter's travel time for that direction. After a reboot the estimate SHALL resume
from its last value, and no frame SHALL be sent because of the reboot.

#### Scenario: Estimate follows travel time
- **WHEN** a shutter with a 20 s close duration at position 100 has been closing for 5 s
- **THEN** its estimate is 75

#### Scenario: Reboot does not move shutters
- **WHEN** the device reboots while shutter `terrasse` is estimated at 40
- **THEN** after boot the estimate is 40 and no frame is sent

### Requirement: Endpoint trips recalibrate
A command to 0 or 100 (including OPEN and CLOSE) SHALL send only the drive frame and no STOP,
because the motor stops at its own limit. Once the travel time has elapsed, the estimate SHALL be
exactly 0 or 100, regardless of the estimate before the trip.

#### Scenario: Close from an unreliable estimate
- **WHEN** a shutter estimated at 60 that is actually at 70 is sent CLOSE
- **THEN** only one DOWN frame is sent, and when the close duration has elapsed the estimate is 0

### Requirement: Intermediate positions
A command to a position between 1 and 99 SHALL send the drive frame towards the target and a STOP
frame when the estimate reaches the target. A command to the current position SHALL send nothing.
A new position command during travel SHALL retarget from the current estimate, reversing direction
if needed.

#### Scenario: Move to half open
- **WHEN** a closed shutter with a 20 s open duration is sent position 50
- **THEN** UP is sent, STOP is sent after about 10 s, and the estimate is 50

#### Scenario: Target equals estimate
- **WHEN** a shutter estimated at 30 is sent position 30
- **THEN** no frame is sent

### Requirement: Runtime travel times
Each shutter SHALL have an open duration and a close duration, settable at runtime without
reflashing and persisted across reboots. A change made while that shutter is idle SHALL apply to
its next movement. A change made while it moves SHALL NOT affect the movement in progress and
SHALL apply once the shutter is idle.

#### Scenario: Calibration survives reboot
- **WHEN** the close duration of `kueche` is set to 23.5 s and the device reboots
- **THEN** the close duration of `kueche` is 23.5 s and is used for the next close

#### Scenario: Change during travel
- **WHEN** the open duration of a shutter is changed while it is opening
- **THEN** the current movement is timed with the old duration and the next movement uses the new one

### Requirement: Group commands advance member estimates
Every configured group (initially `alle`, `eg`, `og`, `og-eltern`, `og-kinder` with their members
from the design's channel table) SHALL accept OPEN, CLOSE, STOP and SHADE. They SHALL
send one telegram addressed to all members by their channel bitmask, and every member's estimate
SHALL then follow the command using that member's own travel times, exactly as if the member had
received the command itself.

#### Scenario: Nightly close of all shutters
- **WHEN** group `alle` receives CLOSE
- **THEN** one DOWN telegram with the mask of channels 0–8 is sent, and all nine shutters reach estimate 0 after their own close durations

#### Scenario: Group stop mid-travel
- **WHEN** group `og-kinder` is opening and receives STOP
- **THEN** one STOP telegram with the mask of channels 4–6 is sent and shutters 4, 5 and 6 each stop at their own current estimate

### Requirement: Group percentage fans out
A position between 0 and 100 exclusive, sent to a group, SHALL be carried out as an individual
position command for each member, one after another, because members travel at different speeds
and cannot share one STOP.

#### Scenario: Group to 50
- **WHEN** group `og-eltern` receives position 50
- **THEN** shutters 7 and 8 each receive their own position-50 command on channels 7 and 8, and each ends at estimate 50

### Requirement: Shade is not a position step
SHADE SHALL send the SHADE frame and SHALL leave the estimate unchanged, because the motor's taught
shade position is not known to the device. The next endpoint trip SHALL recalibrate the estimate
as usual.

#### Scenario: Shade on a shutter
- **WHEN** a shutter estimated at 100 receives SHADE
- **THEN** a SHADE frame is sent and the estimate stays 100 until the next command

### Requirement: Correction from received hand-transmitter telegrams
A decoded UP, DOWN or STOP from a hand transmitter attributed to a shutter or group SHALL update the
affected shutters' estimates as if the device had sent that command at the moment of reception,
without transmitting anything. A received SHADE SHALL be treated like a SHADE command.

#### Scenario: Someone opens the kitchen by hand
- **WHEN** a hand transmitter attributed to `kueche` sends UP while `kueche` is estimated at 0
- **THEN** no frame is sent, the estimate starts rising, and it reaches 100 after the open duration

#### Scenario: Hand stop
- **WHEN** a hand transmitter sends STOP to a shutter that the device has estimated as closing
- **THEN** the estimate freezes at its current value and no frame is sent
