#!/usr/bin/env python3
"""Check no secret key is built into the firmware images.

Searches each image for the backend's masters, the release key's private half, and the keys
provisioned into one oximeter, and fails, naming them, for any it finds. MCUboot holds the release
key's public half by design; the hub isn't checked, since it stands in for the backend and holds
the masters.
"""

import argparse
import base64
import sys
from pathlib import Path

import provision

# In provision.device_keys' order.
DEVICE_KEYS = ("limits key", "log key", "pairing key")
# RFC 5915's ECPrivateKey, as PKCS#8 also wraps it: version 1, then the private key as a 32-byte
# OCTET STRING.
EC_PRIVATE_KEY = bytes.fromhex("020101" + "0420")


def read_master(path: Path) -> bytes:
    """Return the master the hex file at `path` holds."""
    return provision.read_hex(path.read_text(), provision.KEY_SIZE, str(path))


def read_private_key(pem: Path) -> bytes:
    """Return the private key of the P-256 key in `pem`, or exit if it holds none."""
    lines = pem.read_text().splitlines()
    der = base64.b64decode("".join(line for line in lines if not line.startswith("-----")))
    start = der.find(EC_PRIVATE_KEY)
    if start < 0:
        sys.exit(f"{pem} holds no EC private key")
    start += len(EC_PRIVATE_KEY)
    return der[start : start + provision.KEY_SIZE]


def keys(
    signing_master: Path, pairing_master: Path, release_key: Path, device_id: bytes
) -> dict[str, bytes]:
    """Return each key no image may hold, by name."""
    signing, pairing = read_master(signing_master), read_master(pairing_master)
    found = {
        "signing master": signing,
        "pairing master": pairing,
        "release key": read_private_key(release_key),
    }
    device = provision.device_keys(signing, pairing, device_id)
    for index, name in enumerate(DEVICE_KEYS):
        found[name] = device[index * provision.KEY_SIZE : (index + 1) * provision.KEY_SIZE]
    return found


def found(images: list[Path], keys: dict[str, bytes]) -> list[str]:
    """Return '<key> in <image>' for each key an image holds."""
    return [
        f"{name} in {image}"
        for image in images
        for name, key in keys.items()
        if key in image.read_bytes()
    ]


def main() -> None:
    """Search the images for the keys."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--signing-master", type=Path, required=True, help="file holding the signing master, hex"
    )
    parser.add_argument(
        "--pairing-master", type=Path, required=True, help="file holding the pairing master, hex"
    )
    parser.add_argument(
        "--release-key", type=Path, required=True, help="the release key, as imgtool made it"
    )
    parser.add_argument(
        "--device-id", required=True, help="a provisioned device's ID, 16 hex digits"
    )
    parser.add_argument("images", type=Path, nargs="+", help="the images, as raw binaries")
    args = parser.parse_args()

    device_id = provision.read_hex(args.device_id, provision.DEVICE_ID_SIZE, "the device ID")
    held = found(
        args.images, keys(args.signing_master, args.pairing_master, args.release_key, device_id)
    )
    for key in held:
        print(f"built in: {key}")
    if held:
        sys.exit(1)
    print("no key is built into the images")


if __name__ == "__main__":
    main()
