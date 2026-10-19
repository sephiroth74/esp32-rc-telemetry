# Resolves a wildcard upload_port to the board's port before the upload.
#
# platformio.ini limits uploads to the board's own ports with a pattern, but the 1200-baud touch of
# pioarduino 55.03.311 opens the pattern itself and fails. Resolved here, the touch reaches the
# TinyUSB port and the new bootloader port is found; from a tool running on USB-Serial-JTAG, where
# no new port appears, the upload falls back to the same one. With nothing matching, the pattern
# stays and the upload fails cleanly instead of touching another device.
Import("env")  # noqa: F821 — provided by SCons

import glob

port = env.subst("$UPLOAD_PORT")  # noqa: F821
if any(c in port for c in "*?["):
    matches = sorted(glob.glob(port))
    if matches:
        env.Replace(UPLOAD_PORT=matches[0])  # noqa: F821
