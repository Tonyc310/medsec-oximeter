"""Tests for find_keys.py, with keys and images made up for each test."""

import base64
from pathlib import Path

import find_keys
import provision
import pytest

SIGNING_MASTER = bytes(range(0x00, 0x20))
PAIRING_MASTER = bytes(range(0x20, 0x40))
RELEASE_KEY = bytes(range(0x40, 0x60))
DEVICE_ID = bytes.fromhex("0123456789abcdef")
DEVICE_KEYS = provision.device_keys(SIGNING_MASTER, PAIRING_MASTER, DEVICE_ID)
# Each key the check must find, by the name it gives.
SECRETS = {
    "signing master": SIGNING_MASTER,
    "pairing master": PAIRING_MASTER,
    "release key": RELEASE_KEY,
    "limits key": DEVICE_KEYS[0:32],
    "log key": DEVICE_KEYS[32:64],
    "pairing key": DEVICE_KEYS[64:96],
}


def release_key_pem(private_key: bytes) -> str:
    """Return `private_key` as an RFC 5915 EC private key in PEM, without the optional fields."""
    der = base64.b64encode(bytes.fromhex("30250201010420") + private_key).decode()
    return f"-----BEGIN EC PRIVATE KEY-----\n{der}\n-----END EC PRIVATE KEY-----\n"


def keys(tmp_path: Path) -> dict[str, bytes]:
    """Write the key files, and return what find_keys reads from them."""
    (tmp_path / "signing.key").write_text(SIGNING_MASTER.hex() + "\n")
    (tmp_path / "pairing.key").write_text(PAIRING_MASTER.hex() + "\n")
    (tmp_path / "release.pem").write_text(release_key_pem(RELEASE_KEY))
    return find_keys.keys(
        tmp_path / "signing.key", tmp_path / "pairing.key", tmp_path / "release.pem", DEVICE_ID
    )


def image(tmp_path: Path, contents: bytes) -> Path:
    """Write an image with `contents` between code-like bytes, and return its path."""
    (tmp_path / "zephyr.bin").write_bytes(b"\x00\xbf" * 64 + contents + b"\x70\x47" * 64)
    return tmp_path / "zephyr.bin"


def test_passes_an_image_holding_no_key(tmp_path: Path) -> None:
    assert find_keys.found([image(tmp_path, b"medsec-oximeter")], keys(tmp_path)) == []


@pytest.mark.parametrize("name", SECRETS)
def test_names_each_key_planted_in_an_image(tmp_path: Path, name: str) -> None:
    planted = image(tmp_path, SECRETS[name])

    assert find_keys.found([planted], keys(tmp_path)) == [f"{name} in {planted}"]
