# Applications

Not scaffolded yet — each arrives with its own planning document, as PLAN.md sets out.

| App | Platform | Role |
| --- | --- | --- |
| `mobile` | Flutter, iOS + Android | downloads, configuration and lap review over BLE, between runs: the device's BLE is on only in Transfer mode, so there is no live view. Renames car profiles and tracks |
| `desktop` | Flutter, macOS | analysis workbench over logs pulled off the card via USB MSC. Renames car profiles and tracks, and refines track data (PLAN-ESP32.md §7.5) |

Both consume `packages/rct_core`, so neither re-implements the log format or the lap math. The BLE
link is **GATT only** — the ESP32-S3 has no Bluetooth Classic, so there is no serial-port profile to
fall back on — and bulk transfer is deliberately not BLE's job: an hour-long session is several
minutes of notifications versus seconds over USB.
