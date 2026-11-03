#!/usr/bin/env python3
"""Write the Bluetooth host fuzzer's first cases: each command a central can send before pairing.

Each case is a series of packets, each a channel byte, a length byte and then its bytes, as
fuzz/bt_host/src/main.c reads them; the harness adds each frame's L2CAP header. There's a case for
each command on the three channels a central reaches before pairing, ATT, LE signalling and SMP,
with the parameters it takes, so libFuzzer starts inside every handler rather than having to guess
an opcode and its length together.
"""

import argparse
import struct
from pathlib import Path

ATT, SIGNALLING, SMP = 0x04, 0x05, 0x06
# Packets the harness sends as they are: the start of a frame, header and all, or the rest of one.
RAW_START, RAW_REST = 0xFE, 0xFF
WHOLE_RANGE = struct.pack("<HH", 0x0001, 0xFFFF)
PRIMARY_SERVICE, CHARACTERISTIC, PULSE_OXIMETER = 0x2800, 0x2803, 0x1822

# ATT requests and commands a server receives, by name: the opcode and its parameters.
ATT_PDUS = {
    "mtu": (0x02, struct.pack("<H", 247)),
    "find-information": (0x04, WHOLE_RANGE),
    "find-by-type-value": (0x06, WHOLE_RANGE + struct.pack("<HH", PRIMARY_SERVICE, PULSE_OXIMETER)),
    "read-by-type": (0x08, WHOLE_RANGE + struct.pack("<H", CHARACTERISTIC)),
    "read": (0x0A, struct.pack("<H", 0x0003)),
    "read-blob": (0x0C, struct.pack("<HH", 0x0003, 4)),
    "read-multiple": (0x0E, struct.pack("<HH", 0x0003, 0x0005)),
    "read-by-group-type": (0x10, WHOLE_RANGE + struct.pack("<H", PRIMARY_SERVICE)),
    "write": (0x12, struct.pack("<H", 0x0010) + bytes(range(20))),
    "prepare-write": (0x16, struct.pack("<HH", 0x0010, 0) + bytes(8)),
    "execute-write": (0x18, b"\x01"),
    "confirmation": (0x1E, b""),
    "read-multiple-variable": (0x20, struct.pack("<HH", 0x0003, 0x0005)),
    "write-command": (0x52, struct.pack("<H", 0x0011) + b"\x01\x00"),
    "signed-write": (0xD2, struct.pack("<H", 0x0010) + bytes(16)),
}

# SMP commands, by name: the code and parameters of the length SMP requires.
PAIRING = bytes([0x03, 0x01, 0x0D, 16, 0x07, 0x07])
SMP_PDUS = {
    "pairing-request": (0x01, PAIRING),
    "pairing-response": (0x02, PAIRING),
    "pairing-confirm": (0x03, bytes(16)),
    "pairing-random": (0x04, bytes(16)),
    "pairing-failed": (0x05, b"\x05"),
    "encryption-information": (0x06, bytes(16)),
    "central-identification": (0x07, bytes(10)),
    "identity-information": (0x08, bytes(16)),
    "identity-address": (0x09, bytes(7)),
    "signing-information": (0x0A, bytes(16)),
    "security-request": (0x0B, b"\x0d"),
    "public-key": (0x0C, bytes(64)),
    "dhkey-check": (0x0D, bytes(16)),
    "keypress": (0x0E, b"\x00"),
}

# LE signalling commands, by name: the code and its data.
CHANNEL = struct.pack("<H", 0x0040)
SIGNALLING_PDUS = {
    "command-reject": (0x01, struct.pack("<H", 0)),
    "disconnection-request": (0x06, CHANNEL + CHANNEL),
    "disconnection-response": (0x07, CHANNEL + CHANNEL),
    "parameter-update-request": (0x12, struct.pack("<HHHH", 24, 40, 0, 42)),
    "parameter-update-response": (0x13, struct.pack("<H", 0)),
    "credit-connection-request": (0x14, struct.pack("<HHHHH", 0x0080, 0x0040, 247, 247, 1)),
    "credit-connection-response": (0x15, struct.pack("<HHHHH", 0x0040, 247, 247, 1, 0)),
    "credits": (0x16, CHANNEL + struct.pack("<H", 1)),
    "enhanced-connection-request": (0x17, struct.pack("<HHHH", 0x0080, 247, 247, 1) + CHANNEL),
    "enhanced-connection-response": (0x18, struct.pack("<HHHH", 247, 247, 1, 0) + CHANNEL),
    "reconfigure-request": (0x19, struct.pack("<HH", 247, 247) + CHANNEL),
    "reconfigure-response": (0x1A, struct.pack("<H", 0)),
}


def packet(channel: int, payload: bytes) -> bytes:
    """Send a payload on a channel."""
    return bytes([channel, len(payload)]) + payload


def att(opcode: int, parameters: bytes) -> bytes:
    """Send an ATT PDU."""
    return packet(ATT, bytes([opcode]) + parameters)


def signalling(code: int, data: bytes) -> bytes:
    """Send an LE signalling command, with identifier 1."""
    return packet(SIGNALLING, struct.pack("<BBH", code, 1, len(data)) + data)


WRITE = bytes([0x12]) + ATT_PDUS["write"][1]
WRITE_FRAME = struct.pack("<HH", len(WRITE), ATT) + WRITE

CASES = {
    # Service discovery, as a gateway does on every link: the MTU, the services, their
    # characteristics and their descriptors.
    "discovery": b"".join(
        att(*ATT_PDUS[name])
        for name in ("mtu", "read-by-group-type", "read-by-type", "find-information")
    ),
    # A write in two packets: the start of the frame, with its header, then the rest.
    "fragments": packet(RAW_START, WRITE_FRAME[:10]) + packet(RAW_REST, WRITE_FRAME[10:]),
}
CASES |= {f"att-{name}": att(*pdu) for name, pdu in ATT_PDUS.items()}
CASES |= {
    f"smp-{name}": packet(SMP, bytes([code]) + data) for name, (code, data) in SMP_PDUS.items()
}
CASES |= {f"signalling-{name}": signalling(*pdu) for name, pdu in SIGNALLING_PDUS.items()}


def main() -> None:
    """Write each case to a file of its own."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path, help="where to write the cases")
    args = parser.parse_args()

    args.directory.mkdir(parents=True, exist_ok=True)
    for name, case in CASES.items():
        (args.directory / name).write_bytes(case)


if __name__ == "__main__":
    main()
