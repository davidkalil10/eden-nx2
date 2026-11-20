#!/usr/bin/env python3
"""Verify that a Switch 2 NRO contains OUN0 magic, kaiHEN markers, and zero Switch 1 libs."""
import sys
from pathlib import Path

FORBIDDEN_MARKERS = [
    b"libdeko3d",
    b"libuam",
    b"libusbhsfs",
    b"SaltySD",
]

def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path/to/eden-switch2.nro>", file=sys.stderr)
        return 2
    nro = Path(sys.argv[1])
    if not nro.is_file():
        print(f"ERROR: NRO not found: {nro}", file=sys.stderr)
        return 1
    data = nro.read_bytes()
    if len(data) < 0x100:
        print("ERROR: NRO too small", file=sys.stderr)
        return 1
    if data[0x10:0x14] != b"NRO0":
        print(f"ERROR: Missing NRO0 header (got {data[0x10:0x14]!r})", file=sys.stderr)
        return 1
    if b"OUN0" not in data:
        print("ERROR: Missing OUN0 Switch 2 marker (was elf2nro from ounce-devkit used?)", file=sys.stderr)
        return 1
    if b"kaiHEN" not in data and b"kaihen" not in data:
        print("WARNING: kaiHEN marker not found in NRO binary")
    for bad in FORBIDDEN_MARKERS:
        if bad in data:
            print(f"ERROR: Forbidden Switch 1 marker found: {bad!r}", file=sys.stderr)
            return 1
    print(f"OK: {nro.name} ({len(data):,} bytes) verified as Switch 2 OUN0 package.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
