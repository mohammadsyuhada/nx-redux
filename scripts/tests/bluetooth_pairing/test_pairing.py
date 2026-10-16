"""Compile checked-out Bluetooth functions against deterministic BlueZ responses."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]


class PairingRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checkout = ROOT

    def test_bluez_failures_and_device_states(self):
        source = (self.checkout / 'workspace/all/common/generic_bt.c').read_text()
        paired = source[source.index('// Match a complete bluetoothctl property line'):source.index('\nvoid PLAT_bluetoothUnpair(')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define btlog(...) ((void)0)
#define LOG_error(...) ((void)0)
#define LOG_warn(...) snprintf(timeout_warning, sizeof(timeout_warning), __VA_ARGS__)
#define BT_CMD_TIMED_OUT (-2)
enum { BT_PAIR_OK=0, BT_PAIR_FAILED=-1, BT_PAIR_TRUST_FAILED=-2, BT_PAIR_CONNECT_FAILED=-3 };
struct BT_devicePaired { char remote_addr[18], remote_name[249]; bool is_bonded, is_connected; int rssi; };
static int scenario, trust_calls, connect_calls, removed, pair_long_calls, connect_long_calls;
static char timeout_warning[256];
static int boot_cancel_calls, boot_remember_calls;
static void bt_cancel_boot(void) { boot_cancel_calls++; }
static void bt_remember_boot(const char* addr) { assert(strcmp(addr,"AA:BB:CC:DD:EE:FF")==0); boot_remember_calls++; }
static bool pairable = true;
static int enable_calls, disable_calls;

static bool PLAT_bluetoothEnabled(void) { return true; }
static bool bt_version_gte(int a,int b) { return true; }
static void bt_remove_discovered_device(char* addr) { removed++; }
static int bt_run_cmd(const char* cmd,char* out,size_t len) {
 out[0]=0;
 if (strstr(cmd,"bluetoothctl show")) {
  if(scenario==7 || (scenario==13 && enable_calls)) return -1;
  if(scenario==8) { snprintf(out,len,"Alias: Pairable: yes\n"); return 0; }
  snprintf(out,len,"Pairable: %s\n",pairable?"yes":"no"); return 0;
 }
 if (strstr(cmd,"pairable on")) {
  enable_calls++;
  if(scenario==12) { pairable=true; return -1; } // Applied before timeout.
  if(scenario==9) return -1;
  if(scenario==10) { snprintf(out,len,"Failed to set pairable"); return 0; }
  if(scenario!=11) pairable=true;
  return 0;
 }
 if (strstr(cmd,"pairable off")) { disable_calls++; pairable=false; return 0; }

 if (strstr(cmd,"devices Trusted")) { snprintf(out,len,"Device 77:88:99:AA:BB:CC SavedController\n"); return 0; }
 if (strstr(cmd,"devices Paired")) { snprintf(out,len,"Device AA:BB:CC:DD:EE:FF AirPods\n"); return 0; }
 if (strstr(cmd,"devices Connected")) { snprintf(out,len,"Device AA:BB:CC:DD:EE:FF AirPods\nDevice 11:22:33:44:55:66 Controller\n"); return 0; }
 if (strstr(cmd,"--agent")) {
  assert(pairable);
  if (scenario==1) return -1;
  if (scenario==2) snprintf(out,len,"Failed to pair: org.bluez.Error.AuthenticationFailed");
  return 0;
 }
 if (strstr(cmd,"info")) {
  if (strstr(cmd,"77:88")) snprintf(out,len,"Name: Paired: yes Bonded: yes Connected: yes\nAlias: Bonded: yes\nPaired: no\nBonded: no\nConnected: no\nTrusted: yes\n");
  else if (strstr(cmd,"11:22")) snprintf(out,len,"Paired: no\nBonded: no\nConnected: yes\nTrusted: yes\n");
  else snprintf(out,len,"Paired: %s\nBonded: yes\nConnected: yes\nTrusted: yes\n",scenario==3?"no":"yes");
  if(scenario==6) snprintf(out,len,"Paired: yes\nBonded: no\nConnected: yes\n");
  if(scenario==14) snprintf(out,len,"Paired: yes\nConnected: yes\n");
  return 0;
 }
 if (strstr(cmd,"trust")) { trust_calls++; if(scenario==4) snprintf(out,len,"Failed to trust"); return 0; }
 if (strstr(cmd,"connect")) { connect_calls++; if(scenario==5) return BT_CMD_TIMED_OUT; if(scenario==15) return -1; return 0; }
 assert(0); return -1;
}
static int bt_run_cmd_long(const char* cmd,char* out,size_t len) {
 if (strstr(cmd,"--agent")) pair_long_calls++;
 else if (strstr(cmd,"connect")) connect_long_calls++;
 else assert(0);
 return bt_run_cmd(cmd,out,len);
}
'''
        main = r'''
int main(void) {
 char addr[]="AA:BB:CC:DD:EE:FF";
 for(scenario=1;scenario<=3;scenario++) {
  trust_calls=connect_calls=removed=0;
  assert(PLAT_bluetoothPair(addr)==BT_PAIR_FAILED);
  assert(!trust_calls && !connect_calls && !removed);
 }
 scenario=4; assert(PLAT_bluetoothPair(addr)==BT_PAIR_TRUST_FAILED); assert(!connect_calls);
 scenario=5; timeout_warning[0]=0;
 assert(PLAT_bluetoothPair(addr)==BT_PAIR_CONNECT_FAILED);
 assert(connect_calls==1 && strstr(timeout_warning,"BlueZ request may still complete"));
 scenario=15; timeout_warning[0]=0;
 assert(PLAT_bluetoothPair(addr)==BT_PAIR_CONNECT_FAILED);
 assert(timeout_warning[0]==0);
 scenario=0; pair_long_calls=connect_long_calls=0;
 assert(PLAT_bluetoothPair(addr)==BT_PAIR_OK);
 assert(pair_long_calls==1 && connect_long_calls==1);
 assert(boot_remember_calls==1 && boot_cancel_calls>0);
 scenario=6; trust_calls=connect_calls=removed=0; assert(PLAT_bluetoothPair(addr)==BT_PAIR_FAILED); assert(!trust_calls && !connect_calls && !removed);
 assert(enable_calls==0 && disable_calls==0); // Already pairable: preserve state.
 for(scenario=7;scenario<=13;scenario++) {
  pairable=false; enable_calls=disable_calls=pair_long_calls=trust_calls=connect_calls=0;
  assert(PLAT_bluetoothPair(addr)==BT_PAIR_FAILED);
  assert(pair_long_calls==0 && trust_calls==0 && connect_calls==0);
  assert(!pairable);
  assert(enable_calls==(scenario>=9));
  assert(disable_calls==(scenario>=9));
 }
 for(scenario=0;scenario<=6;scenario++) {
  pairable=false; enable_calls=disable_calls=pair_long_calls=0;
  int result=PLAT_bluetoothPair(addr);
  assert(result==(scenario==0?BT_PAIR_OK:scenario<=3?BT_PAIR_FAILED:
                 scenario==4?BT_PAIR_TRUST_FAILED:scenario==5?BT_PAIR_CONNECT_FAILED:BT_PAIR_FAILED));
  assert(enable_calls==1 && disable_calls==1 && pair_long_calls==1 && !pairable);
 }
 scenario=14; trust_calls=connect_calls=removed=0;
 assert(PLAT_bluetoothPair(addr)==BT_PAIR_FAILED);
 assert(!trust_calls && !connect_calls && !removed); // Missing Bonded fails closed.
 scenario=0;
 struct BT_devicePaired devices[4]; memset(devices,0xff,sizeof(devices));
 assert(PLAT_bluetoothPaired(devices,4)==3);
 assert(devices[0].is_bonded && devices[0].is_connected);
 assert(!devices[1].is_bonded && !devices[1].is_connected);
 assert(!devices[2].is_bonded && devices[2].is_connected);
 assert(!bt_property_yes("Name: Paired: yes\nPaired: no\n", "Paired:"));
 assert(!bt_property_yes("\tBonded: yes please\nBonded: no\n", "Bonded:"));
 assert(bt_property_yes("\tBonded: yes\r\n", "Bonded:"));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(harness + paired + main)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'), str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_pair_command_wrapper_keeps_thirty_second_capture_and_liveness(self):
        source = (self.checkout / 'workspace/all/common/generic_bt.c').read_text()
        wrapper = source[source.index('static int bt_run_cmd_long('):
                         source.index('// Bluetoothctl version detection')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define LOG_warn(...) ((void)0)
#define BT_CMD_TIMED_OUT (-2)
static bool daemon_up = true;
static int deadline_calls, timeout_seen, result = -2;
static bool bt_daemon_alive(void) { return daemon_up; }
static int bt_run_cmd_deadline(const char* cmd, char* out, size_t len, int timeout) {
    assert(strstr(cmd, "bluetoothctl") && out && len > 1);
    deadline_calls++; timeout_seen = timeout;
    strcpy(out, "Failed to pair");
    return result;
}
''' + wrapper + r'''
int main(void) {
    char out[64] = "stale";
    assert(bt_run_cmd_long("bluetoothctl pair AA:BB:CC:DD:EE:FF", out, sizeof(out)) == -2);
    assert(deadline_calls == 1 && timeout_seen == 30);
    assert(strcmp(out, "Failed to pair") == 0);
    daemon_up = false;
    assert(bt_run_cmd_long("bluetoothctl connect AA:BB:CC:DD:EE:FF", out, sizeof(out)) == -1);
    assert(deadline_calls == 1 && out[0] == '\0');
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_timed_out_connect_issues_no_disconnect(self):
        source = (self.checkout / 'workspace/all/common/generic_bt.c').read_text()
        connect = source[source.index('void PLAT_bluetoothConnect('):
                         source.index('\nvoid PLAT_bluetoothDisconnect(')]
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define BT_CMD_TIMED_OUT (-2)
#define btlog(...) ((void)0)
#define LOG_info(...) ((void)0)
#define LOG_error(...) (errors++)
#define LOG_warn(...) snprintf(warning, sizeof(warning), __VA_ARGS__)
static int result, calls, errors;
static char warning[256];
static int boot_cancels, boot_remembers;
static void bt_cancel_boot(void) { boot_cancels++; }
static void bt_remember_boot(const char* addr) { assert(strcmp(addr,"AA:BB:CC:DD:EE:FF")==0); boot_remembers++; }
static int bt_run_cmd_long(const char* cmd, char* out, size_t len) {
 // A second call (including Disconnect) or a different target is forbidden.
 assert(strcmp(cmd,"bluetoothctl connect AA:BB:CC:DD:EE:FF 2>&1")==0);
 assert(out && len > 0);
 out[0] = 0;
 calls++;
 return result;
}
''' + connect + r'''
int main(void) {
 char addr[]="AA:BB:CC:DD:EE:FF";
 result=BT_CMD_TIMED_OUT;
 PLAT_bluetoothConnect(addr);
 assert(calls==1 && errors==1);
 assert(boot_cancels==1 && boot_remembers==0);
 assert(strstr(warning,"BlueZ request may still complete"));
 result=-1;
 warning[0]=0;
 PLAT_bluetoothConnect(addr);
 assert(calls==2 && errors==2 && warning[0]==0);
 result=0;
 PLAT_bluetoothConnect(addr);
 assert(calls==3 && errors==2 && warning[0]==0);
 assert(boot_cancels==3 && boot_remembers==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_pair_and_connect_really_allow_more_than_five_seconds(self):
        source = (self.checkout / 'workspace/all/common/generic_bt.c').read_text()
        executor = source[source.index('#define BT_CMD_TIMED_OUT'):
                          source.index('// Run a command with no output capture')]
        wrapper = source[source.index('static int bt_run_cmd_long('):
                         source.index('// Bluetoothctl version detection')]
        properties = source[source.index('// Match a complete bluetoothctl property line'):
                            source.index('int PLAT_bluetoothPaired(')]
        connect = source[source.index('void PLAT_bluetoothConnect('):
                         source.index('\nvoid PLAT_bluetoothDisconnect(')]
        pair = source[source.index('// BlueZ 5.78 ignores'):
                      source.index('\nvoid PLAT_bluetoothUnpair(')]
        harness = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/wait.h>
#define LOG_error(...) ((void)0)
#define LOG_warn(...) ((void)0)
#define LOG_info(...) ((void)0)
#define btlog(...) ((void)0)
enum { BT_PAIR_OK=0, BT_PAIR_FAILED=-1, BT_PAIR_TRUST_FAILED=-2, BT_PAIR_CONNECT_FAILED=-3 };
static bool bt_daemon_alive(void) { return true; }
static void bt_remove_discovered_device(char* addr) { (void)addr; }
static void bt_cancel_boot(void) {}
static void bt_remember_boot(const char* addr) { (void)addr; }
static int bt_run_cmd(const char* cmd, char* out, size_t len) {
 snprintf(out,len,"Pairable: yes\nPaired: yes\nBonded: yes\n"); return 0;
}
''' + executor + wrapper + properties + pair + connect + r'''
int main(void) {
 time_t started = time(NULL);
 assert(PLAT_bluetoothPair("AA:BB:CC:DD:EE:FF") == BT_PAIR_OK);
 assert(time(NULL) - started >= 12);
 started = time(NULL);
 PLAT_bluetoothConnect("AA:BB:CC:DD:EE:FF");
 assert(time(NULL) - started >= 6);
 char out[32];
 assert(bt_run_cmd_deadline("printf ready; sleep 4",out,sizeof(out),1) == BT_CMD_TIMED_OUT);
 assert(strcmp(out,"ready") == 0);
 assert(bt_run_cmd_deadline("exit 7",out,sizeof(out),3) == 7);
 char marker[]="/tmp/bt-timeout-child-XXXXXX";
 int marker_fd=mkstemp(marker);
 assert(marker_fd>=0);
 close(marker_fd);
 unlink(marker);
 char command[160];
 snprintf(command,sizeof(command),"(sleep 4; touch %s) &",marker);
 assert(bt_run_cmd_deadline(command,out,sizeof(out),1)==BT_CMD_TIMED_OUT);
 sleep(5);
 assert(access(marker,F_OK)!=0); // Timed-out descendant cannot outlive its shell.
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            # Both operations use the production output-capturing executor.
            cli = path / 'bluetoothctl'
            cli.write_text('#!/bin/sh\nsleep 6\nprintf "operation completed\\n"\n')
            cli.chmod(0o755)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            env = dict(__import__('os').environ, PATH=str(path) + ':' + __import__('os').environ['PATH'])
            subprocess.run([str(path / 'test')], check=True, env=env, timeout=35)

    def test_ui_state_and_fallback_contract(self):
        settings = (self.checkout / 'workspace/all/settings/settings_bt.c').read_text()
        self.assertIn('dinfo->paired = 1;', settings)
        self.assertIn('dinfo->bonded = paired[i].is_bonded;', settings)
        self.assertNotIn('Temporary', settings)
        self.assertNotIn('Pair again', settings)
        self.assertIn('info->bonded', settings)
        self.assertIn('Pairing failed. Put device in pairing mode and retry.', settings)
        fallback = (self.checkout / 'workspace/all/common/api.c').read_text()
        self.assertIn('int PLAT_bluetoothPair(char* addr) { return BT_PAIR_FAILED; }', fallback)

if __name__ == '__main__':
    unittest.main()
