#!/usr/bin/env python3
"""Encodes rcl-golden-v1.json into rcl-golden-v1.hex, by docs/protocol.md.

A third implementation of the format, in nothing but struct and zlib, written from the document
and not from lib/rcl, so that the golden bytes are not just what the C++ happens to produce. Run it
from anywhere; it rewrites the .hex beside it:

    python3 -I docs/fixtures/make_rcl_golden.py
"""

import json
import pathlib
import struct
import zlib

HERE = pathlib.Path(__file__).resolve().parent
MAGIC = bytes([0x89, 0x52, 0x43, 0x4C, 0x0D, 0x0A, 0x1A, 0x0A])
SYNC = 0xA5
TYPES = {
    "session_header": 0x01,
    "ubx": 0x02,
    "imu_aggregate": 0x03,
    "imu_raw": 0x04,
    "rc_samples": 0x05,
    "marker": 0x06,
    "lap": 0x07,
    "footer": 0x08,
}


def payload(r):
    t = r["type"]
    if t == "session_header":
        device = bytes(int(x, 16) for x in r["device"].split(":"))
        firmware = r["firmware"].encode("ascii").ljust(16, b"\0")
        return struct.pack(
            "<I6sH16sHHHBBHBB",
            r["session_id"], device, r["post"], firmware, r["epoch_ms"], r["rc_period_us"],
            r["imu_raw_period_us"], r["accel_range_g"], 0, r["gyro_range_dps"], r["car_profile"], 0)
    if t == "ubx":
        return bytes.fromhex(r["frame"])
    if t == "imu_aggregate":
        return struct.pack("<H18h", r["count"], *[v for axis in r["axes"] for v in axis])
    if t == "imu_raw":
        s = r["samples"]
        return struct.pack("<BB", len(s), 0) + b"".join(struct.pack("<6h", *x) for x in s)
    if t == "rc_samples":
        s = r["samples"]
        return struct.pack("<BB", len(s), 0) + b"".join(struct.pack("<3H", *x) for x in s)
    if t == "marker":
        return struct.pack("<HBB", r["number"], r["source"], 0)
    if t == "lap":
        return struct.pack("<HBBIII", r["lap"], r["flags"], r["line"], r["crossing_ms"],
                           r["crossing_itow_ms"], r["lap_time_ms"])
    if t == "footer":
        return struct.pack(
            "<B3x16I", r["close"], r["ubx_frames"], r["records"], r["uart_overruns"],
            r["uart_errors"], r["ubx_bad_checksums"], r["ubx_oversize"], r["gnss_queue_full"],
            r["ring_overflow"], *r["rc_rejects"], r["writes"], r["slow_writes"],
            r["write_max_ms"], r["write_p99_ms"], r["ring_high_water"])
    raise ValueError(t)


def main():
    golden = json.loads((HERE / "rcl-golden-v1.json").read_text())
    nonce = struct.pack("<Q", int(golden["nonce"], 16))
    head = MAGIC + struct.pack("<HH", golden["version"], 24) + nonce
    blocks = [("file header: version %d, nonce %s" % (golden["version"], golden["nonce"]),
               head + struct.pack("<I", zlib.crc32(head)))]
    seed = zlib.crc32(nonce)
    for i, r in enumerate(golden["records"], 1):
        p = payload(r)
        body = struct.pack("<BHI", TYPES[r["type"]], len(p), r["t_ms"]) + p
        record = bytes([SYNC]) + body + struct.pack("<I", zlib.crc32(body, seed))
        blocks.append(("record %d: %s, t %d ms, %d payload bytes" % (i, r["type"], r["t_ms"], len(p)),
                       record))

    lines = [
        "# The golden .rcl of docs/protocol.md, version 1: rcl-golden-v1.json encoded by",
        "# make_rcl_golden.py. Do not edit by hand; edit the JSON and run the script.",
    ]
    total = 0
    for title, data in blocks:
        lines.append("# " + title)
        for k in range(0, len(data), 32):
            lines.append(" ".join("%02x" % b for b in data[k:k + 32]))
        total += len(data)
    lines.append("# %d bytes" % total)
    (HERE / "rcl-golden-v1.hex").write_text("\n".join(lines) + "\n")
    print("rcl-golden-v1.hex: %d bytes, %d records" % (total, len(golden["records"])))


if __name__ == "__main__":
    main()
