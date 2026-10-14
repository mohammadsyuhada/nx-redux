"""Exercise the checked-out Bluetooth helper with stubbed shell tools."""
import json
import os
import pathlib
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
INIT = 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_init.sh'
HELPER = 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_reconnect.sh'
AUDIO = 'AA:BB:CC:DD:EE:FF'
UPSTREAM_BASE = 'af1b0fbbb1fa0489875daad5c05f4980e3602b4d'

STUB = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
root=pathlib.Path(os.environ['STUB_STATE'])
p=root/'stub.json'
s=json.loads(p.read_text())
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
with (root/'calls').open('a') as f: f.write(name+' '+ ' '.join(args)+'\n')
if name=='busybox':
 if args==['--list']:
  if s.get('no_list'): sys.exit(2)
  print('flock')
 elif args==['flock','--help']:
  if s.get('no_flock'): sys.exit(127)
  print('flock [-sxun] FD')
 elif args[:1]==['sleep']:
  import time
  time.sleep(float(args[1]))
 else:
  import fcntl
  try: fcntl.flock(int(args[-1]),fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError: sys.exit(1)
 sys.exit(0)
elif name=='date':
 print(int(s.get('clock',100)))
elif name=='sleep': s['clock']=s.get('clock',100)+float(args[0])
elif name=='dbus-send':
 s['clock']=s.get('clock',100)+1
 print('boolean '+('false' if s.get('no_bluealsa') else 'true'))
elif name=='bluetoothctl':
 if s.get('hang'):
  import time
  time.sleep(5)
 if args[:1]==['--timeout']:
  if s.get('buffer_until_timeout'):
   import time
   time.sleep(int(args[1])+0.2)
  s['clock']=s.get('clock',100)+int(args[1]); args=args[2:]
 else:
  s['clock']=s.get('clock',100)+(8 if args[:1]==['connect'] else 2)
 if args[:1]==['show']: print('Name: Powered: yes\nPowered: '+('no' if s.get('off') else 'yes'))
 elif args[:2]==['devices','Connected']:
  print('Device 11:22:33:44:55:66 Controller\nDevice AA:BB:CC:DD:EE:FF Audio')
 elif args[:1]==['info']:
  controller=args[1].startswith('11:22') or s.get('controller')
  bonded=not s.get('unbonded')
  trusted=not s.get('untrusted')
  connected=s.get('connected',False) or s.get('capture',False)
  print('Name: Bonded: yes Trusted: yes Connected: yes')
  print('Bonded: '+('yes' if bonded else 'no'))
  print('Trusted: '+('yes' if trusted else 'no'))
  print('Connected: '+('yes' if connected else 'no'))
  print('UUID: '+('Human Interface Device (00001124-0000-1000-8000-00805f9b34fb)' if controller else 'Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)'))
 elif args[:1]==['connect']:
  s['attempts']=s.get('attempts',0)+1
  if s.get('cancel'):
   (pathlib.Path(os.environ['BT_RECONNECT_STATE'])/'token').write_text('newer\n')
  if s['attempts']>=s.get('succeed_on',2): s['connected']=True
p.write_text(json.dumps(s))
'''


class ToggleReconnect(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checkout = ROOT
        # Compare shutdown behavior with the parent source, including after this PR is committed.
        cls.baseline = subprocess.check_output(['git', 'show', UPSTREAM_BASE + ':' + INIT], cwd=ROOT)
        cls.helper = cls.checkout / HELPER

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.state = self.root / 'state'
        self.state.mkdir()
        (self.state / 'token').write_text('123\n')
        (self.state / 'audio').write_text(AUDIO + '\n')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        for command in ['bluetoothctl', 'dbus-send', 'date', 'sleep', 'busybox', 'logger']:
            path = self.bin / command
            path.write_text(STUB)
            path.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.bin)+os.pathsep+os.environ['PATH'],
                        STUB_STATE=str(self.root), BT_RECONNECT_STATE=str(self.state))
        self.configure()

    def configure(self, **settings):
        (self.root / 'stub.json').write_text(json.dumps(settings))

    def run_shell(self, commands, virtual_clock=True):
        portable_identity = ('bt_current_pid() { BT_CURRENT_PID=$$; }; '
                             'bt_process_identity() { BT_PROCESS_IDENTITY=; kill -0 "$1" 2>/dev/null || return 1; BT_PROCESS_IDENTITY=$1; }; ')
        clock = 'bt_now() { date +%s; }; ' if virtual_clock else ''
        subprocess.run(['sh', '-c', '. "$1"; ' + clock + portable_identity + commands, 'test', str(self.helper)],
                       env=self.env, check=True, timeout=15)

    def attempts(self):
        return json.loads((self.root / 'stub.json').read_text()).get('attempts', 0)

    def test_retry_after_zero_exit_failure_and_stop_on_success(self):
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 2)
        self.assertLessEqual(json.loads((self.root / 'stub.json').read_text())['clock'], 130)
        calls = (self.root / 'calls').read_text()
        self.assertIn('logger -t nx-bt-reconnect connect attempt 2', calls)
        self.assertIn('logger -t nx-bt-reconnect connection confirmed', calls)
        self.assertTrue(json.loads((self.root / 'stub.json').read_text())['connected'])
        self.configure(succeed_on=1)
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 1)

    def test_failure_attempt_cap_and_off_cancellation(self):
        self.configure(succeed_on=99)
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 2)
        self.configure(cancel=True, succeed_on=99)
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 1)
        self.configure()
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 0)

    def test_readiness_and_revalidation_fail_closed(self):
        for settings in [dict(off=True), dict(no_bluealsa=True), dict(unbonded=True),
                         dict(untrusted=True), dict(controller=True), dict(connected=True)]:
            with self.subTest(settings=settings):
                self.configure(**settings)
                self.run_shell('bt_reconnect_audio 123')
                self.assertEqual(self.attempts(), 0)

    def test_legacy_busybox_without_list_still_captures_and_reconnects(self):
        self.configure(capture=True, no_list=True)
        self.run_shell('bt_toggle_begin && bt_capture_audio')
        self.assertEqual((self.state / 'audio').read_text().strip(), AUDIO)
        self.configure(no_list=True)
        self.run_shell('bt_reconnect_audio "$(cat "$BT_RECONNECT_STATE/token")"')
        self.assertEqual(self.attempts(), 2)
        self.assertNotIn('busybox --list', (self.root / 'calls').read_text())

    def test_missing_flock_preserves_unavailable_result(self):
        self.configure(no_flock=True)
        self.run_shell('bt_toggle_begin; result=$?; test "$result" -eq 1')
        self.assertFalse((self.state / 'lock').exists())

    def test_device_preflight_is_read_only_and_catches_missing_capability(self):
        self.configure(capture=True, no_list=True)
        portable = self.root / 'portable-helper.sh'
        portable.write_text(self.helper.read_text() + '\n'
                            'bt_now() { date +%s; }\n'
                            'bt_current_pid() { BT_CURRENT_PID=$$; }\n'
                            'bt_process_identity() { BT_PROCESS_IDENTITY=; kill -0 "$1" 2>/dev/null || return 1; BT_PROCESS_IDENTITY=$1; }\n')
        original_state = {path.name: path.read_bytes() for path in self.state.iterdir()}
        result = subprocess.run(['sh', str(ROOT / 'scripts/tests/bluetooth_reconnect/check-device-helper.sh'), str(portable), AUDIO],
                                env=self.env, capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = (self.root / 'calls').read_text()
        self.assertNotIn(' connect ', calls)
        self.assertNotIn(' power ', calls)
        self.assertNotIn(' reconnect_worker ', calls)
        self.assertIn('audio selection captured', result.stdout)
        self.assertEqual(original_state, {path.name: path.read_bytes() for path in self.state.iterdir()})
        self.configure(no_flock=True)
        result = subprocess.run(['sh', str(ROOT / 'scripts/tests/bluetooth_reconnect/check-device-helper.sh'), str(portable)],
                                env=self.env, capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode, 1)
        self.assertIn('initialization failed', result.stderr)

    def test_buffered_cli_output_is_captured_without_internal_timeout(self):
        self.configure(buffer_until_timeout=True)
        self.run_shell('bt_ctl 1 show > "$STUB_STATE/show-output"')
        self.assertIn('Powered: yes', (self.root / 'show-output').read_text())
        self.assertNotIn('bluetoothctl --timeout', (self.root / 'calls').read_text())

    def test_capture_filters_controller_and_false_properties(self):
        self.configure(capture=True)
        self.run_shell('BT_RECONNECT_TOKEN=123; bt_capture_audio')
        self.assertEqual((self.state / 'audio').read_text().strip(), AUDIO)
        self.configure(capture=True, unbonded=True)
        self.run_shell('BT_RECONNECT_TOKEN=123; bt_capture_audio')
        self.assertFalse((self.state / 'audio').exists())

    def test_invalid_address_and_missing_volatile_state(self):
        (self.state / 'audio').write_text('AA:BB:CC:DD:EE:FF; touch /tmp/injected\n')
        self.run_shell('bt_reconnect_audio 123')
        self.assertFalse((self.root / 'calls').exists())
        (self.state / 'audio').unlink()
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 0)

    def test_daemon_readiness_hang_cannot_prevent_shutdown(self):
        self.configure(hang=True)
        started = time.monotonic()
        self.run_shell('BT_RECONNECT_TOKEN=123; bt_capture_audio; touch "$STUB_STATE/shutdown-ran"')
        self.assertLess(time.monotonic() - started, 3.5)
        self.assertTrue((self.root / 'shutdown-ran').exists())

    def test_proc_identity_parser_checks_parent_and_start_time(self):
        proc = self.root / 'proc'
        (proc / '123').mkdir(parents=True)
        (proc / 'self').mkdir()
        record = '123 (command with ) spaces) ' + ' '.join(['S', '456'] + ['0'] * 17 + ['789']) + '\n'
        (proc / '123/stat').write_text(record)
        (proc / 'self/stat').write_text(record)
        helper = self.root / 'identity-helper.sh'
        helper.write_text(self.helper.read_text().replace('/proc/', str(proc) + '/'))
        commands = ('. "$1"; bt_current_pid; test "$BT_CURRENT_PID" = 123; '
                    'bt_process_identity 123 456; test "$BT_PROCESS_IDENTITY" = 789; '
                    '! bt_process_identity 123 999; ! bt_process_identity 999 456')
        subprocess.run(['sh', '-c', commands, 'test', str(helper)], env=self.env, check=True)

    def test_watchdog_skips_recycled_or_missing_identity(self):
        # Intercept kill so any attempt to signal a mismatching identity is observable.
        commands = ('bt_process_identity() { BT_PROCESS_IDENTITY=replacement; }; '
                    'kill() { touch "$STUB_STATE/wrong-kill"; }; '
                    'bt_kill_identity 123 456 original; '
                    'bt_kill_identity 123 456 ""; exit 0')
        self.run_shell(commands)
        self.assertFalse((self.root / 'wrong-kill').exists())

    def test_kernel_lock_released_after_crashed_owner(self):
        commands = '. "$1"; bt_toggle_begin || exit 1; touch "$STUB_STATE/locked"; /bin/sleep 5'
        holder = subprocess.Popen(['sh', '-c', commands, 'test', str(self.helper)], env=self.env, start_new_session=True)
        deadline = time.monotonic() + 3
        while not (self.root / 'locked').exists():
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.01)
        # Kill the lifecycle group, including a temporary inherited command fd.
        os.killpg(holder.pid, 9)
        holder.wait()
        self.run_shell('bt_toggle_begin; bt_token_current')
        self.assertNotEqual((self.state / 'token').read_text().strip(), '123')

    def test_copied_etc_script_resolves_sd_helper_for_suspend(self):
        system = self.root / 'system'
        helper = system / 'etc/bluetooth/bt_reconnect.sh'
        helper.parent.mkdir(parents=True)
        helper.write_bytes(self.helper.read_bytes())
        etc_init = self.root / 'etc/bt_init.sh'
        etc_init.parent.mkdir()
        preamble = (self.checkout / INIT).read_text().split('TRIMUI_MODEL=', 1)[0]
        etc_init.write_text(preamble + '\nprintf "%s\\n" "$BT_RECONNECT_HELPER"\n')
        env = dict(self.env, SYSTEM_PATH=str(system))
        result = subprocess.check_output(['sh', str(etc_init)], env=env, text=True)
        self.assertEqual(result.strip(), str(helper))

    def test_missing_helper_keeps_original_lifecycle_available(self):
        etc_init = self.root / 'etc/bt_init.sh'
        etc_init.parent.mkdir()
        text = (self.checkout / INIT).read_text()
        preamble = text.split('TRIMUI_MODEL=', 1)[0]
        dispatch = text[text.index('case "$1" in'):text.index('case "$1" in', text.index('case "$1" in') + 1)]
        etc_init.write_text(preamble + dispatch + '\ntouch "$STUB_STATE/baseline-ran"\n')
        env = dict(self.env, SYSTEM_PATH=str(self.root / 'missing-system'))
        subprocess.run(['sh', str(etc_init), 'stop'], env=env, check=True)
        self.assertTrue((self.root / 'baseline-ran').exists())

    def test_broken_optional_state_retains_baseline_lifecycle(self):
        system = self.root / 'system'
        helper = system / 'etc/bluetooth/bt_reconnect.sh'
        helper.parent.mkdir(parents=True)
        helper.write_bytes(self.helper.read_bytes())
        etc_init = self.root / 'etc/bt_init.sh'
        etc_init.parent.mkdir()
        text = (self.checkout / INIT).read_text()
        preamble = text.split('TRIMUI_MODEL=', 1)[0]
        dispatch = text[text.index('case "$1" in'):text.index('case "$1" in', text.index('case "$1" in') + 1)]
        etc_init.write_text(preamble + dispatch + '\ntouch "$STUB_STATE/baseline-ran"\n')
        (self.state / 'lock').mkdir()
        env = dict(self.env, SYSTEM_PATH=str(system))
        subprocess.run(['sh', str(etc_init), 'stop'], env=env, check=True)
        self.assertTrue((self.root / 'baseline-ran').exists())
        (self.root / 'baseline-ran').unlink()
        blocked = self.root / 'blocked'
        blocked.write_text('not a directory')
        env['BT_RECONNECT_STATE'] = str(blocked)
        subprocess.run(['sh', str(etc_init), 'stop'], env=env, check=True, stderr=subprocess.DEVNULL)
        self.assertTrue((self.root / 'baseline-ran').exists())

    def test_latest_request_serializes_actions_and_invalidates_old_worker(self):
        holder_commands = ('. "$1"; bt_toggle_begin || exit 1; touch "$STUB_STATE/holder-ready"; /bin/sleep 0.4; '
                           'bt_token_current && touch "$STUB_STATE/old-action"; exit 0')
        holder = subprocess.Popen(['sh', '-c', holder_commands, 'test', str(self.helper)], env=self.env)
        try:
            deadline = time.monotonic() + 3
            while not (self.root / 'holder-ready').exists():
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.01)
            self.run_shell('bt_toggle_begin; bt_token_current; touch "$STUB_STATE/new-action"')
            self.assertEqual(holder.wait(timeout=3), 0)
            self.assertFalse((self.root / 'old-action').exists())
            self.assertTrue((self.root / 'new-action').exists())
            self.run_shell('bt_toggle_begin; bt_token_current')
        finally:
            if holder.poll() is None:
                holder.kill()
                holder.wait()

    def test_shutdown_guards_preserved_and_shell_syntax(self):
        modified = (self.checkout / INIT).read_bytes()
        before = self.baseline.split(b'stop_bt() {', 1)[1].split(b'\ncase "$1" in', 1)[0]
        after = modified.split(b'stop_bt() {', 1)[1].split(b'\ncase "$1" in', 1)[0]
        self.assertEqual(before, after.replace(b'\n\tbt_capture_audio', b'', 1))
        for path in [self.checkout / INIT, self.helper]:
            subprocess.run(['sh', '-n', str(path)], check=True)

if __name__ == '__main__':
    unittest.main()
