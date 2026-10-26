#!/usr/bin/env python3
"""Provision an oximeter's keys, as the factory would.

Derives the device's keys from the backend's masters and the device ID the way the backend does,
with HKDF-SHA256 (RFC 5869): the limits key and log key from the signing master, the pairing key
from the pairing master. Gives the UICR words to write: as a list, or as a Renode script that
writes them into the simulated oximeter.
"""

import argparse
import hashlib
import hmac
import sys
from pathlib import Path

# Must match hub/src/device_keys.c and app/src/device_identity.c: one label per key, and the keys
# in this order in UICR.
SIGNING_LABELS = (b"medsec limits key", b"medsec log key")
PAIRING_LABEL = b"medsec pairing key"
KEY_SIZE = 32
DEVICE_ID_SIZE = 8
UICR_CUSTOMER = 0x10001080


def hkdf_sha256(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    """HKDF-SHA256, RFC 5869: extract, then expand to `length` bytes."""
    prk = hmac.new(salt or bytes(hashlib.sha256().digest_size), ikm, hashlib.sha256).digest()
    okm = b""
    block = b""
    counter = 1
    while len(okm) < length:
        block = hmac.new(prk, block + info + bytes([counter]), hashlib.sha256).digest()
        okm += block
        counter += 1
    return okm[:length]


def device_keys(signing_master: bytes, pairing_master: bytes, device_id: bytes) -> bytes:
    """Return the keys of the oximeter with `device_id`, one after the other, as UICR holds them."""
    keys = [
        hkdf_sha256(signing_master, b"", label + device_id, KEY_SIZE) for label in SIGNING_LABELS
    ]
    keys.append(hkdf_sha256(pairing_master, b"", PAIRING_LABEL + device_id, KEY_SIZE))
    return b"".join(keys)


def uicr_words(keys: bytes) -> list[tuple[int, int]]:
    """Return the (address, value) pairs that put `keys` in UICR, as little-endian words."""
    return [
        (UICR_CUSTOMER + offset, int.from_bytes(keys[offset : offset + 4], "little"))
        for offset in range(0, len(keys), 4)
    ]


def read_hex(text: str, size: int, what: str) -> bytes:
    """Return the `size` bytes `text` holds in hex, or exit naming `what`."""
    try:
        value = bytes.fromhex(text.strip())
    except ValueError:
        sys.exit(f"{what} isn't hex")
    if len(value) != size:
        sys.exit(f"{what} must be {size} bytes")
    return value


def main() -> None:
    """Write the UICR words for the keys, as a list or as a Renode script."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--signing-master", type=Path, required=True, help="file holding the signing master, hex"
    )
    parser.add_argument(
        "--pairing-master", type=Path, required=True, help="file holding the pairing master, hex"
    )
    parser.add_argument("--device-id", required=True, help="the device's ID, 16 hex digits")
    parser.add_argument("--renode", type=Path, help="write a Renode script here instead of a list")
    args = parser.parse_args()

    signing_master = read_hex(args.signing_master.read_text(), KEY_SIZE, str(args.signing_master))
    pairing_master = read_hex(args.pairing_master.read_text(), KEY_SIZE, str(args.pairing_master))
    device_id = read_hex(args.device_id, DEVICE_ID_SIZE, "the device ID")
    words = uicr_words(device_keys(signing_master, pairing_master, device_id))

    if args.renode is None:
        for address, value in words:
            print(f"0x{address:08X} 0x{value:08X}")
        return
    lines = ['mach set "oximeter"']
    lines += [f"sysbus WriteDoubleWord 0x{address:08X} 0x{value:08X}" for address, value in words]
    args.renode.parent.mkdir(parents=True, exist_ok=True)
    args.renode.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
