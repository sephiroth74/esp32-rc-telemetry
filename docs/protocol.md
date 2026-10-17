# Protocol — `.rcl` binary format and BLE GATT

**Single source of truth** for the on-disk log format and the BLE service. The firmware
(`firmware/lib/rcl/`) and the Dart core (`packages/rct_core/`) are two implementations of what this
file says, checked against the shared fixtures in [fixtures/](fixtures/).

**Status: not written yet.** Blocks the logging core (PLAN-ESP32.md phase 5) and the BLE server
(phase 9). Until it exists, `firmware/lib/rcl/rcl.h` holds only the file magic.

## `.rcl` records

To define: file header, then the record types listed in PLAN-ESP32.md §7.2 — session header, raw UBX
passthrough, IMU aggregate, IMU raw, RC sample, marker, lap event, session footer.

Constraints that come from the plan and are not negotiable here:

- **Records are self-delimiting**, so a file truncated by a power cut stays parseable (§4.5).
- **UBX payloads are stored verbatim**, all 92 bytes of NAV-PVT and every NAV-SAT block, so no field
  is lost to a curated subset (§5.3).
- **A generic raw-UBX record**, so a message added later needs no format change.
- Loss counters and write-latency statistics live in the session footer (§4.4, §5.3).

## BLE GATT

To define: service and characteristic UUIDs, the **status** notification payload, the **control**
command set, and the credit/ACK scheme for **download**. Roles are sketched in PLAN-ESP32.md §7.4.
There is no live stream: BLE runs only in Transfer mode.
