Run `python3 scripts/tests/bluetooth_discovery/test_discovery.py` from the repository.
The tests compile checked-out C session, Settings and rendering functions with the
host compiler. Real subprocesses exercise owned acknowledgement, child liveness,
start rejection, missing acknowledgement, early termination, BlueZ state/query
failure, rescan deduplication and stopping only the owned client. Completion tests
accelerate the monotonic clock; they verify the 60-second command and deadline
logic without claiming an actual 60-second hardware trial. A separate test uses
the production command executor and a hung property query to verify bounded exit.
A compiled C child uses ordinary buffered `printf` without `fflush` or buffering
overrides, then stays alive. This reproduces noninteractive BlueZ stdio behavior,
verifies active scanning beyond five actual seconds, and rejects both failure
output and a live unacknowledged child even when adapter discovery is already on.
Shell printf mocks alone do not reproduce this buffering behavior.

Settings tests distinguish cached refreshes from radio sessions, exercise one
automatic scan plus explicit retries, retain cached results, guard overlapping
requests, recover scanner creation failure, queue a scan after enabling, stop on
page exit and prioritize pairing errors. Rendering tests route routine scan
progress and completion to the existing Network-style status area below the list,
preserving help for the selected row. Pairing and scan errors, boot-setting errors,
and long no-device guidance use the wrapped description area; tests also verify
that normal placement returns after an error. Other pages retain their existing
status rendering. No screenshot or hardware scan was validated by these host tests.

Discovery is active only after this child's `Discovery started` reply and a live
BlueZ `Discovering: yes` observation. In BlueZ 5.78, `rl_init` returns before
`setlinebuf(stdout)` in noninteractive mode, and `bt_shell_printf` uses `vprintf`
without flushing (see [upstream shell.c](https://github.com/bluez/bluez/blob/5.78/src/shared/shell.c)).
Both platform bundles ship that version. An owned pseudo-terminal on stdout and
stderr makes libc flush replies line by line while stdin stays `/dev/null` and
the command remains noninteractive. This needs no external stdbuf tool or libutil.
The parser accepts terminal CRLF but still requires an exact acknowledgement.
Property checks run about once per second with a two-second command cap. Cancellation drains only
the owned process group and may wait for that short probe plus a polling interval;
page exit does not wait for cached inventory queries. Starting another scan never
purges BlueZ entries or the discovered-device cache.
