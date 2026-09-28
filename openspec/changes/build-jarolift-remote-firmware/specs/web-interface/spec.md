# Spec Delta

## Purpose

Rebuilds the old dongle's web interface on the new device in the style of dewenni's
ESP32-Jarolift-Controller, served over HTTPS, so that shutters and groups can be operated, added and
taught, the old dongle can be taken over, and the device can be configured at runtime from a
browser, including a broker given as a URL with protocol and hostname.

## ADDED Requirements

### Requirement: Pages and access
The device SHALL serve a web interface with the pages Shutters, Groups, Settings, System and Log,
reachable from a common navigation, over HTTPS on port 443. Port 80 SHALL answer every request only with a
redirect to the same path over HTTPS, without content and without asking for credentials. Every
page and every action behind it SHALL require the configured web credentials. The interface SHALL
work without internet access, so all of its assets are served by the device itself.

#### Scenario: Log in
- **WHEN** an operator opens `https://<device>/` in a browser and enters the web credentials
- **THEN** the Shutters page is shown with navigation to Groups, Settings, System and Log

#### Scenario: Plain HTTP
- **WHEN** a browser requests `http://<device>/anything`
- **THEN** the device answers with a redirect to `https://<device>/anything` and serves no content

#### Scenario: No credentials
- **WHEN** any page or action is requested without valid credentials
- **THEN** the request is refused and nothing is changed or sent

#### Scenario: Offline network
- **WHEN** the browser has no internet access but reaches the device
- **THEN** all pages render completely

### Requirement: System page
The System page SHALL show the firmware version, the device's current IP address, the network link
state, the configured broker URL with the current connection state (connected, connecting,
disconnected, disabled) and the last connection error, if any, as well as the MQTT topic layout the
device uses.

#### Scenario: Broker unreachable
- **WHEN** the configured broker cannot be reached
- **THEN** the System page shows the state `connecting` or `disconnected` and the last error, for example a DNS or TLS failure

### Requirement: Shutters and Groups pages
The Shutters page SHALL show one card per active shutter and the Groups page one card per active
group, each with its display label, and controls for OPEN, STOP, CLOSE, SHADE and a target
position from 0 to 100; shutter cards also show the current position and state, group cards the
members. Every action SHALL behave exactly like the same command received over MQTT, the page SHALL
show position changes while shutters move without a manual reload, and each action SHALL be
confirmed with a short status message.

#### Scenario: Close from the browser
- **WHEN** an operator clicks CLOSE on the card of `kueche`
- **THEN** `kueche` closes as if `CLOSE` had been published to `jarolift/kueche/set`, and its position on the card counts down to 0

#### Scenario: Group from the browser
- **WHEN** an operator clicks CLOSE on the card of group `alle`
- **THEN** one telegram with the mask of all members is sent and the positions of all nine shutters on the Shutters page follow

### Requirement: Shutter settings
The Settings page SHALL list the sixteen channels, numbered 1 to 16 as in dewenni's controller,
each in one row with a switch "active", a display label, the topic name, open and close duration,
and the actions LEARN, UNLEARN and SETSHADE. A change to a row SHALL be saved as soon as the field
is left, without a separate save step, and take effect without a restart. The topic name SHALL be
kebab-case (`a-z`, `0-9`, `-`), unique across shutters and groups and not `bridge`; when a channel is
activated without one, it SHALL be derived from the label ("EG Küche Fenster" →
`eg-kueche-fenster`). Invalid input SHALL be rejected with a message at the field. When a shutter is
renamed or deactivated, the device SHALL clear the retained state topics under the old name. The
build configuration only provides the initial layout. The page SHALL explain how a motor is put
into learn mode (learn button on the motor or switching its power off and on, then LEARN within
5 s; or UP+DOWN and 8× STOP on an already taught hand transmitter) and which learn mode is used.
LEARN, UNLEARN and SETSHADE SHALL only be sent after a confirmation that names the channel.

#### Scenario: Add a shutter
- **WHEN** an operator activates channel 15, enters the label "Gäste Fenster" and leaves the field
- **THEN** it is saved with topic `gaeste-fenster`, appears on the Shutters page and publishes `jarolift/gaeste-fenster/state` and `position`

#### Scenario: Teach it
- **WHEN** the operator presses the learn button on the new motor and clicks LEARN for channel 15 within 5 s and confirms
- **THEN** the learn sequence for that channel is sent according to the configured learn mode

#### Scenario: Cancelled confirmation
- **WHEN** an operator clicks LEARN or UNLEARN and cancels the confirmation
- **THEN** nothing is sent

#### Scenario: Rename a topic
- **WHEN** an operator changes the topic of `kind3-fenster` to `gaeste-fenster`
- **THEN** the retained `jarolift/kind3-fenster/…` state topics are cleared and the shutter publishes under `jarolift/gaeste-fenster/…`

#### Scenario: Change a label
- **WHEN** an operator changes the label of `kind3-fenster` to "Kinderzimmer Fenster"
- **THEN** the cards show the new label and the shutter still uses `jarolift/kind3-fenster/…`

#### Scenario: Invalid input
- **WHEN** an operator gives two shutters the topic `kueche`, or uses `Küche` as a topic
- **THEN** nothing is saved and the field shows the problem

#### Scenario: Edit a travel time
- **WHEN** an operator sets the close duration of `terrasse` to 19.0 s
- **THEN** the value is persisted and republished on `jarolift/terrasse/close_duration`

### Requirement: Group settings
The Settings page SHALL list up to eight groups, each in one row with a switch "active", a display
label, the topic name and the members. Members SHALL be chosen in a selection dialog with one
checkbox per active shutter, which also shows the resulting 16-bit channel mask. Groups SHALL NOT
need to be taught. A group needs at least one member, and members must be active shutters; changes
are saved immediately like shutter rows.

#### Scenario: Add a group
- **WHEN** an operator activates group 6, labels it "Süd" and selects `terrasse` and `wohnzimmer-links` in the dialog
- **THEN** a CLOSE on `jarolift/sued/set` sends one telegram with the mask of both channels and the estimates of both members follow

#### Scenario: Member deactivated
- **WHEN** an operator deactivates a shutter that is still a member of a group
- **THEN** nothing is saved and the message names the group

### Requirement: HTTPS certificate
The HTTPS server SHALL use a certificate and private key stored on the device. On first start, and
whenever no valid pair is stored, the device SHALL generate a self-signed certificate for its own
use. A certificate chain and private key in PEM format SHALL be installable through the
authenticated API, so that an external job can push a renewed Let's Encrypt certificate; the device
SHALL reject a pair that does not parse or whose key does not match the certificate, and SHALL use
an accepted pair for new connections without a restart. The private key SHALL never be returned by
any API or shown on any page; the Settings page SHALL show the certificate's subject, issuer and
expiry date.

#### Scenario: Before the first push
- **WHEN** the device starts without a stored certificate
- **THEN** it serves HTTPS with a self-signed certificate and the Settings page shows it as self-signed

#### Scenario: Push a renewed certificate
- **WHEN** the certificate job posts a valid chain and key for `jarolift.example.com`
- **THEN** new HTTPS connections present that certificate, and the Settings page shows the new expiry date

#### Scenario: Mismatched key
- **WHEN** a certificate is posted together with a key that belongs to a different certificate
- **THEN** the request is rejected and the previous certificate stays in use

### Requirement: Settings — network
The Settings page SHALL allow configuring the wired network: DHCP on or off, and for a static
configuration the IP address, network mask, gateway and DNS server. Invalid addresses SHALL be
rejected with a message and not saved. Saved network settings SHALL be persisted and SHALL take
effect after a restart, which the page offers.

#### Scenario: Switch to a static address
- **WHEN** an operator disables DHCP, enters IP, mask, gateway and DNS, saves and restarts
- **THEN** the device is reachable at the new static address and resolves hostnames through the given DNS server

#### Scenario: Invalid network mask
- **WHEN** an operator enters `255.0.255.0` as network mask
- **THEN** the settings are not saved and the field is marked invalid

### Requirement: Settings — broker as URL
The Settings page SHALL accept the broker as a URL of the form
`[<scheme>://]<host>[:<port>][/<path>]`:
- `<scheme>` is one of `mqtt` (plain TCP, default port 1883), `mqtts` (TLS, default port 8883),
  `ws` (WebSocket, default port 80) or `wss` (WebSocket over TLS, default port 443), matched
  case-insensitively. Without a scheme, `mqtt` applies.
- `<host>` is an IPv4 address, a hostname or a fully qualified domain name.
- `<port>` is optional, from 1 to 65535; without it the scheme's default port applies.
- `<path>` is only allowed for `ws` and `wss` and defaults to `/mqtt`.

An empty broker field SHALL disable the broker connection. Any other value that does not match
this form SHALL be rejected with a message naming the problem and SHALL NOT be saved. The default
value SHALL be `mqtt://192.0.2.10:1883`.

#### Scenario: Plain IP as before
- **WHEN** an operator enters `192.0.2.10`
- **THEN** it is saved and the device connects over plain MQTT to `192.0.2.10:1883`

#### Scenario: Hostname with TLS
- **WHEN** an operator enters `mqtts://mqtt.home.example.org`
- **THEN** it is saved and the device resolves the hostname through DNS and connects with TLS to port 8883

#### Scenario: WebSocket with path and port
- **WHEN** an operator enters `wss://broker.example.org:9443/ws`
- **THEN** the device connects with MQTT over a TLS WebSocket to `broker.example.org:9443` on path `/ws`

#### Scenario: Unsupported scheme
- **WHEN** an operator enters `http://192.0.2.10`
- **THEN** the value is not saved and the message names `http` as an unsupported scheme

#### Scenario: Path on a plain MQTT URL
- **WHEN** an operator enters `mqtt://192.0.2.10/mqtt`
- **THEN** the value is not saved and the message says a path is only allowed for `ws` and `wss`

#### Scenario: Port out of range
- **WHEN** an operator enters `mqtt://broker:70000`
- **THEN** the value is not saved and the port field is reported as invalid

#### Scenario: Disable the broker
- **WHEN** an operator clears the broker field and saves
- **THEN** the device closes its broker connection, does not reconnect, and stays operable from the web interface

### Requirement: Settings — broker credentials and TLS trust
Next to the broker URL the Settings page SHALL offer username, password, client ID and, for `mqtts`
and `wss`, a CA certificate in PEM format. The client ID SHALL default to a value derived from the
device's MAC address. For TLS connections the broker's certificate SHALL be verified against the
configured CA certificate, or against a built-in bundle of public root CAs if none is configured,
and the host name in the URL SHALL be checked against the certificate. A connection whose
certificate does not verify SHALL NOT be established. A CA field that is not a valid PEM
certificate SHALL be rejected.

#### Scenario: Private CA
- **WHEN** the broker URL is `mqtts://mqtt.home.lan` and the house CA certificate is pasted
- **THEN** the device connects if the broker's certificate is signed by that CA for `mqtt.home.lan`

#### Scenario: Wrong certificate
- **WHEN** the broker presents a certificate that the configured CA did not sign
- **THEN** no connection is established and the System page shows a TLS verification error

### Requirement: Broker settings apply without reflashing
Saving broker URL, credentials, client ID or CA certificate SHALL persist them and SHALL make the
device disconnect and reconnect with the new settings without a reflash or restart. Hostnames SHALL
be resolved again on every connection attempt, so a changed DNS record takes effect on the next
reconnect.

#### Scenario: Move to another broker
- **WHEN** an operator changes the broker URL from `mqtt://192.0.2.10` to `mqtt://mqtt.home.lan` and saves
- **THEN** the device publishes `offline` to the old broker's `jarolift/bridge/state` (or the old broker applies the last will), then connects to the new broker and publishes `online` and all retained state there

### Requirement: Settings — Jarolift
The Settings page SHALL show the Jarolift identity settings: master key (MSB and LSB, each a 32-bit
hex number), learn mode (new or old), serial prefix (24-bit hex number) and the rolling counter. The master key SHALL be write-only: the page shows whether it is set, never its value.
Changing the serial prefix and regenerating the channel serials SHALL require an explicit
confirmation that warns that every motor then has to be taught again. Setting the counter below its
current value SHALL require an explicit confirmation that warns about losing the motors.

#### Scenario: Master key hidden
- **WHEN** an operator opens the Settings page on a migrated device
- **THEN** the master key fields show that a key is set but not the key, and the page source contains no key value

#### Scenario: Seed counters during migration
- **WHEN** an operator sets the counter to the migration seed value, which is higher than its current value, and saves
- **THEN** the counter is persisted and the next telegram uses a value of at least the seed

#### Scenario: Lowering a counter
- **WHEN** an operator enters a counter value lower than the current one
- **THEN** it is saved only after a confirmation warning that motors may stop responding

#### Scenario: Invalid hex value
- **WHEN** an operator enters `0x12345` as master MSB with an invalid character or wrong length
- **THEN** nothing is saved and the field is marked invalid

### Requirement: Settings — maintenance
The Settings page SHALL offer a restart of the device and SHALL show the firmware version. A restart
SHALL NOT send any frame.

#### Scenario: Restart from the browser
- **WHEN** an operator clicks restart
- **THEN** the device reboots, keeps all settings, counters and positions, and no shutter moves

### Requirement: Log page
The Log page SHALL show the device's recent log messages, including sent and received telegrams
(channel, command, counter) and broker connection events, and SHALL update without a manual
reload. It SHALL never show secrets.

#### Scenario: Watch a command
- **WHEN** a shutter is closed over MQTT while the Log page is open
- **THEN** the log shows the received command and the sent telegram with channel and counter within a few seconds
