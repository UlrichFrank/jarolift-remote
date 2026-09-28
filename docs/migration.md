# Migration from the ESP8266 dongle

Moves the Jarolift crypto identity and rolling counter from the old dongle (madmartin/Jarolift_MQTT,
fork `UlrichFrank/Jarolift_MQTT`, at `192.0.2.20`) to this device, so that no motor has to be taught
again, and switches the consumers in `../home-new` to the new topics.

**The one rule:** the old dongle and this device must never both transmit. Both use one rolling
counter for all channels; the motors accept only rising values. The takeover enforces this: after
it is applied, this device sends nothing until the dongle no longer answers and you finish it.

State on 2026-09-28: dongle online at `192.0.2.20`, new learn mode, counter about 27000 (it rises
with every command the dongle sends). The retained `tele/jarolift/LWT = Offline` on the broker is
stale.

## 1. Take over the identity

Web interface → Einstellungen → **Übernahme vom alten Dongle**:

1. Enter the dongle's address and click **Dongle auslesen**. The device reads the configuration
   twice; if a command came in between it says so — read again.
2. Check the preview: prefix, learn mode, counter, channel names, "Master-Key gefunden".
3. Keep the margin at 8 and "Kanalnamen als Bezeichnungen übernehmen" ticked, click
   **Übernehmen** and confirm.

From now on the device does not transmit (banner on every page, "transmission locked" in the log).
It polls the dongle every 15 s and keeps its own counter at the dongle's counter + 8, so the dongle
may keep running until the switch-over. The lock survives restarts and updates.

## 2. Record it outside the device

1. Put master key, serial prefix and learn mode into the secrets and encrypt them. The key is never
   shown by the device; read it once with the tool (writes a mode-600 file, prints it masked):

   ```sh
   PY="$(head -1 "$(which esphome)" | cut -c3-)"
   $PY tools/read_dongle.py --dongle 192.0.2.20 --out dongle-record.json
   sops secrets.enc.yaml        # jarolift_master_msb, jarolift_master_lsb,
                                # jarolift_serial_prefix, jarolift_learn_mode_new
   ```

   The tool also cross-checks the dongle's MQTT `sendconfig` answer. Delete the plain record after.
2. Offline record (paper or password manager): date, the dongle's last counter, the device's
   counter after step 3.

## 3. Switch the dongle off and finish

1. **Power off the dongle permanently.** Unplug it and label it; it must not be powered again with
   the same identity (see Rollback).
2. Within 15 s the takeover section shows the dongle as "nicht mehr erreichbar". Click
   **Übernahme abschließen**. The device now transmits, starting at the tracked counter.
   ("Trotzdem freigeben …" is only for a dongle that is certainly off but whose address changed.)

## 4. Verify one shutter, then one group

1. Pick one shutter you can watch, e.g. `kueche` (Kanal 1). Send ▲, then ▼ from its card. The motor
   must move both times.
2. If it does not move: do not try others. Check the Log page for the telegram and counter, then go
   to Rollback.
3. Send ▼ on the group **Alle**. This is one telegram with the bitmask of channels 1–9 (dewenni's
   method). All nine shutters must close. If they do not, the motors do not accept bitmask group
   telegrams: keep using the old group channels (Kanal 10–14, still taught in the motors) as
   shutters named like the groups, and report it (design Decision 4).
4. Add the device's counter to the offline record.

## 5. Old group channels

The dongle's group channels (Kanal 10–14: Alle, EG, OG, OG Eltern, OG Kinder) are still taught in
the motors. With bitmask groups they are unused. Before one of them is reused for a single shutter,
activate it and click **Abmelden** — every motor that knows it forgets it — then teach the new motor.

## 6. Switch the consumers

In `../home-new`, in one rollout together with the device:

- `charts/mqtt/homekit/values/values.yaml`: the nine window coverings to
  `jarolift/<name>/position`, `/state`, `/set`; remove `factor: -1, offset: 100`.
- `mqtt-rules`: schedules to `jarolift/alle/set`; retire `shutterState`; update `cover.Cover`.
- `charts/observability/mqtt-logger`: allowlist `jarolift/#` instead of the legacy wildcards.
- `docs/topic-tree.md` §4b and `docs/device-inventory.md`.
- Certificate job and LAN DNS record for `jarolift.example.com` (task 10.4).

`mqtt-alerting` needs no change; its `bridge-offline` rule already matches `jarolift/bridge/state`.

## 7. Calibrate

For every shutter, time a full open and a full close and enter both in Einstellungen → Rollläden.
Check with a move to 50 % that it stops visibly at half height.

## Rollback

- **Before step 3.1** (dongle still on): nothing has been sent by the device. Keep using the dongle;
  the device can stay locked or be reset.
- **After step 3.1:** the device has advanced the counter. Before the dongle transmits again, set
  its device counter above the device's counter (dongle web UI → System → device counter, "save new
  device counter"), power the device off, and revert the consumers in `home-new`.
