"""Tests for bt_features.py, with build configurations made up for each test."""

import bt_features

# What the check allows: every feature on the list that must be on, as Kconfig writes a .config.
ALLOWED = [name for name, reason in bt_features.FEATURES.items() if reason is not None]


def configuration(*on: str) -> str:
    """Return a .config with each option in `on` set, and the rest off as Kconfig records them."""
    lines = [f"CONFIG_{name}=y" for name in on]
    lines += [f"# CONFIG_{name} is not set" for name in bt_features.FEATURES if name not in on]
    return "\n".join(lines) + "\n"


def test_passes_the_features_the_oximeter_uses() -> None:
    # As in a real build: a listed feature's _SUPPORT twin, and an internal option with one.
    twins = ["BT_CTLR_LE_ENC_SUPPORT", "BT_CTLR_ENTROPY", "BT_CTLR_ENTROPY_SUPPORT"]

    assert bt_features.problems(configuration(*ALLOWED, *twins)) == []


def test_names_a_feature_that_must_be_off() -> None:
    found = bt_features.problems(configuration(*ALLOWED, "BT_CTLR_LE_PING"))

    assert found == ["CONFIG_BT_CTLR_LE_PING is on, and the oximeter doesn't use it"]


def test_names_a_feature_that_must_be_on() -> None:
    on = [name for name in ALLOWED if name != "BT_SMP_SC_ONLY"]

    assert bt_features.problems(configuration(*on)) == [
        f"CONFIG_BT_SMP_SC_ONLY is off: {bt_features.FEATURES['BT_SMP_SC_ONLY']}"
    ]


def test_names_an_optional_controller_feature_not_on_the_list() -> None:
    found = bt_features.problems(
        configuration(*ALLOWED, "BT_CTLR_SCA_UPDATE", "BT_CTLR_SCA_UPDATE_SUPPORT")
    )

    assert found == ["CONFIG_BT_CTLR_SCA_UPDATE is on, but isn't on the list"]
