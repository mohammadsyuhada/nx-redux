# Bluetooth reconnect tests

From the repository root, with Python 3, a host C compiler, and standard shell
tools available, run:

```sh
python3 -m unittest discover -s scripts/tests/bluetooth_reconnect -p 'test_nx_*.py' -v
```

The tests read the Bluetooth source and helper in this checkout. They use
temporary files and stubbed Bluetooth commands; they do not contact a device.
The shutdown-guard check compares the init script with the upstream parent
commit `af1b0fbbb1fa0489875daad5c05f4980e3602b4d`, so run in a clone with
that history. The two `check-device-*.sh` scripts are optional read-only device
preflights and are not needed for the host suite.
