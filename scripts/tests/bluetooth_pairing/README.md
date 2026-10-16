Run `python3 scripts/tests/bluetooth_pairing/test_pairing.py` from the repository.
The tests compile the checked-out C functions using the host C compiler and stub
BlueZ responses. The command test runs the real output-capturing executor: Pair,
Connect after Pair, and manual Connect each take six seconds, beyond the old
five-second query deadline. It also checks bounded termination and output capture.
Timeout has a distinct result from an ordinary command failure. The runner kills
its shell's process group even if the shell exits while a child keeps stdout open.
Timed-out Connect reports that BlueZ may still complete the request; it does not
issue an automatic Disconnect, which could race with a newly connected device.

Pairability tests cover initially enabled/disabled adapters, failed enable and
verification, and restoration after failed Pair. Pair success requires both
Paired and Bonded; missing Bonded fails closed rather than implying a saved bond.
The shipped platform BlueZ bundles are 5.78 (see `licenses/bluez.txt`). Inventory
retains existing controller actions without treating trust as proof of bonding.
These host tests prove the command and reporting paths, not daemon-request
cancellation, hardware pairing, or audio readiness.
