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
static int scenario, trust_calls, connect_calls, removed;
static bool PLAT_bluetoothEnabled(void) { return true; }
static bool bt_version_gte(int a,int b) { return true; }
static bool bt_cancel_boot(void) { return true; }
static void bt_remember_boot(char* addr) { (void)addr; }
static void bt_remove_discovered_device(char* addr) { removed++; }
static int bt_run_cmd(const char* cmd,char* out,size_t len) {
 out[0]=0;
 if (strstr(cmd,"devices Trusted")) { snprintf(out,len,"Device 77:88:99:AA:BB:CC SavedController\n"); return 0; }
 if (strstr(cmd,"devices Paired")) { snprintf(out,len,"Device AA:BB:CC:DD:EE:FF AirPods\n"); return 0; }
 if (strstr(cmd,"devices Connected")) { snprintf(out,len,"Device AA:BB:CC:DD:EE:FF AirPods\nDevice 11:22:33:44:55:66 Controller\n"); return 0; }
 if (strstr(cmd,"--agent")) {
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
 scenario=0; assert(PLAT_bluetoothPair(addr)==BT_PAIR_TEMPORARY);
 scenario=6; assert(PLAT_bluetoothPair(addr)==BT_PAIR_OK);
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
