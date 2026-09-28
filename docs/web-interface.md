# Web interface

The device serves a single-page interface at `https://<device>/`, modelled on dewenni's
ESP32-Jarolift-Controller:

| Page | Purpose |
|---|---|
| Rollläden | One card per active shutter: label, position, ▲ ■ ▼ shade, target position |
| Gruppen | One card per active group: label, members, ▲ ■ ▼ shade, target position |
| Einstellungen | Sixteen shutter rows (label, topic, travel times, Anlernen / Abmelden / Beschattung setzen, active switch — saved when a field is left), up to eight groups (active, label, topic, members via selection dialog), takeover from the old dongle, Jarolift identity and counter, network, MQTT, HTTPS certificate, restart |
| System | Firmware, IP, uptime, last reset reason, radio, counter, broker state and last error, topic layout |
| Log | The most recent log lines, updated every 1.5 s |

Channels are numbered 1–16 on the page (like dewenni); the API and the log use 0–15.

HTTPS runs on port 443. Port 80 only answers with `301` to the same path over HTTPS. Every page
and every API call requires HTTP basic auth with the web credentials from `secrets.yaml`
(`web_username`, `web_password`). Positions and the log are refreshed by polling; nothing is
loaded from the internet.

Until a certificate is installed, the device serves a self-signed one it generated on first start
(browser warning). The Let's Encrypt certificate for `jarolift.example.com` is pushed by the
certificate job on the home server (see `/api/tls` below).

## API

All examples assume:

```sh
DEV=https://192.0.2.30
AUTH="admin:change-me"
```

`-k` is only needed while the device uses its self-signed certificate. Requests with a body use
`Content-Type: application/json`. Errors come back as HTTP 400 with
`{"errors": [{"field": "...", "message": "..."}]}`, one entry per invalid field.

### Status

```sh
curl -sk -u "$AUTH" "$DEV/api/status"
```

Firmware build time, IP address, `network_connected`, `uptime_s`, `reset_reason`, `radio`,
`broker_url`, `broker_state` (`connected` | `connecting` | `disconnected` | `disabled`),
`broker_error`, `learn_mode_new`, `counter`, `migration` (takeover state), `certificate`
(`subject`, `issuer`, `not_after`, `self_signed`), every active shutter (`name`, `label`, `channel`,
`state`, `position`, `open_duration`, `close_duration`) and every active group (`index`, `name`,
`label`, `members` as labels).

### Commands

```sh
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"target":"kueche","command":"CLOSE"}' "$DEV/api/command"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"target":"og-eltern","command":"POSITION","position":50}' "$DEV/api/command"
```

`command` is `OPEN`, `CLOSE`, `STOP`, `SHADE`, `POSITION` with `position` 0–100 (100 = open), or —
for shutters only — `SETSHADE`, `LEARN` and `UNLEARN`. `target` is a shutter or group topic name. A
command behaves exactly like the same payload on `jarolift/<target>/set`; `LEARN` and `UNLEARN` are
only accepted here, never over MQTT. A group command is one telegram addressed to all members by
their channel bitmask.

### Travel times

```sh
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"target":"terrasse","which":"close","seconds":19.0}' "$DEV/api/duration"
```

`which` is `open` or `close`, `seconds` 1–300. The value is persisted, republished on
`jarolift/<target>/<which>_duration` and applied once the shutter is idle.

### Settings and channels

```sh
curl -sk -u "$AUTH" "$DEV/api/settings"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"broker_url":"mqtt://mqtt.example.com"}' "$DEV/api/settings"
```

`GET` returns every setting except secrets: the MQTT password and the master key appear only as
`mqtt_password_set` / `master_key_set`. `POST` takes any subset of:

| Field | Format |
|---|---|
| `dhcp`, `ip`, `subnet`, `gateway`, `dns` | bool, IPv4 strings; applied after a restart |
| `broker_url` | `[mqtt\|mqtts\|ws\|wss://]host[:port][/path]`, empty = no broker |
| `mqtt_username`, `mqtt_password`, `client_id` | strings; omit `mqtt_password` to keep it, `""` clears it |
| `ca_certificate` | PEM, used for `mqtts` and `wss`; empty = built-in public root CAs |
| `master_msb`, `master_lsb` | 32-bit hex, write-only |
| `serial_prefix` | 24-bit hex; a change also needs `"confirm_regenerate_serials": true` |
| `learn_mode_new` | bool |
| `channels` | list of `{"channel": 0–15, "enabled", "name", "label"}`; only the listed channels and fields change |
| `groups` | list of `{"index": 0–7, "enabled", "name", "label", "members": [channel, …]}`; only the listed groups and fields change |

The response is `{"ok": true, "restart_required": <bool>}`. Broker and channel changes take
effect immediately; renaming or deactivating a shutter clears its retained `jarolift/<old name>/…`
topics. Topic names are unique across shutters and groups. Settings saved here override the build-time values from `secrets.yaml` and the YAML's
channel table.

Activate a shutter on channel 15 (index 14) and a group, then teach the new shutter (press the
learn button on its motor first; LEARN within 5 s):

```sh
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"channels":[{"channel":14,"enabled":true,"name":"gaeste-fenster","label":"Gäste Fenster"}],"groups":[{"index":5,"enabled":true,"name":"sued","label":"Süd","members":[1,2]}]}' \
  "$DEV/api/settings"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' \
  -d '{"target":"gaeste-fenster","command":"LEARN"}' "$DEV/api/command"
```

### Rolling counter

```sh
curl -sk -u "$AUTH" "$DEV/api/counter"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' -d '{"counter":27100}' "$DEV/api/counter"
```

One counter for all channels and groups, like the old dongle; the value is the next one to send.
Lowering it is refused unless the request contains `"confirm_lower": true`, and every change is
logged.

### Takeover from the old dongle

```sh
curl -sk -u "$AUTH" "$DEV/api/migration"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' -d '{"host":"192.0.2.20"}' "$DEV/api/migration/read"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' -d '{"margin":8,"labels":true}' "$DEV/api/migration/apply"
curl -sk -u "$AUTH" -H 'Content-Type: application/json' -d '{}' "$DEV/api/migration/release"
```

`read` fetches the dongle's configuration twice (`POST http://<host>/api cmd=get config`) and
returns serial prefix, learn mode, counter, channel names and `master_key_found` — never the key.
`apply` adopts the last read (identity, counter + `margin`, optionally the names as labels) and
locks transmission. `GET /api/migration` shows the lock, whether the dongle still answers and its
latest counter (polled every 15 s; the device's counter follows it). `release` ends the lock and is
refused while the dongle answers unless `"force": true`. See `docs/migration.md`.

### HTTPS certificate

```sh
curl -sk -u "$AUTH" "$DEV/api/tls"
jq -n --rawfile c tls.crt --rawfile k tls.key '{certificate:$c, private_key:$k}' |
  curl -sk -u "$AUTH" -H 'Content-Type: application/json' --data @- "$DEV/api/tls"
```

`GET` shows `subject`, `issuer`, `not_after` and `self_signed`; the private key is never returned.
`POST` takes the PEM chain (leaf first) and the PEM private key (EC or RSA). The pair is rejected
if either does not parse or the key does not belong to the certificate; an accepted pair is stored
and used for new connections without a restart.

### Log and restart

```sh
curl -sk -u "$AUTH" "$DEV/log?after=0"
curl -sk -u "$AUTH" -X POST -H 'Content-Type: application/json' -d '{}' "$DEV/api/restart"
```

`/log` returns the buffered lines newer than `after`; the header `X-Log-Last` carries the number
to pass next time. A restart keeps settings, counters, positions and the certificate and sends
nothing.
