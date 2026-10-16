"""Host regressions for tg5040 restart-only Bluetooth audio recall."""
import os
import pathlib
import shutil
import sys
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
HELPER = ROOT / 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_reconnect.sh'
INIT = ROOT / 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_init.sh'
AUDIO = 'AA:BB:CC:DD:EE:FF'
CONTROLLER = '11:22:33:44:55:66'


class ReconnectRegression(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.state = self.root / 'state'
        self.state.mkdir()
        (self.state / 'token').write_text('current\n')
        self.env = dict(os.environ, BT_RECONNECT_STATE=str(self.state), TEST_ROOT=str(self.root))

    def run_shell(self, body, script=HELPER, *args):
        return subprocess.run(['sh', '-c', '. "$1"; shift; bt_now() { printf "100\\n"; }; ' + body,
                               'test', str(script), *args],
                              env=self.env, check=True, capture_output=True, text=True, timeout=8)

    def test_capture_requires_one_saved_audio_sink_and_clears_old_arm(self):
        stub = r'''
BT_RECONNECT_TOKEN=current
bt_ctl() {
 case "$2 $3" in
  show*) printf 'Powered: yes\n';;
  'devices Connected') printf 'Device 11:22:33:44:55:66 Gamepad\nDevice AA:BB:CC:DD:EE:FF Headphones\n';;
  'info 11:22:33:44:55:66') printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\nUUID: Human Interface Device (00001124-0000-1000-8000-00805f9b34fb)\n';;
  'info AA:BB:CC:DD:EE:FF') printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_capture_audio
'''
        self.run_shell(stub)
        self.assertEqual((self.state / 'audio').read_text(), f'current {AUDIO}\n')
        (self.state / 'audio').write_text(f'old {AUDIO}\n')
        self.run_shell(stub.replace('Bonded: yes\\nTrusted: yes\\nUUID: Audio Sink',
                                    'Bonded: no\\nTrusted: yes\\nUUID: Audio Sink'))
        self.assertFalse((self.state / 'audio').exists())

    def test_false_property_and_multiple_sinks_do_not_arm(self):
        stub = r'''
BT_RECONNECT_TOKEN=current
bt_ctl() {
 case "$2 $3" in
  show*) printf 'Powered: yes\n';;
  'devices Connected') printf 'Device AA:BB:CC:DD:EE:FF First\nDevice 22:33:44:55:66:77 Second\n';;
  'info AA:BB:CC:DD:EE:FF') printf 'Name: Bonded: yes\nConnected: yes\nBonded: no\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
  'info 22:33:44:55:66:77') printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_capture_audio
'''
        self.run_shell(stub)
        self.assertEqual((self.state / 'audio').read_text(), 'current 22:33:44:55:66:77\n')
        self.run_shell(stub.replace('Bonded: no\\nTrusted: yes', 'Bonded: yes\\nTrusted: yes'))
        self.assertFalse((self.state / 'audio').exists())

    def test_previous_stop_arm_is_consumed_once_and_stale_arm_rejected(self):
        (self.state / 'audio').write_text(f'previous {AUDIO}\n')
        body = r'''
BT_RECONNECT_TOKEN=current
BT_RECONNECT_PREVIOUS_TOKEN=previous
bt_prepare_reconnect
test "$BT_RECONNECT_TARGET" = AA:BB:CC:DD:EE:FF
! bt_prepare_reconnect
test ! -e "$BT_RECONNECT_STATE/audio"
'''
        self.run_shell(body)
        (self.state / 'audio').write_text(f'stale {AUDIO}\n')
        self.run_shell('BT_RECONNECT_TOKEN=current; BT_RECONNECT_PREVIOUS_TOKEN=previous; '
                       '! bt_prepare_reconnect; test ! -e "$BT_RECONNECT_STATE/audio"')

    def test_missing_token_cannot_authorize_worker(self):
        (self.state / 'token').unlink()
        self.run_shell('unset BT_RECONNECT_TOKEN; ! bt_token_current; '
                       'BT_RECONNECT_TOKEN=""; ! bt_token_current')

    def test_plain_start_and_failed_start_do_not_replay_old_arm(self):
        init = INIT.read_text()
        worker = init[init.index('start_reconnect_worker() {'):
                      init.index('\nble_start() {')]
        harness = self.root / 'start-worker.sh'
        harness.write_text(HELPER.read_text() + '\n' + worker + r'''
BT_RECONNECT_TOKEN=current
BT_RECONNECT_PREVIOUS_TOKEN=previous
BT_DAEMON_STARTED=1
bt_daemon_identity() { BT_DAEMON_IDENTITY=daemon:1; }
BT_RECONNECT_HELPER="$TEST_ROOT/worker.sh"
start_reconnect_worker
wait
''')
        target = self.root / 'worker.sh'
        target.write_text('#!/bin/sh\nprintf "%s %s %s %s\\n" "$1" "$2" "$3" "$4" >> "$TEST_ROOT/spawns"\n')
        self.run_shell('true', harness)
        self.assertFalse((self.root / 'spawns').exists())
        # A failed start leaves an old capture behind. A later unrelated start
        # must consume it without launching a worker.
        (self.state / 'audio').write_text(f'older {AUDIO}\n')
        self.run_shell('true', harness)
        self.assertFalse((self.root / 'spawns').exists())
        self.assertFalse((self.state / 'audio').exists())
        (self.state / 'audio').write_text(f'previous {AUDIO}\n')
        self.run_shell('true', harness)
        expected = f'reconnect_worker current {AUDIO} daemon:1'
        self.assertEqual((self.root / 'spawns').read_text().splitlines(), [expected])
        self.run_shell('true', harness)
        self.assertEqual((self.root / 'spawns').read_text().splitlines(), [expected])

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux /proc identities')
    def test_start_selects_exact_daemon_when_busybox_pgrep_misses_it(self):
        daemon_path = self.root / 'bluetoothd'
        daemon_path.symlink_to(shutil.which('sleep'))
        daemon = subprocess.Popen([str(daemon_path), '10'])
        other = subprocess.Popen([shutil.which('sleep'), '10'])
        self.addCleanup(lambda: (daemon.terminate(), daemon.wait()) if daemon.poll() is None else None)
        self.addCleanup(lambda: (other.terminate(), other.wait()) if other.poll() is None else None)
        self.assertEqual(pathlib.Path(f'/proc/{daemon.pid}/comm').read_text().strip(), 'bluetoothd')

        init = INIT.read_text()
        worker = init[init.index('start_reconnect_worker() {'):
                      init.index('\nble_start() {')]
        harness = self.root / 'start-worker.sh'
        harness.write_text(HELPER.read_text() + '\n' + worker + r'''
BT_RECONNECT_TOKEN=current
BT_RECONNECT_PREVIOUS_TOKEN=previous
BT_DAEMON_STARTED=1
BT_RECONNECT_HELPER="$TEST_ROOT/worker.sh"
pgrep() { return 1; }
pidof() { printf '%s\n' "$PIDOF_RESULT"; }
start_reconnect_worker
wait
''')
        target = self.root / 'worker.sh'
        target.write_text('#!/bin/sh\nprintf "%s %s %s %s\\n" "$1" "$2" "$3" "$4" >> "$TEST_ROOT/spawns"\n')

        (self.state / 'audio').write_text(f'previous {AUDIO}\n')
        self.env['PIDOF_RESULT'] = str(daemon.pid)
        self.run_shell('true', harness)
        spawns = (self.root / 'spawns').read_text().splitlines()
        self.assertEqual(len(spawns), 1)
        self.assertRegex(spawns[0], rf'^reconnect_worker current {AUDIO} {daemon.pid}:[0-9]+$')

        # Both another daemon and an executable named like one are unsafe.
        for candidates in (f'{daemon.pid} {other.pid}', str(other.pid), ''):
            (self.state / 'audio').write_text(f'previous {AUDIO}\n')
            self.env['PIDOF_RESULT'] = candidates
            self.run_shell('true', harness)
            self.assertEqual((self.root / 'spawns').read_text().splitlines(), spawns)
            self.assertFalse((self.state / 'audio').exists())

    def test_worker_two_attempt_cap_and_new_token_cancellation(self):
        base = r'''
BT_RECONNECT_TOKEN=current
date() { /bin/date "$@"; }
bt_ctl() {
 case "$2" in
  show) printf 'Powered: yes\n';;
  info) printf 'Connected: no\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_run_bounded() { printf 'boolean true\n'; }
bt_worker_connect() {
 printf '%s\n' "$1" >> "$TEST_ROOT/connects"
 test "${CANCEL_AFTER_FIRST:-0}" -eq 0 || printf 'newer\n' > "$BT_RECONNECT_STATE/token"
 return 1
}
bt_reconnect_audio current AA:BB:CC:DD:EE:FF daemon:1
'''
        self.run_shell(base)
        self.assertEqual((self.root / 'connects').read_text().splitlines(), [AUDIO, AUDIO])
        (self.root / 'connects').unlink()
        self.run_shell('CANCEL_AFTER_FIRST=1\n' + base)
        self.assertEqual((self.root / 'connects').read_text().splitlines(), [AUDIO])

    def test_no_readiness_exits_within_bounded_window(self):
        (self.root / 'clock').write_text('100\n')
        body = r'''
BT_RECONNECT_TOKEN=current
bt_now() { cat "$TEST_ROOT/clock"; }
sleep() { now=$(cat "$TEST_ROOT/clock"); printf '%s\n' "$((now + 1))" > "$TEST_ROOT/clock"; }
bt_ctl() { printf 'Powered: no\n'; }
bt_worker_connect() { touch "$TEST_ROOT/connects"; }
bt_reconnect_audio current AA:BB:CC:DD:EE:FF daemon:1
'''
        self.run_shell(body)
        self.assertFalse((self.root / 'connects').exists())
        self.assertLessEqual(int((self.root / 'clock').read_text()), 111)

    def test_total_reconnect_window_stops_late_second_attempt(self):
        (self.root / 'clock').write_text('100\n')
        body = r'''
BT_RECONNECT_TOKEN=current
bt_now() { cat "$TEST_ROOT/clock"; }
sleep() { now=$(cat "$TEST_ROOT/clock"); printf '%s\n' "$((now + 1))" > "$TEST_ROOT/clock"; }
bt_ctl() {
 case "$2" in
  show) printf 'Powered: yes\n';;
  info) printf 'Connected: no\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_run_bounded() { printf 'boolean true\n'; }
bt_worker_connect() {
 printf '%s\n' "$1" >> "$TEST_ROOT/connects"
 printf '129\n' > "$TEST_ROOT/clock"
 return 1
}
bt_reconnect_audio current AA:BB:CC:DD:EE:FF daemon:1
'''
        self.run_shell(body)
        self.assertEqual((self.root / 'connects').read_text().splitlines(), [AUDIO])
        self.assertLessEqual(int((self.root / 'clock').read_text()), 130)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux /proc')
    def test_real_bounded_runner_exit_timeout_and_identity_failure(self):
        bin_dir = self.root / 'bin'
        bin_dir.mkdir()
        busybox = bin_dir / 'busybox'
        busybox.write_text('#!/bin/sh\ncase "$1" in\n'
                           ' sleep) shift; exec /bin/sleep "$@";;\n'
                           ' flock) shift; exec /usr/bin/flock "$@";;\n'
                           'esac\nexit 127\n')
        busybox.chmod(0o755)
        env = dict(self.env, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'])
        body = r'''
. "$1"
bt_run_bounded 2 sh -c 'exit 7'
test "$?" -eq 7 || exit 10
started=$(date +%s)
bt_run_bounded 1 sh -c 'sleep 5'
test "$?" -ne 0 || exit 11
ended=$(date +%s)
test "$((ended - started))" -le 3 || exit 12
bt_process_identity() { return 1; }
started=$(date +%s)
bt_run_bounded 2 sh -c 'sleep 5'
test "$?" -ne 0 || exit 13
ended=$(date +%s)
test "$((ended - started))" -le 2 || exit 14
'''
        subprocess.run(['sh', '-c', body, 'test', str(HELPER)], env=env,
                       check=True, timeout=9)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux flock')
    def test_waiting_worker_cannot_connect_after_new_stop_token(self):
        import fcntl

        bin_dir = self.root / 'bin'
        bin_dir.mkdir()
        busybox = bin_dir / 'busybox'
        busybox.write_text('#!/bin/sh\ncase "$1" in\n'
                           ' flock) shift; touch "$TEST_ROOT/flock-attempted"; exec /usr/bin/flock "$@";;\n'
                           ' sleep) shift; exec /bin/sleep "$@";;\n'
                           'esac\nexit 127\n')
        busybox.chmod(0o755)
        env = dict(self.env, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'])
        lock = self.state / 'lock'
        with lock.open('w') as lock_fd:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            body = r'''
. "$1"
BT_RECONNECT_TOKEN=current
BT_RECONNECT_DAEMON_IDENTITY=daemon:1
bt_daemon_identity() { BT_DAEMON_IDENTITY=daemon:1; }
bt_ctl() { touch "$TEST_ROOT/connect-ran"; }
bt_set_deadline 5 || exit 10
bt_worker_connect AA:BB:CC:DD:EE:FF
test "$?" -ne 0
'''
            worker = subprocess.Popen(['sh', '-c', body, 'test', str(HELPER)], env=env,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 3
                while not (self.root / 'flock-attempted').exists():
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.01)
                self.assertIsNone(worker.poll())
                (self.state / 'token').write_text('newer\n')
                fcntl.flock(lock_fd, fcntl.LOCK_UN)
                stdout, stderr = worker.communicate(timeout=3)
                self.assertEqual(worker.returncode, 0, (stdout, stderr))
                self.assertFalse((self.root / 'connect-ran').exists())
            finally:
                if worker.poll() is None:
                    worker.kill()
                    worker.wait()

    def test_capture_has_one_total_deadline(self):
        (self.root / 'clock').write_text('100\n')
        body = r'''
BT_RECONNECT_TOKEN=current
bt_now() { cat "$TEST_ROOT/clock"; }
bt_ctl() {
 limit=$1; shift
 now=$(cat "$TEST_ROOT/clock"); printf '%s\n' "$((now + limit))" > "$TEST_ROOT/clock"
 case "$1" in
  show) printf 'Powered: yes\n';;
  devices) printf 'Device 11:22:33:44:55:66 Controller\nDevice AA:BB:CC:DD:EE:FF Headphones\n';;
  info) printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\nUUID: Human Interface Device (00001124-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_capture_audio
'''
        self.run_shell(body)
        self.assertLessEqual(int((self.root / 'clock').read_text()), 106)
        self.assertFalse((self.state / 'audio').exists())

    def test_missing_monotonic_clock_disables_capture_and_reconnect(self):
        (self.state / 'audio').write_text(f'old {AUDIO}\n')
        body = r'''
BT_RECONNECT_TOKEN=current
bt_now() { return 1; }
bt_ctl() { touch "$TEST_ROOT/bluetoothctl-ran"; printf 'Powered: yes\n'; }
bt_worker_connect() { touch "$TEST_ROOT/connect-ran"; }
bt_capture_audio
bt_reconnect_audio current AA:BB:CC:DD:EE:FF daemon:1
'''
        self.run_shell(body)
        self.assertFalse((self.state / 'audio').exists())
        self.assertFalse((self.root / 'bluetoothctl-ran').exists())
        self.assertFalse((self.root / 'connect-ran').exists())

    def test_worker_rechecks_token_and_daemon_inside_lock(self):
        body = r'''
BT_RECONNECT_TOKEN=current
BT_RECONNECT_DAEMON_IDENTITY=daemon:1
BT_DEADLINE=$(( $(bt_now) + 30 ))
busybox() { test "$1 $2" = 'flock -n'; }
bt_daemon_identity() { BT_DAEMON_IDENTITY=$DAEMON_NOW; }
bt_ctl() { printf '%s\n' "$2" >> "$TEST_ROOT/connects"; }
DAEMON_NOW=daemon:2
! bt_worker_connect AA:BB:CC:DD:EE:FF
DAEMON_NOW=daemon:1
printf 'newer\n' > "$BT_RECONNECT_STATE/token"
! bt_worker_connect AA:BB:CC:DD:EE:FF
printf 'current\n' > "$BT_RECONNECT_STATE/token"
bt_worker_connect AA:BB:CC:DD:EE:FF
'''
        self.run_shell(body)
        self.assertEqual((self.root / 'connects').read_text().splitlines(), ['connect'])

    def test_stop_order_and_failed_optional_state_still_shutdown(self):
        init = INIT.read_text()
        stop = init.split('stop_bt() {', 1)[1].split('\n}', 1)[0]
        self.assertLess(stop.index('bt_capture_audio'), stop.index('killall bluealsa'))
        self.assertLess(stop.index('bt_capture_audio'), stop.index('bluetoothctl power off'))
        enable = (ROOT / 'workspace/all/common/generic_bt.c').read_text()
        enable = enable.split('void PLAT_bluetoothEnable(', 1)[1].split('\nvoid PLAT_bluetoothDisable(', 1)[0]
        tg5040 = enable.split('if (strcmp(PLATFORM, "tg5040") == 0) {', 1)[1].split('} else {', 1)[0]
        self.assertNotIn('bluetoothctl power off', tg5040)
        dispatch = init[init.index('case "$1" in\n\tstart|stop|restart)'):]
        for failure in (1, 3):
            with self.subTest(failure=failure):
                harness = self.root / 'dispatch.sh'
                harness.write_text('''#!/bin/sh
bt_toggle_begin() { return %d; }
stop_bt() { touch "$TEST_ROOT/shutdown"; }
start_bt() { touch "$TEST_ROOT/startup"; }
bt_token_current() { return 1; }
''' % failure + dispatch)
                subprocess.run(['sh', str(harness), 'stop'], env=self.env, check=True, timeout=3)
                self.assertTrue((self.root / 'shutdown').exists())
                (self.root / 'shutdown').unlink()
                restart = subprocess.run(['sh', str(harness), 'restart'], env=self.env, timeout=3)
                self.assertEqual(restart.returncode, 1)
                self.assertTrue((self.root / 'shutdown').exists())
                self.assertFalse((self.root / 'startup').exists())
                (self.root / 'shutdown').unlink()
                start = subprocess.run(['sh', str(harness), 'start'], env=self.env, timeout=3)
                self.assertEqual(start.returncode, 1)
                self.assertFalse((self.root / 'shutdown').exists())
                self.assertFalse((self.root / 'startup').exists())

    def test_superseded_request_does_not_fire_shutdown_trap(self):
        init = INIT.read_text()
        dispatch = init[init.index('case "$1" in\n\tstop|restart)'):]
        harness = self.root / 'superseded.sh'
        harness.write_text('''#!/bin/sh
bt_toggle_begin() { return 2; }
stop_bt() { touch "$TEST_ROOT/shutdown"; }
start_bt() { touch "$TEST_ROOT/startup"; }
''' + dispatch)
        for action in ('stop', 'restart'):
            with self.subTest(action=action):
                result = subprocess.run(['sh', str(harness), action], env=self.env,
                                        capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse((self.root / 'shutdown').exists())
                self.assertFalse((self.root / 'startup').exists())

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux shell redirection semantics')
    def test_fatal_lock_redirection_still_runs_shutdown_once(self):
        init = INIT.read_text()
        dispatch = init[init.index('case "$1" in\n\tstop|restart)'):]
        harness = self.root / 'fatal-lock.sh'
        harness.write_text('''#!/bin/sh
bt_toggle_begin() { exec 9>"$TEST_ROOT/lockdir"; }
stop_bt() { printf 'stop\\n' >> "$TEST_ROOT/shutdown"; }
start_bt() { touch "$TEST_ROOT/startup"; }
''' + dispatch)
        (self.root / 'lockdir').mkdir()
        for action in ('stop', 'restart'):
            with self.subTest(action=action):
                result = subprocess.run(['sh', str(harness), action], env=self.env,
                                        capture_output=True, text=True, timeout=3)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual((self.root / 'shutdown').read_text().splitlines(), ['stop'])
                self.assertFalse((self.root / 'startup').exists())
                (self.root / 'shutdown').unlink()


if __name__ == '__main__':
    unittest.main()
