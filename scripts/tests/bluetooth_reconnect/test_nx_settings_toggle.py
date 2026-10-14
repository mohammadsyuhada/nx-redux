"""Exercise the Settings Off/On call sequence with the real reconnect helper."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from test_nx_toggle_reconnect import AUDIO, HELPER, INIT, ROOT, STUB

STALE = '22:33:44:55:66:77'
BOOT_TARGET = '33:44:55:66:77:88'


class SettingsToggle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        source = (ROOT / 'workspace/all/common/generic_bt.c').read_text()
        enable = source[source.index('void PLAT_bluetoothEnable(bool shouldBeOn) {'):
                        source.index('bool PLAT_bluetoothBootReconnectSupported()')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define SYSTEM_PATH "/fixture"
#define btlog(...) ((void)0)
static bool bt_discovering, saved_on;
static int bt_system(const char *cmd) {
    assert(strstr(cmd, "bluetoothctl power off") != NULL);
    return system("bluetoothctl power off");
}
static int fake_system(const char *cmd) {
    int result;
    if (strstr(cmd, "bt_init.sh stop")) {
        result = system(getenv("BT_TEST_STOP")); assert(result == 0); return result;
    }
    if (strstr(cmd, "bt_init.sh start")) {
        result = system(getenv("BT_TEST_START")); assert(result == 0); return result;
    }
    assert(0); return -1;
}
static void CFG_setBluetooth(bool on) { saved_on = on; }
#define system fake_system
''' + enable + r'''
int main(int argc, char **argv) {
    PLAT_bluetoothEnable(false);
    assert(!saved_on);
    if (argc > 1 && strcmp(argv[1], "cycle") == 0) {
        PLAT_bluetoothEnable(true);
        assert(saved_on);
    }
    return 0;
}
'''
        build = pathlib.Path(cls.build.name)
        (build / 'toggle.c').write_text(harness)
        cls.binary = build / 'toggle'
        subprocess.run(['cc', '-std=c99', '-Werror', '-DPLATFORM="tg5040"',
                        str(build / 'toggle.c'), '-o', str(cls.binary)], check=True)
        cls.tg5050 = build / 'toggle-5050'
        subprocess.run(['cc', '-std=c99', '-Werror', '-DPLATFORM="tg5050"',
                        str(build / 'toggle.c'), '-o', str(cls.tg5050)], check=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.state = self.root / 'state'
        self.state.mkdir()
        (self.state / 'token').write_text('123\n')
        (self.state / 'audio').write_text(STALE + '\n')
        self.userdata = self.root / 'userdata'
        self.userdata.mkdir()
        self.preference = self.userdata / 'bt-boot-audio'
        self.preference.write_text('1 ' + BOOT_TARGET + '\n')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        stub = STUB.replace("elif name=='bluetoothctl':", "elif name=='ps': print('bluetoothd')\nelif name=='killall': pass\nelif name=='bluetoothctl':")
        stub = stub.replace("if args[:1]==['show']: print('Name: Powered: yes\\nPowered: '+('no' if s.get('off') else 'yes'))",
                            "if args[:1]==['show']:\n  if s.get('fail_show'): sys.exit(1)\n  print('Name: Powered: yes\\nPowered: '+('no' if s.get('off') else 'yes'))")
        stub = stub.replace("elif args[:2]==['devices','Connected']:",
                            "elif args[:2]==['power','off']: s['off']=True; s['connected']=False; s['capture']=False\n elif args[:2]==['power','on']: s['off']=False\n elif args[:2]==['devices','Connected']:")
        for name in ['bluetoothctl', 'busybox', 'date', 'sleep', 'dbus-send', 'logger', 'ps', 'killall']:
            command = self.bin / name
            command.write_text(stub)
            command.chmod(0o755)
        helper = ROOT / HELPER
        init = (ROOT / INIT).read_text()
        start = init.index('stop_bt() {')
        stop = init.index('\t# The stock S80hciattach', start)
        # This is the actual stop_bt prefix through its adapter power-off call.
        stop_body = init[start:stop] + '}\nstop_bt\n'
        setup = ('. "$BT_TEST_HELPER"\nBT_RECONNECT_TOKEN=123\n'
                 'bt_now() { date +%s; }\n'
                 'bt_current_pid() { BT_CURRENT_PID=$$; }\n'
                 'bt_process_identity() { BT_PROCESS_IDENTITY=$1; }\n')
        self.stop = self.root / 'stop.sh'
        self.stop.write_text('#!/bin/sh\n' + setup + stop_body)
        self.stop.chmod(0o755)
        self.start = self.root / 'start.sh'
        self.start.write_text('#!/bin/sh\nbluetoothctl power on\n' + setup + 'bt_reconnect_audio 123\n')
        self.start.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        STUB_STATE=str(self.root), BT_RECONNECT_STATE=str(self.state),
                        USERDATA_PATH=str(self.userdata), BT_TEST_HELPER=str(helper),
                        BT_TEST_STOP=str(self.stop), BT_TEST_START=str(self.start))

    def run_case(self, *, cycle=False, platform='tg5040', **settings):
        (self.root / 'stub.json').write_text(json.dumps(settings))
        binary = self.binary if platform == 'tg5040' else self.tg5050
        subprocess.run([str(binary)] + (['cycle'] if cycle else []),
                       env=self.env, check=True, timeout=20)
        return (self.root / 'calls').read_text()

    def test_off_captures_current_sink_before_power_off_and_on_recalls_it(self):
        calls = self.run_case(cycle=True, capture=True, succeed_on=1)
        self.assertEqual((self.state / 'audio').read_text().strip(), AUDIO)
        self.assertLess(calls.index('bluetoothctl show'), calls.index('bluetoothctl power off'))
        self.assertIn('bluetoothctl connect ' + AUDIO, calls)
        self.assertNotIn('bluetoothctl connect ' + STALE, calls)
        self.assertEqual(self.preference.read_text(), '1 ' + BOOT_TARGET + '\n')

    def test_off_clears_stale_target_when_no_eligible_sink_remains(self):
        for condition in ({}, {'unbonded': True}, {'untrusted': True}, {'controller': True}):
            with self.subTest(condition=condition):
                (self.state / 'audio').write_text(STALE + '\n')
                self.run_case(capture=bool(condition), **condition)
                self.assertFalse((self.state / 'audio').exists())

    def test_failed_power_probe_keeps_previous_target(self):
        self.run_case(fail_show=True)
        self.assertEqual((self.state / 'audio').read_text().strip(), STALE)

    def test_tg5050_keeps_power_off_path_without_stop(self):
        calls = self.run_case(platform='tg5050', capture=True)
        self.assertIn('bluetoothctl power off', calls)
        self.assertNotIn('bluetoothctl show', calls)
        self.assertEqual((self.state / 'audio').read_text().strip(), STALE)
