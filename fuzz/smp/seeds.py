#!/usr/bin/env python3
"""Write the SMP fuzzer's first cases: requests a gateway really sends, as ATT writes.

Each case is a series of writes, each a length byte and then its bytes, as fuzz/smp/src/main.c
reads them. libFuzzer mutates these into the rest; without them it rarely gets a header and a CBOR
map right together. The confirm that standard update tools send once an update runs is among them,
so every run tries what the update guard is there to refuse.
"""

import argparse
import struct
from pathlib import Path

ATT_WRITE_MAX = 244  # the oximeter's ATT MTU (247), less a write's 3-byte header
IMAGE_MAGIC = 0x96F3B83D

READ, WRITE = 0, 2
OS_GROUP, IMAGE_GROUP = 0, 1
RESET = 5  # in the OS group
STATE, UPLOAD = 0, 1  # in the image group


def text(value: str) -> bytes:
    """Encode a CBOR text string of fewer than 24 bytes."""
    encoded = value.encode()
    return bytes([0x60 + len(encoded)]) + encoded


def data(value: bytes) -> bytes:
    """Encode a CBOR byte string of fewer than 256 bytes."""
    return bytes([0x58, len(value)]) + value


def request(op: int, group: int, command: int, payload: bytes) -> bytes:
    """Frame a CBOR payload with an SMP version 2 header."""
    # Op, flags (bits 3-4: version 2), payload length, group, sequence number, command.
    return struct.pack(">BBHHBB", op, 0x08, len(payload), group, 0, command) + payload


def writes(packet: bytes) -> bytes:
    """Split a request into ATT writes, each with its length first."""
    chunks = (packet[at : at + ATT_WRITE_MAX] for at in range(0, len(packet), ATT_WRITE_MAX))
    return b"".join(bytes([len(chunk)]) + chunk for chunk in chunks)


# The first piece of an upload starts with the image's header, so it starts with the magic number.
FIRST_PIECE = struct.pack("<I", IMAGE_MAGIC) + bytes(28)

CASES = {
    "state-read": request(READ, IMAGE_GROUP, STATE, b"\xa0"),
    # Marks the update in slot 1, named by its hash, for a test boot.
    "mark-for-test": request(
        WRITE,
        IMAGE_GROUP,
        STATE,
        b"\xa2" + text("hash") + data(bytes(32)) + text("confirm") + b"\xf4",
    ),
    # Keeps the running image for good.
    "confirm": request(WRITE, IMAGE_GROUP, STATE, b"\xa1" + text("confirm") + b"\xf5"),
    "upload": request(
        WRITE,
        IMAGE_GROUP,
        UPLOAD,
        b"\xa3"
        + text("off")
        + b"\x00"
        + text("data")
        + data(FIRST_PIECE)
        + text("len")
        + b"\x19\x03\xe8",
    ),
    "reset": request(WRITE, OS_GROUP, RESET, b"\xa0"),
}


def main() -> None:
    """Write each case to a file of its own."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path, help="where to write the cases")
    args = parser.parse_args()

    args.directory.mkdir(parents=True, exist_ok=True)
    for name, packet in CASES.items():
        (args.directory / name).write_bytes(writes(packet))


if __name__ == "__main__":
    main()
