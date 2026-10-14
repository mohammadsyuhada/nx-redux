"""Exercise boot recall and preference persistence in the checked-out source."""
import os
import pathlib
import subprocess
import test_nx_toggle_reconnect as toggle

ROOT = pathlib.Path(__file__).resolve().parents[3]


class BootReconnect(toggle.ToggleReconnect):
    def setUp(self):
        super().setUp()
        self.userdata = self.root / 'userdata'
        self.userdata.mkdir()
        self.preference = self.userdata / 'bt-boot-audio'
        self.env['USERDATA_PATH'] = str(self.userdata)
        # Additional behaviors of the external BlueZ stub, not helper replicas.
        stub = toggle.STUB.replace("elif args[:1]==['connect']:", """elif args[:1]==['connect']:
  if s.get('disable_during_connect'):
   (pathlib.Path(os.environ['USERDATA_PATH'])/'bt-boot-audio').write_text('0 AA:BB:CC:DD:EE:FF\\n')
  if s.get('manual_during_connect'):
   (pathlib.Path(os.environ['BT_RECONNECT_STATE'])/'boot-cancel').touch()
""")
        stub = stub.replace("print('Device 11:22:33:44:55:66 Controller\\nDevice AA:BB:CC:DD:EE:FF Audio')", """print('Device 11:22:33:44:55:66 Controller\\nDevice AA:BB:CC:DD:EE:FF Audio')
  if s.get('other_audio'): print('Device 22:33:44:55:66:77 Other audio')""")
        stub = stub.replace("connected=s.get('connected',False) or s.get('capture',False)", "connected=s.get('connected',False) or s.get('capture',False) or (args[1].startswith('22:33') and s.get('other_audio'))")
        for path in self.bin.iterdir():
            path.write_text(stub)

    def test_boot_save_completion_updates_item_without_success_status(self):
        source = (self.checkout / 'workspace/all/settings/settings_bt.c').read_text()
        start = source.index('static void bt_on_tick(SettingsPage* page) {')
        end = source.index('// Page Creation', start)
        function = source[start:source.rfind('// ============================================', start, end)]
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#define BT_IDX_BOOT 2
#define BT_PAIR_OK 0
#define BT_PAIR_TEMPORARY 1
#define BT_PAIR_FAILED 2
#define BT_PAIR_TRUST_FAILED 3
typedef struct { int unused; } SettingItem;
typedef struct {
    int needs_layout, selected, dynamic_start, item_count;
    SettingItem items[3];
    const char *status_msg;
} SettingsPage;
static struct { int done; } bt_action_ctx;
static struct { int val, done; } bt_boot_ctx;
static int bt_pair_result, bt_boot_result, sync_count;
static const char *bt_action_status;
static int settings_page_visible_count(SettingsPage *page) { return page->item_count; }
static void settings_item_sync(SettingItem *item) { (void)item; sync_count++; }
static int BT_enabled(void) { return 0; }
''' + function + r'''
int main(void) {
    SettingsPage page = { .dynamic_start = 3, .item_count = 4 };
    bt_boot_ctx.val = 1;
    bt_boot_ctx.done = 1;
    bt_boot_result = 0;
    bt_on_tick(&page);
    assert(bt_boot_ctx.done == 0 && sync_count == 1);
    assert(bt_action_status == NULL && page.status_msg == NULL);

    bt_boot_ctx.val = 0;
    bt_boot_ctx.done = 1;
    bt_on_tick(&page);
    assert(bt_boot_ctx.done == 0 && sync_count == 2);
    assert(bt_action_status == NULL && page.status_msg == NULL);

    bt_boot_ctx.done = 1;
    bt_boot_result = -1;
    bt_on_tick(&page);
    assert(bt_boot_ctx.done == 0 && sync_count == 3);
    assert(strcmp(page.status_msg, "Could not save boot reconnect. Check storage and retry.") == 0);
    return 0;
}
'''
        path = self.root / 'boot-status.c'
        path.write_text(harness)
        binary = self.root / 'boot-status'
        subprocess.run(['cc', '-std=c99', '-Werror', str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def run_shell(self, commands):
        # Existing deterministic fixture uses date as its virtual monotonic clock.
        super().run_shell('bt_now() { date +%s; }; ' + commands)

    def fresh_boot(self):
        for name in ['boot-cancel', 'boot-consumed']:
            path = self.state / name
            if path.is_dir(): path.rmdir()
            elif path.exists(): path.unlink()

    def test_boot_default_off_and_strict_record(self):
        for record in [None, '', '1', '1 BAD', '1 AA:BB:CC:DD:EE:FF;echo BAD',
                       '1 AA:BB:CC:DD:EE:FF\n1 AA:BB:CC:DD:EE:FF', '0 '+toggle.AUDIO, '1 -']:
            with self.subTest(record=record):
                self.fresh_boot()
                if record is not None: self.preference.write_text(record+'\n')
                self.run_shell('bt_reconnect_audio 123 boot')
                self.assertEqual(self.attempts(), 0)

    def test_enabled_boot_once_and_volatile_path_stays_separate(self):
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        self.configure(succeed_on=1)
        (self.state / 'audio').unlink()
        self.run_shell('bt_reconnect_audio 123 boot')
        self.assertEqual(self.attempts(), 1)
        self.configure(succeed_on=1)
        self.run_shell('bt_reconnect_audio 123 boot')
        self.assertEqual(self.attempts(), 0)
        self.fresh_boot()
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 0)

    def test_boot_cancellation_never_disconnects_late_success(self):
        for config in [dict(disable_during_connect=True), dict(manual_during_connect=True)]:
            self.fresh_boot()
            self.preference.write_text('1 '+toggle.AUDIO+'\n')
            self.configure(succeed_on=1, **config)
            self.run_shell('bt_reconnect_audio 123 boot')
            self.assertEqual(self.attempts(), 1)
            calls=(self.root/'calls').read_text()
            self.assertNotIn('bluetoothctl disconnect', calls)
        self.fresh_boot()
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        self.configure()
        self.run_shell('bt_boot_cancel; bt_reconnect_audio 123 boot')
        self.assertEqual(self.attempts(), 0)
        # Boot-only inhibitor must not suppress existing off/on recall.
        self.run_shell('bt_reconnect_audio 123')
        self.assertEqual(self.attempts(), 2)

    def test_boot_skips_another_connected_audio_sink(self):
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        self.configure(other_audio=True)
        self.run_shell('bt_reconnect_audio 123 boot')
        self.assertEqual(self.attempts(), 0)

    def test_simple_on_off_remembers_last_eligible_connection(self):
        self.configure(capture=True)
        self.run_shell('bt_boot_remember '+toggle.AUDIO)
        self.assertEqual(self.preference.read_text(), '0 '+toggle.AUDIO+'\n')
        self.run_shell('bt_boot_set on')
        self.assertEqual(self.preference.read_text(), '1 '+toggle.AUDIO+'\n')
        self.run_shell('bt_boot_set off')
        self.assertEqual(self.preference.read_text(), '0 '+toggle.AUDIO+'\n')
        self.configure(controller=True, capture=True)
        self.run_shell('bt_boot_remember 11:22:33:44:55:66')
        self.assertEqual(self.preference.read_text(), '0 '+toggle.AUDIO+'\n')
        self.configure(capture=True)
        self.run_shell('bt_boot_remember 22:33:44:55:66:77')
        self.assertEqual(self.preference.read_text(), '0 22:33:44:55:66:77\n')

    def test_enabled_without_history_and_read_only_failure(self):
        self.run_shell('bt_boot_set on')
        self.assertEqual(self.preference.read_text(), '1 -\n')
        self.configure(capture=True)
        self.run_shell('bt_boot_remember '+toggle.AUDIO)
        self.assertEqual(self.preference.read_text(), '1 '+toggle.AUDIO+'\n')
        self.preference.unlink()
        self.preference.mkdir()
        self.run_shell('if bt_boot_set off; then exit 1; fi')
        self.assertTrue(self.preference.is_dir())

    def test_persistence_lock_prevents_lost_updates(self):
        self.preference.write_text('0 '+toggle.AUDIO+'\n')
        self.run_shell('bt_pref_lock; exec 6>&7; exec 7>&-; '
                       '(if bt_boot_set on; then exit 1; fi); exec 6>&-')
        self.assertEqual(self.preference.read_text(), '0 '+toggle.AUDIO+'\n')

    def test_symlink_preferences_fail_closed(self):
        outside=self.root/'outside'
        outside.write_text('1 '+toggle.AUDIO+'\n')
        self.preference.symlink_to(outside)
        self.run_shell('bt_reconnect_audio 123 boot; if bt_boot_set off; then exit 1; fi')
        self.assertEqual(self.attempts(), 0)
        self.assertEqual(outside.read_text(), '1 '+toggle.AUDIO+'\n')

    def test_boot_eligibility_and_unavailable_window(self):
        for settings in [dict(off=True), dict(no_bluealsa=True), dict(unbonded=True),
                         dict(untrusted=True), dict(controller=True), dict(connected=True),
                         dict(succeed_on=99)]:
            self.fresh_boot()
            self.preference.write_text('1 '+toggle.AUDIO+'\n')
            self.configure(**settings)
            self.run_shell('bt_reconnect_audio 123 boot')
            self.assertLessEqual(self.attempts(), 2)
            if 'succeed_on' not in settings: self.assertEqual(self.attempts(), 0)
            import json
            if 'succeed_on' not in settings:
                self.assertLessEqual(json.loads((self.root/'stub.json').read_text())['clock'], 130)
            # Connect stub always adds 8 synthetic seconds even when called with
            # less remaining time. Check actual clamped watchdog budgets separately.

    def test_forget_only_matching_target_keeps_toggle(self):
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        subprocess.run(['sh', str(self.helper), 'forget_boot', '11:22:33:44:55:66'],
                       env=self.env, check=True, timeout=5)
        self.assertEqual(self.preference.read_text(), '1 '+toggle.AUDIO+'\n')
        subprocess.run(['sh', str(self.helper), 'forget_boot', toggle.AUDIO],
                       env=self.env, check=True, timeout=5)
        self.assertEqual(self.preference.read_text(), '1 -\n')

    def test_deadline_clamps_each_call_to_remaining_window(self):
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        self.configure(succeed_on=99)
        # Record the actual external watchdog budget, while advancing virtual time
        # by that budget as a worst-case slow command. Do not charge the stub's
        # fixed eight seconds when the real watchdog has less time remaining.
        self.run_shell("""
        bt_run_bounded() {
          remaining=$((BT_DEADLINE - $(date +%s)))
          [ "$1" -gt 0 ] && [ "$1" -le "$remaining" ] || exit 90
          budget=$1; shift
          start=$(date +%s)
          "$@"
          result=$?
          python3 -c 'import json,os,pathlib,sys; p=pathlib.Path(os.environ["STUB_STATE"])/"stub.json"; s=json.loads(p.read_text()); s["clock"]=int(sys.argv[1])+int(sys.argv[2]); p.write_text(json.dumps(s))' "$start" "$budget"
          return "$result"
        }
        bt_reconnect_audio 123 boot
        [ "$(date +%s)" -le 130 ]
        """)
        self.assertLessEqual(self.attempts(), 2)

    def test_c_hooks_preserve_manual_actions_when_optional_cancel_fails(self):
        source=(self.checkout/'workspace/all/common/generic_bt.c').read_text()
        function=source[source.index('static bool bt_cancel_boot('):source.index('static void bt_remember_boot(')]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define SYSTEM_PATH "/system"
#define LOG_warn(...) ((void)0)
static bool enabled;
static int calls, result;
static bool PLAT_bluetoothBootReconnectEnabled(void) { return enabled; }
static int bt_run_cmd_deadline(const char* c,void* o,int l,int t) { calls++; return result; }
""" + function + r"""
int main(void) {
  enabled=false; result=1; assert(bt_cancel_boot()); assert(calls==0);
  enabled=true; assert(bt_cancel_boot()); assert(calls==1);
  result=-2; assert(bt_cancel_boot()); assert(calls==2);
  result=0; assert(bt_cancel_boot()); assert(calls==3);
}
"""
        path=self.root/'cancel.c'; path.write_text(harness)
        subprocess.run(['cc','-std=c99','-Werror',str(path),'-o',str(self.root/'cancel')],check=True)
        subprocess.run([str(self.root/'cancel')],check=True)

    def test_real_monotonic_deadline_ignores_backward_wall_clock(self):
        import time
        self.preference.write_text('1 '+toggle.AUDIO+'\n')
        self.configure(no_bluealsa=True, clock=-100000)
        start=time.monotonic()
        # Bypass the virtual clock override and run the delivered /proc/uptime
        # implementation while the wall-clock command reports a bogus past time.
        import threading
        uptime=self.root/'uptime'
        stop=threading.Event()
        def update_uptime():
            while not stop.is_set():
                temporary=self.root/'uptime.new'
                temporary.write_text(f"{1000+time.monotonic()-start:.2f} 0.00\n")
                temporary.replace(uptime)
                stop.wait(0.02)
        feeder=threading.Thread(target=update_uptime)
        feeder.start()
        while not uptime.exists(): time.sleep(0.01)
        # macOS has no /proc; substitute only the proc-file location, preserving
        # the delivered parsing/arithmetic, with a live monotonic uptime feed.
        delivered=self.helper
        self.helper=self.root/'helper-monotonic.sh'
        self.helper.write_text(delivered.read_text().replace('/proc/uptime', str(uptime)))
        try:
            super().run_shell('sleep() { /bin/sleep "$@"; }; bt_reconnect_audio 123 boot', virtual_clock=False)
        finally:
            stop.set(); feeder.join(); self.helper=delivered
        elapsed=time.monotonic()-start
        self.assertGreaterEqual(elapsed, 9)
        self.assertLess(elapsed, 13)
        self.assertEqual(self.attempts(), 0)
        self.assertNotIn('date +%s', (self.root/'calls').read_text())
