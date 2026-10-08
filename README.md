# medsec-oximeter

[![CI](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml/badge.svg)](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml)

A Bluetooth pulse oximeter built to the security expectations FDA sets for connected medical devices, together with the cybersecurity documentation a premarket submission needs: architecture views, threat model, risk assessment, security controls with test evidence, SBOM and a postmarket plan. The firmware runs on an nRF52840 under Zephyr and is tested in the Renode simulator.

> **This is a portfolio exercise, not a medical device.** It has not been cleared or approved by FDA or any other regulator. Don't use it on people or for any medical decision.

## Status

Phase 2 of 7, the baseline device. The firmware boots and logs; the sensor, SpO2 and BLE come next. The baseline is built without security controls on purpose, so the threat model starts from an honest "before" state.

## Build and run

Needs `git`, `cmake`, `ninja`, Python 3 with `venv`, `dtc` and `gperf`. From the repository root, once:

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install west
west init -l app
west update --narrow -o=--depth=1          # Zephyr v4.4.2 and its modules, into deps/
pip install -r deps/zephyr/scripts/requirements-base.txt
west sdk install -t arm-zephyr-eabi        # skip if a matching Zephyr SDK is installed
```

Then, in each new shell:

```bash
. .venv/bin/activate
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk app
renode renode/oximeter.resc                # type `start`; the console opens in its own window
```

## Test

The Robot Framework test boots the firmware in [Renode](https://renode.io) 1.17. It needs `renode-test` on `PATH` with its Python packages (`pip install -r <renode>/tests/requirements.txt`). It writes its report to the working directory, so run it from the build tree:

```bash
mkdir -p build/renode && cd build/renode && renode-test ../../renode/oximeter.robot
```

## License

MIT
