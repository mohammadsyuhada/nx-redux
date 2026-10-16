"""Host regressions for the opt-in tg5040 boot headphone recall policy."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
HELPER = ROOT / 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_reconnect.sh'
INIT = ROOT / 'skeleton/SYSTEM/tg5040/etc/bluetooth/bt_init.sh'
LAUNCH = ROOT / 'skeleton/SYSTEM/tg5040/paks/MinUI.pak/launch.sh'
AUDIO = 'AA:BB:CC:DD:EE:FF'
OTHER = '22:33:44:55:66:77'


class BootReconnectRegression(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.state = self.root / 'state'
        self.state.mkdir()
        (self.state / 'token').write_text('current\n')
        self.userdata = self.root / 'userdata'
        self.userdata.mkdir()
        self.preference = self.userdata / 'bt-boot-audio'
        self.env = dict(os.environ, BT_RECONNECT_STATE=str(self.state),
                        BT_BOOT_DIR=str(self.userdata), TEST_ROOT=str(self.root))

    def run_shell(self, body):
        setup = r'''. "$1"; shift
bt_now() { printf '100\n'; }
busybox() { case "$1" in flock) return 0;; esac; return 1; }
'''
        return subprocess.run(['sh', '-c', setup + body, 'test', str(HELPER)],
                              env=self.env, check=True, capture_output=True,
                              text=True, timeout=8)

    def test_boot_defaults_off_and_rejects_malformed_or_linked_records(self):
        self.run_shell('! bt_boot_enabled')
        for record in ('', '1', '1 BAD', f'0 {AUDIO}',
                       f'1 {AUDIO};touch BAD', f'1 {AUDIO}\n1 {AUDIO}'):
            with self.subTest(record=record):
                self.preference.write_text(record + '\n')
                self.run_shell('! bt_boot_enabled')
        for record in (f'1 {AUDIO}\nBAD', f'1 {AUDIO} extra\n',
                       f'1 {AUDIO} \n', f'1 {AUDIO}\n\n'):
            with self.subTest(record=record):
                self.preference.write_bytes(record.encode())
                self.run_shell('! bt_boot_enabled')
        self.preference.write_text('1 -\n')
        self.run_shell('bt_boot_enabled')  # Setting is On, pending a remembered headset.
        self.run_shell(f'! bt_valid_addr "{AUDIO}\nBAD"')
        self.preference.unlink()
        outside = self.root / 'outside'
        outside.write_text(f'1 {AUDIO}\n')
        self.preference.symlink_to(outside)
        self.run_shell('! bt_boot_enabled; ! bt_boot_set off')
        self.assertEqual(outside.read_text(), f'1 {AUDIO}\n')

    def test_bluetooth_static_rows_keep_requested_order(self):
        source = (ROOT / 'workspace/all/settings/settings_bt.c').read_text()
        indices = {name: int(index) for name, index in re.findall(
            r'^#define BT_IDX_(\w+) (\d+)$', source, re.MULTILINE)}
        create = source[source.index('SettingsPage* bt_page_create(void) {'):
                        source.index('void bt_page_destroy(')]
        rows = re.findall(
            r'page->items\[BT_IDX_(\w+)\] = \(SettingItem\)\{\s*\.name = "([^"]+)"',
            create)
        expected = ['Bluetooth', 'Bluetooth diagnostics',
                    'Reconnect audio on boot', 'Scan again']
        self.assertEqual([name for _, name in rows], expected)
        self.assertEqual([name for key, name in sorted(rows, key=lambda row: indices[row[0]])],
                         expected)
        self.assertEqual(len(rows), len(expected))

    def test_boot_opportunity_is_claimed_only_once(self):
        self.run_shell('bt_boot_claim; ! bt_boot_claim')
        self.assertTrue((self.state / 'boot-consumed').is_dir())

    def test_launcher_boot_gate_consumes_off_no_target_and_bt_off(self):
        init = INIT.read_text()
        worker = init[init.index('start_boot_worker() {'):init.index('\nble_start() {')]
        dispatch = init[init.index('BT_BOOT_FIRST=0\n'):]
        harness = self.root / 'boot-dispatch.sh'
        harness.write_text(HELPER.read_text() + '\n' + worker + r'''
BT_RECONNECT_HELPER="$TEST_ROOT/worker.sh"
bt_toggle_begin() { BT_RECONNECT_TOKEN=current; return 0; }
bt_daemon_identity() { BT_DAEMON_IDENTITY=daemon:1; }
start_bt() { BT_DAEMON_STARTED=0; touch "$TEST_ROOT/started"; }
stop_bt() { touch "$TEST_ROOT/stopped"; }
start_reconnect_worker() { touch "$TEST_ROOT/restart-worker"; }
''' + dispatch + '\nwait\n')
        target = self.root / 'worker.sh'
        target.write_text('#!/bin/sh\nprintf "%s %s %s %s %s\\n" "$1" "$2" "$3" "$4" "$5" >> "$TEST_ROOT/spawns"\n')

        def invoke(*args):
            subprocess.run(['sh', str(harness), *args], env=self.env, check=True,
                           capture_output=True, text=True, timeout=4)

        invoke('start', 'boot')  # Missing preference is Off and consumes the chance.
        self.assertTrue((self.state / 'boot-consumed').is_dir())
        self.assertFalse((self.root / 'spawns').exists())
        self.preference.write_text(f'1 {AUDIO}\n')
        invoke('start', 'boot')  # Enabling later cannot cause a launcher retry.
        self.assertFalse((self.root / 'spawns').exists())
        (self.state / 'boot-consumed').rmdir()
        self.preference.write_text('1 -\n')
        invoke('start', 'boot')  # On without a target also consumes the chance.
        self.assertFalse((self.root / 'spawns').exists())
        (self.state / 'boot-consumed').rmdir()
        invoke('stop', 'boot')  # Bluetooth disabled at boot consumes the chance.
        self.assertTrue((self.root / 'stopped').exists())
        self.preference.write_text(f'1 {AUDIO}\n')
        invoke('start', 'boot')
        self.assertFalse((self.root / 'spawns').exists())
        (self.state / 'boot-consumed').rmdir()
        invoke('start', 'boot')  # Boot may recall with an already-live daemon.
        self.assertEqual((self.root / 'spawns').read_text().splitlines(),
                         [f'reconnect_worker current {AUDIO} daemon:1 boot'])
        invoke('start', 'boot')
        self.assertEqual(len((self.root / 'spawns').read_text().splitlines()), 1)
        invoke('start')
        self.assertTrue((self.root / 'restart-worker').exists())
        self.assertEqual(len((self.root / 'spawns').read_text().splitlines()), 1)
        self.assertIn('start boot', LAUNCH.read_text())
        self.assertIn('stop boot', LAUNCH.read_text())

    def test_enabling_seeds_one_eligible_connected_headset(self):
        body = r'''
bt_ctl() {
 case "$2 $3" in
  'devices Connected') printf 'Device 11:22:33:44:55:66 Hybrid\nDevice AA:BB:CC:DD:EE:FF Headphones\n';;
  'info 11:22:33:44:55:66') printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\nUUID: Human Interface Device (00001812-0000-1000-8000-00805f9b34fb)\n';;
  'info AA:BB:CC:DD:EE:FF') printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_boot_set on
'''
        self.run_shell(body)
        self.assertEqual(self.preference.read_text(), f'1 {AUDIO}\n')
        self.run_shell('bt_boot_set off')
        self.assertEqual(self.preference.read_text(), f'0 {AUDIO}\n')

    def test_enabling_without_unique_headset_preserves_only_last_target(self):
        self.run_shell('bt_ctl() { printf ""; }; bt_boot_set on')
        self.assertEqual(self.preference.read_text(), '1 -\n')
        self.run_shell('bt_boot_set off')
        self.assertEqual(self.preference.read_text(), '0 -\n')
        self.preference.write_text(f'0 {AUDIO}\n')
        body = r'''
bt_ctl() {
 case "$2" in
  devices) printf 'Device AA:BB:CC:DD:EE:FF One\nDevice 22:33:44:55:66:77 Two\n';;
  info) printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_boot_set on
'''
        self.run_shell(body)
        self.assertEqual(self.preference.read_text(), f'1 {AUDIO}\n')

    def test_remember_requires_verified_pure_audio_and_forget_matches_target(self):
        self.preference.write_text(f'0 {AUDIO}\n')
        bad = r'''
bt_ctl() { printf 'Name: Bonded: yes\nConnected: yes\nBonded: no\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n'; }
bt_boot_remember 11:22:33:44:55:66
'''
        self.run_shell(bad)
        self.assertEqual(self.preference.read_text(), f'0 {AUDIO}\n')
        hybrid = r'''
bt_ctl() { printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\nUUID: Human Interface Device (00001124-0000-1000-8000-00805f9b34fb)\n'; }
bt_boot_remember 11:22:33:44:55:66
'''
        self.run_shell(hybrid)
        self.assertEqual(self.preference.read_text(), f'0 {AUDIO}\n')
        good = r'''
bt_ctl() { printf 'Connected: yes\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n'; }
bt_boot_remember 22:33:44:55:66:77
'''
        self.run_shell(good)
        self.assertEqual(self.preference.read_text(), f'0 {OTHER}\n')
        self.run_shell(f'bt_boot_forget {AUDIO}')
        self.assertEqual(self.preference.read_text(), f'0 {OTHER}\n')
        self.run_shell(f'bt_boot_forget {OTHER}')
        self.assertEqual(self.preference.read_text(), '0 -\n')

    def test_boot_cancel_is_volatile_and_does_not_touch_restart_arm(self):
        (self.state / 'audio').write_text(f'previous {AUDIO}\n')
        self.run_shell('bt_boot_cancel')
        self.assertTrue((self.state / 'boot-cancel').exists())
        self.assertEqual((self.state / 'audio').read_text(), f'previous {AUDIO}\n')

    def test_c_api_scopes_boot_option_and_validates_helper_arguments(self):
        source = (ROOT / 'workspace/all/common/generic_bt.c').read_text()
        api = source[source.index('bool PLAT_bluetoothBootReconnectSupported()'):
                     source.index('bool PLAT_bluetoothDiagnosticsEnabled()')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define SYSTEM_PATH "/system"
#define R_OK 4
#define LOG_warn(...) (warnings++)
static const char* PLATFORM="tg5040";
static int installed, preference_on, helper_result, warnings, calls, last_timeout;
static char last_command[256];
static int access(const char* path,int mode) {
 assert(strcmp(path,"/system/etc/bluetooth/bt_reconnect.sh")==0 && mode==R_OK);
 return installed?0:-1;
}
static int bt_run_cmd_deadline(const char* cmd,char* out,size_t len,int timeout) {
 (void)out; (void)len;
 calls++; last_timeout=timeout;
 snprintf(last_command,sizeof(last_command),"%s",cmd);
 if(strstr(cmd,"boot_enabled")) return preference_on?0:1;
 return helper_result;
}
''' + api + r'''
int main(void) {
 assert(!PLAT_bluetoothBootReconnectEnabled());
 assert(calls==0);
 PLATFORM="tg5050";
 assert(!PLAT_bluetoothBootReconnectSupported());
 assert(PLAT_bluetoothBootReconnectEnable(true)==-1 && calls==0);
 PLATFORM="tg5040";
 installed=1; preference_on=0;
 assert(!PLAT_bluetoothBootReconnectEnabled() && calls==1);
 preference_on=1;
 assert(PLAT_bluetoothBootReconnectEnabled());
 assert(last_timeout==2 && strstr(last_command,"boot_enabled"));
 assert(PLAT_bluetoothBootReconnectEnable(true)==0);
 assert(last_timeout==30 && strstr(last_command,"boot_set on"));
 assert(PLAT_bluetoothBootReconnectEnable(false)==0);
 assert(last_timeout==30 && strstr(last_command,"boot_set off"));
 int before=calls;
 bt_remember_boot("AA:BB:CC:DD:EE:FF;touch BAD");
 bt_forget_boot(NULL);
 assert(calls==before);
 bt_remember_boot("AA:BB:CC:DD:EE:FF");
 assert(calls==before+1 && last_timeout==10 && strstr(last_command,"remember_boot AA:BB:CC:DD:EE:FF"));
 bt_forget_boot("AA:BB:CC:DD:EE:FF");
 assert(calls==before+2 && last_timeout==20 && strstr(last_command,"forget_boot AA:BB:CC:DD:EE:FF"));
 preference_on=0; before=calls;
 bt_cancel_boot();
 assert(calls==before+1); // Only the enabled query was needed.
 preference_on=1; helper_result=1; before=calls;
 bt_cancel_boot();
 assert(calls==before+2 && warnings==1); // Failure is reported, manual work can proceed.
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_settings_boot_save_status_wins_over_stale_pair_failure(self):
        source = (ROOT / 'workspace/all/settings/settings_bt.c').read_text()
        thread = source[source.index('static void* bt_boot_thread('):source.index('static int bt_get_boot(')]
        getter = source[source.index('static int bt_get_boot('):source.index('static void bt_set_boot(int val);')]
        setter = source[source.index('static void bt_set_boot(int val) {'):
                        source.index('static void bt_pair_worker(')]
        tick = source[source.index('static void bt_on_tick(SettingsPage* page) {'):
                      source.index('// Page Creation', source.index('static void bt_on_tick(SettingsPage* page) {'))]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define BT_IDX_BOOT 2
#define BT_IDX_SCAN 3
#define ITEM_BUTTON 1
#define ITEM_STATIC 2
#define BT_PAIR_OK 0
#define BT_PAIR_FAILED -1
#define BT_PAIR_TRUST_FAILED -2
enum { BT_DISCOVERY_IDLE, BT_DISCOVERY_STARTING, BT_DISCOVERY_ACTIVE,
       BT_DISCOVERY_COMPLETE, BT_DISCOVERY_FAILED, BT_DISCOVERY_INTERRUPTED,
       BT_DISCOVERY_STOPPED };
typedef struct { int type; const char* desc; } SettingItem;
typedef struct {
 void* screen; int needs_layout, selected, dynamic_start, item_count;
 SettingItem items[4]; int status_in_description; const char* status_msg;
} SettingsPage;
static SettingsPage page={.screen=(void*)1,.dynamic_start=4,.item_count=4};
static volatile int bt_boot_value;
static struct { int val; volatile int done,busy,result; int handled; } bt_boot_ctx;
static struct { volatile int busy; } bt_toggle_ctx;
static struct { volatile int busy,done; } bt_action_ctx;
static volatile int bt_pair_result=BT_PAIR_OK;
static int bt_scan_lifecycle, bt_scan_requested, bt_scanner_failed;
static int sync_count, async_mode, save_result, enabled_value;
#define pthread_mutex_lock(x) ((void)(x))
#define pthread_mutex_unlock(x) ((void)(x))
static SettingsPage* settings_menu_current(void) { return &page; }
static int settings_page_visible_count(SettingsPage* p) { return p->item_count; }
static void settings_item_sync(SettingItem* item) { (void)item; sync_count++; }
static int BT_bootReconnectEnable(bool on) { (void)on; return save_result; }
static bool BT_bootReconnectEnabled(void) { return enabled_value; }
static int BT_discoveryState(void) { return BT_DISCOVERY_IDLE; }
static bool BT_enabled(void) { return false; }
static void settings_run_async(void* screen,const char* msg,void* (*worker)(void*),
                               void* arg,volatile int* done,volatile int* busy) {
 (void)screen; (void)msg; (void)arg;
 *done=0; *busy=0;
 if(async_mode==0) worker(NULL);
 if(async_mode==2) *busy=1;
}
''' + thread + getter + setter + tick + r'''
int main(void) {
 bt_action_ctx.done=1; bt_pair_result=BT_PAIR_FAILED;
 save_result=-1; async_mode=0;
 bt_set_boot(1);
 assert(bt_action_ctx.done==0 && sync_count==1);
 bt_on_tick(&page);
 assert(strcmp(page.status_msg,"Could not save boot reconnect setting. Retry.")==0 && page.status_in_description);
 assert(sync_count==2 && bt_boot_ctx.handled==1);
 async_mode=1; sync_count=0; bt_boot_ctx.result=0;
 bt_set_boot(0); bt_on_tick(&page);
 assert(bt_boot_ctx.done && bt_boot_ctx.result==-1 && page.status_in_description);
 assert(sync_count==2);
 async_mode=2; save_result=0; enabled_value=1; sync_count=0;
 bt_boot_ctx.done=0; bt_set_boot(1);
 assert(bt_boot_ctx.busy && !bt_boot_ctx.done);
 bt_boot_thread(NULL); // Completion after the user has stopped waiting.
 assert(bt_get_boot()==1 && !bt_boot_ctx.busy && bt_boot_ctx.done);
 bt_on_tick(&page);
 assert(bt_boot_ctx.handled && sync_count==2);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_boot_worker_rechecks_preference_and_cancellation(self):
        self.preference.write_text(f'1 {AUDIO}\n')
        body = r'''
bt_ctl() {
 case "$2" in
  show) printf 'Powered: yes\n';;
  info) printf 'Connected: no\nBonded: yes\nTrusted: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n';;
 esac
}
bt_run_bounded() { printf 'boolean true\n'; }
sleep() { :; }
bt_worker_connect() {
 printf '%s\n' "$1" >> "$TEST_ROOT/connects"
 case "${CANCEL_AFTER_FIRST:-}" in
  off) printf '0 AA:BB:CC:DD:EE:FF\n' > "$BT_BOOT_FILE";;
  manual) : > "$BT_RECONNECT_STATE/boot-cancel";;
 esac
 return 1
}
bt_reconnect_audio current AA:BB:CC:DD:EE:FF daemon:1 boot
'''
        self.run_shell(body)
        self.assertEqual((self.root / 'connects').read_text().splitlines(), [AUDIO, AUDIO])
        for mode in ('off', 'manual'):
            with self.subTest(mode=mode):
                (self.root / 'connects').unlink()
                (self.state / 'boot-cancel').unlink(missing_ok=True)
                self.preference.write_text(f'1 {AUDIO}\n')
                self.run_shell(f'CANCEL_AFTER_FIRST={mode}\n' + body)
                self.assertEqual((self.root / 'connects').read_text().splitlines(), [AUDIO])

    def test_boot_connect_skips_other_audio_and_controller_does_not_block(self):
        self.preference.write_text(f'1 {AUDIO}\n')
        body = r'''
BT_RECONNECT_TOKEN=current
BT_BOOT_MODE=1
BT_RECONNECT_DAEMON_IDENTITY=daemon:1
bt_set_deadline 30
bt_daemon_identity() { BT_DAEMON_IDENTITY=daemon:1; }
bt_ctl() {
 case "$2 $3" in
  'devices Connected') printf 'Device 22:33:44:55:66:77 Other\n';;
  'info 22:33:44:55:66:77')
   if [ "$OTHER_KIND" = audio ]; then
    printf 'Connected: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n'
   else
    printf 'Connected: yes\nUUID: Human Interface Device (00001812-0000-1000-8000-00805f9b34fb)\n'
   fi;;
  'connect AA:BB:CC:DD:EE:FF') touch "$TEST_ROOT/connect-ran";;
 esac
}
bt_worker_connect AA:BB:CC:DD:EE:FF
'''
        self.run_shell('OTHER_KIND=controller\n' + body)
        self.assertTrue((self.root / 'connect-ran').exists())
        (self.root / 'connect-ran').unlink()
        self.run_shell('OTHER_KIND=audio\n' + body.replace('bt_worker_connect AA:BB:CC:DD:EE:FF',
                                                           '! bt_worker_connect AA:BB:CC:DD:EE:FF'))
        self.assertFalse((self.root / 'connect-ran').exists())

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux /proc identities')
    def test_bounded_child_and_watchdog_do_not_inherit_preference_locks(self):
        bin_dir = self.root / 'bin'
        bin_dir.mkdir()
        busybox = bin_dir / 'busybox'
        busybox.write_text('#!/bin/sh\n'
                           'for fd in 7 8 9; do\n'
                           '  test ! -e "/proc/self/fd/$fd" || touch "$TEST_ROOT/inherited-lock"\n'
                           'done\n'
                           'test "$1" = sleep || exit 127\n'
                           'shift; exec /bin/sleep "$@"\n')
        busybox.chmod(0o755)
        env = dict(self.env, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'])
        body = r'''
. "$1"
exec 7>"$TEST_ROOT/preference-lock"
exec 8>"$TEST_ROOT/boot-lock"
exec 9>"$TEST_ROOT/lifecycle-lock"
bt_run_bounded 2 sh -c 'for fd in 7 8 9; do test ! -e "/proc/self/fd/$fd" || exit 99; done; sleep 0.2'
test "$?" -eq 0
test ! -e "$TEST_ROOT/inherited-lock"
'''
        subprocess.run(['sh', '-c', body, 'test', str(HELPER)], env=env,
                       check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
