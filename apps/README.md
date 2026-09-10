# Applications

Not scaffolded yet — each arrives with its own planning document, as PLAN.md sets out.

| App | Platform | Role |
| --- | --- | --- |
| `mobile` | Flutter, iOS + Android | live view over BLE while the car is running |
| `desktop` | Flutter, macOS | analysis workbench over logs pulled off the card via USB MSC |

Both consume `packages/rct_core`, so neither re-implements the log format or the lap math. The BLE
link is **GATT only** — the ESP32-S3 has no Bluetooth Classic, so there is no serial-port profile to
fall back on — and bulk transfer is deliberately not BLE's job: an hour-long session is several
minutes of notifications versus seconds over USB.
