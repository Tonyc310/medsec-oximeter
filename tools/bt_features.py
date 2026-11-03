#!/usr/bin/env python3
"""Check the oximeter's Bluetooth features against the ones it uses.

Reads a build's .config and fails, naming each, for a listed option not in the state the list
gives, and for an optional controller feature that's on but not listed. A device that hasn't
paired reaches every feature that's on, so each one on is there for the reason the list gives.
"""

import argparse
import re
import sys
from pathlib import Path

# The options a device's traffic can reach before it pairs: why the oximeter needs each one on, or
# None for one that must be off.
FEATURES: dict[str, str | None] = {
    "BT_PERIPHERAL": "the gateway connects to the oximeter",
    "BT_BROADCASTER": "the oximeter advertises, for the gateway to find it",
    "BT_CENTRAL": None,
    "BT_OBSERVER": None,
    "BT_BONDABLE": "the oximeter keeps its gateway's bond (C-02)",
    "BT_SMP_SC_ONLY": "pairing is LE Secure Connections only (C-01)",
    "BT_SMP_ENFORCE_MITM": "pairing is authenticated (C-01)",
    "BT_PRIVACY": "the oximeter advertises under a private address (C-25)",
    "BT_PHY_UPDATE": "the gateway moves the link to 2M PHY, halving each packet's time on air",
    "BT_GATT_SERVICE_CHANGED": "a bonded gateway learns when an update changes the services",
    "BT_L2CAP_DYNAMIC_CHANNEL": None,
    "BT_EATT": None,
    "BT_SIGNING": None,
    "BT_GATT_CACHING": None,
    "BT_GATT_READ_MULTIPLE": None,
    "BT_GATT_READ_MULT_VAR_LEN": None,
    "BT_GATT_DYNAMIC_DB": None,
    "BT_DEVICE_NAME_GATT_WRITABLE": None,
    "BT_EXT_ADV": None,
    "BT_CTLR_LE_ENC": "the link is encrypted (C-01)",
    "BT_CTLR_PRIVACY": "the controller resolves private addresses (C-25)",
    "BT_CTLR_DATA_LENGTH": "a signed command, or a piece of an update, travels in one packet",
    "BT_CTLR_PHY_2M": "the gateway moves the link to 2M PHY, halving each packet's time on air",
    "BT_CTLR_EXT_REJ_IND": "PHY update needs it",
    "BT_CTLR_CHAN_SEL_2": "the channel hopping both ends choose, with no procedure of its own",
    "BT_CTLR_CONN_PARAM_REQ": None,
    "BT_CTLR_LE_PING": None,
    "BT_CTLR_PER_INIT_FEAT_XCHG": None,
    "BT_CTLR_MIN_USED_CHAN": None,
}

# Options with a _SUPPORT twin that nothing over the air reaches: how the controller is built.
INTERNAL = {
    "BT_CTLR_ASSERT_OPTIMIZE_FOR_SIZE",
    "BT_CTLR_CRYPTO",
    "BT_CTLR_ENTROPY",
    "BT_CTLR_XTAL_ADVANCED",
}


def problems(config: str) -> list[str]:
    """Return what's wrong with the options in `config`, the text of a .config, naming each."""
    on = set(re.findall(r"^CONFIG_(\w+)=y$", config, re.MULTILINE))
    found = []
    for name, reason in FEATURES.items():
        if reason is None and name in on:
            found.append(f"CONFIG_{name} is on, and the oximeter doesn't use it")
        elif reason is not None and name not in on:
            found.append(f"CONFIG_{name} is off: {reason}")
    # Zephyr's controller gives each optional feature a _SUPPORT twin, so a new one shows here.
    for support in sorted(on):
        name = support.removesuffix("_SUPPORT")
        if (
            support.startswith("BT_CTLR_")
            and name != support
            and name in on
            and name not in FEATURES
            and name not in INTERNAL
        ):
            found.append(f"CONFIG_{name} is on, but isn't on the list")
    return found


def main() -> None:
    """Check the .config of the oximeter's build."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("config", type=Path, help="the build's .config")
    args = parser.parse_args()

    found = problems(args.config.read_text())
    for problem in found:
        print(problem)
    if found:
        sys.exit(1)
    print("every Bluetooth feature on is one the oximeter uses")


if __name__ == "__main__":
    main()
