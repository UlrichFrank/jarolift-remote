# Spec Delta

## Purpose

Specifies how the Olimex ESP32-PoE node runs: network, broker connection, settings persistence,
updates, diagnostics and recovery, so that it can be run and repaired without physical access.

## ADDED Requirements

### Requirement: Wired Ethernet
The device SHALL connect over the ESP32-PoE's wired Ethernet, either with DHCP or with a static IP
address, network mask, gateway and DNS server, as configured in the web interface. Until a static
configuration is saved, the device SHALL use DHCP. It SHALL NOT enable WiFi and SHALL NOT open an
access point under any condition.

#### Scenario: Normal boot with the default configuration
- **WHEN** the device is powered over PoE for the first time
- **THEN** it obtains its address by DHCP and is reachable there

#### Scenario: Static configuration applied at boot
- **WHEN** a static configuration was saved and the device restarts
- **THEN** it uses the saved address, mask, gateway and DNS server without asking a DHCP server

#### Scenario: No fallback access point
- **WHEN** the Ethernet link is down for an extended period
- **THEN** no WiFi network is broadcast by the device

### Requirement: Broker connection
The device SHALL connect to the broker given by the configured broker URL (see the web-interface
capability), over plain MQTT, MQTT over TLS, MQTT over WebSocket or MQTT over a TLS WebSocket as the
URL's scheme says. Hostnames SHALL be resolved through DNS on every connection attempt. The device
SHALL reconnect automatically after a broker, DNS or network outage, with a delay between attempts.
Commands received while disconnected are not required to be delivered. After reconnecting, it SHALL
republish all retained state it owns. With an empty broker URL it SHALL NOT connect to any broker.

#### Scenario: Broker restart
- **WHEN** the broker restarts
- **THEN** the device reconnects without a reboot and republishes bridge state, all shutter states, positions and durations

#### Scenario: DNS failure
- **WHEN** the broker's hostname cannot be resolved
- **THEN** the device logs the failure, keeps retrying, and shutters remain operable from the web interface

### Requirement: Safe boot behaviour
Booting, reconnecting or recovering SHALL NOT transmit any radio frame. Frames SHALL be sent only in
response to a command.

#### Scenario: Power cycle
- **WHEN** the device is power-cycled
- **THEN** no shutter moves

### Requirement: Over-the-air updates and safe mode
The device SHALL accept password-protected firmware updates over Ethernet. A newly installed
firmware that crashes before it has booted successfully SHALL be rolled back to the previous
firmware automatically. A firmware that was accepted but then fails to boot repeatedly SHALL start
in a safe mode that still accepts an OTA update. An OTA update SHALL preserve rolling counters,
runtime settings, travel times, position estimates and the HTTPS certificate.

#### Scenario: Faulty update
- **WHEN** a new firmware that crashes during boot is uploaded over the network
- **THEN** the device returns to the previous firmware by itself and stays reachable

#### Scenario: OTA update
- **WHEN** a new firmware is uploaded over the network with the correct password
- **THEN** the device reboots into it and every channel's counter is at least its value before the update

#### Scenario: Boot loop
- **WHEN** a faulty firmware fails to boot several times in a row
- **THEN** the device enters safe mode and a corrected firmware can be uploaded over the network

### Requirement: USB serial recovery
When network recovery is not possible, the device SHALL be flashable over its USB port. The
documented flashing procedure SHALL NOT erase the non-volatile storage that holds the counters.

#### Scenario: Recovery by cable
- **WHEN** the device is reflashed over USB following the documented procedure
- **THEN** the rolling counters are preserved

### Requirement: Runtime settings persistence
Settings changed in the web interface (network, broker, Jarolift identity, labels, travel times)
SHALL be persisted in non-volatile storage and SHALL survive restarts, OTA updates and regular
reflashes. Values from the build configuration SHALL only serve as defaults for settings that were
never saved.

#### Scenario: Broker URL survives an update
- **WHEN** the broker URL was changed in the web interface and a new firmware is installed over OTA
- **THEN** the device connects to the broker URL saved in the web interface

### Requirement: Logs
Logs SHALL be available on the web interface's Log page and over the USB serial port.

#### Scenario: Diagnose by cable
- **WHEN** an operator connects to the USB serial port
- **THEN** the same log messages as on the Log page are printed

### Requirement: Secrets handling
The master key, serial prefix, OTA password, web credentials and broker credentials SHALL come
from an encrypted secrets file at build time, as defaults that the web interface may override.
The master key, OTA password, broker password and the HTTPS private key SHALL NOT be shown on the
web interface, returned by its API, published over MQTT or written to the logs.

#### Scenario: Inspect the device
- **WHEN** an operator views every web page and subscribes to `jarolift/#`
- **THEN** neither the master key nor any password appears
