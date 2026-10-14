"""Compile checked-out Bluetooth functions against deterministic BlueZ responses."""
import os
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
enum { BT_PAIR_OK=0, BT_PAIR_TEMPORARY=1, BT_PAIR_FAILED=-1, BT_PAIR_TRUST_FAILED=-2, BT_PAIR_CONNECT_FAILED=-3 };
struct BT_devicePaired { char remote_addr[18], remote_name[249]; bool is_paired, is_bonded, is_connected, is_trusted; int rssi; };
static int scenario, trust_calls, connect_calls, removed, pair_long_calls, connect_long_calls;
static bool pairable = true;
static int enable_calls, disable_calls;

static bool PLAT_bluetoothEnabled(void) { return true; }
static bool bt_version_gte(int a,int b) { return true; }
static bool bt_cancel_boot(void) { return true; }
static void bt_remember_boot(char* addr) { (void)addr; }
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
  else snprintf(out,len,"Paired: %s\nBonded: no\nConnected: yes\nTrusted: yes\n",scenario==3?"no":"yes");
  if(scenario==6) snprintf(out,len,"Paired: yes\nBonded: yes\nConnected: yes\n");
  return 0;
 }
 if (strstr(cmd,"trust")) { trust_calls++; if(scenario==4) snprintf(out,len,"Failed to trust"); return 0; }
 if (strstr(cmd,"connect")) { connect_calls++; if(scenario==5) return -1; return 0; }
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
 scenario=5; assert(PLAT_bluetoothPair(addr)==BT_PAIR_CONNECT_FAILED);
 scenario=0; pair_long_calls=connect_long_calls=0;
 assert(PLAT_bluetoothPair(addr)==BT_PAIR_TEMPORARY);
 assert(pair_long_calls==1 && connect_long_calls==1);
 scenario=6; assert(PLAT_bluetoothPair(addr)==BT_PAIR_OK);
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
  assert(result==(scenario==0?BT_PAIR_TEMPORARY:scenario<=3?BT_PAIR_FAILED:
                 scenario==4?BT_PAIR_TRUST_FAILED:scenario==5?BT_PAIR_CONNECT_FAILED:BT_PAIR_OK));
  assert(enable_calls==1 && disable_calls==1 && pair_long_calls==1 && !pairable);
 }
 scenario=0;
 struct BT_devicePaired devices[4]; memset(devices,0xff,sizeof(devices));
 assert(PLAT_bluetoothPaired(devices,4)==3);
 assert(devices[0].is_paired && !devices[0].is_bonded && devices[0].is_connected);
 assert(!devices[1].is_paired && !devices[1].is_bonded && !devices[1].is_connected && devices[1].is_trusted);
 assert(!devices[2].is_paired && !devices[2].is_bonded && devices[2].is_connected);
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

    def test_ui_state_and_fallback_contract(self):
        settings = (self.checkout / 'workspace/all/settings/settings_bt.c').read_text()
        self.assertIn('dinfo->paired = paired[i].is_paired;', settings)
        self.assertIn('dinfo->bonded = paired[i].is_bonded;', settings)
        self.assertIn('.name = "Pair again"', settings)
        self.assertIn('Pairing failed. Put device in pairing mode and retry.', settings)
        fallback = (self.checkout / 'workspace/all/common/api.c').read_text()
        self.assertIn('int PLAT_bluetoothPair(char* addr) { return BT_PAIR_FAILED; }', fallback)

if __name__ == '__main__':
    unittest.main()
