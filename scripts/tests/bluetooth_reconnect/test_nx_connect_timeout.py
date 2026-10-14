"""Test checked-out connection cleanup behavior."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]


class ConnectTimeout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checkout = ROOT
        cls.source = (cls.checkout / 'workspace/all/common/generic_bt.c').read_text()

    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(source)
            subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'test'), str(path / 'test.c')], check=True)
            subprocess.run([str(path / 'test')], check=True, timeout=5)

    def test_cleanup_only_own_timed_out_disconnected_audio(self):
        helpers = self.source[self.source.index('// Match a complete bluetoothctl property line'):self.source.index('int PLAT_bluetoothPaired(')]
        connect = self.source[self.source.index('void PLAT_bluetoothConnect('):self.source.index('\nvoid PLAT_bluetoothDisconnect(')]
        stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define BT_CMD_TIMED_OUT (-2)
#define btlog(...) ((void)0)
#define LOG_error(...) ((void)0)
#define LOG_warn(...) ((void)0)
static char result_log[128];
#define LOG_info(...) snprintf(result_log,sizeof(result_log),__VA_ARGS__)
static int scenario, info_calls, cleanup_calls, connect_calls, cleanup_result;
static bool bt_cancel_boot(void) { return true; }
static void bt_remember_boot(char* addr) { (void)addr; }
static int bt_run_cmd(const char* cmd,char* info,size_t len) {
 assert(strstr(cmd,"info AA:BB:CC:DD:EE:FF")); info_calls++;
 if(scenario==3 || (scenario==8 && info_calls==2)) return -1;
 const char* connected=(scenario==7 || (scenario==1 && info_calls==2))?"yes":"no";
 const char* uuid=scenario==2?"00001124-0000-1000-8000-00805f9b34fb":"0000110b-0000-1000-8000-00805f9b34fb";
 snprintf(info,len,"Name: Connected: no Bonded: yes Trusted: yes\nConnected: %s\nBonded: %s\nTrusted: %s\nUUID: Audio Sink (%s)\n",connected,scenario==9?"no":"yes",scenario==10?"no":"yes",uuid);
 return 0;
}
static int bt_system(const char* cmd) {
 assert(strcmp(cmd,"bluetoothctl connect AA:BB:CC:DD:EE:FF 2>/dev/null")==0);connect_calls++;
 return scenario==4?1:scenario==5?0:scenario==6?-1:BT_CMD_TIMED_OUT;
}
static int bt_run_cmd_deadline(const char* cmd,char* output,size_t len,int timeout) {
 assert(strcmp(cmd,"bluetoothctl disconnect AA:BB:CC:DD:EE:FF 2>/dev/null")==0);
 assert(timeout==5 && !output && !len); cleanup_calls++;return cleanup_result;
}
'''
        main = r'''
int main(void) {
 char addr[]="AA:BB:CC:DD:EE:FF";
 for(scenario=0;scenario<=10;scenario++) {
  info_calls=cleanup_calls=connect_calls=0;cleanup_result=0;
  PLAT_bluetoothConnect(addr);
  assert(connect_calls==1);assert(cleanup_calls==(scenario==0));
  if(scenario==0) assert(strstr(result_log,"returned: -2"));
 }
 scenario=0; info_calls=cleanup_calls=0;cleanup_result=BT_CMD_TIMED_OUT;
 PLAT_bluetoothConnect(addr);assert(cleanup_calls==1);assert(strstr(result_log,"returned: -2"));
 info_calls=cleanup_calls=connect_calls=0;
 PLAT_bluetoothConnect("AA:BB:CC:DD:EE:FF;echo BAD");PLAT_bluetoothConnect(NULL);
 assert(!info_calls && !cleanup_calls && !connect_calls);
 assert(!bt_connect_cleanup_candidate("Name: Bonded: yes Trusted: yes Connected: no\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n"));
 assert(!bt_connect_cleanup_candidate("Bonded: yes\nTrusted: yes\nConnected: yes\nUUID: Audio Sink (0000110b-0000-1000-8000-00805f9b34fb)\n"));
 return 0;
}
'''
        self.compile_run(stub + helpers + connect + main)

    def test_executor_distinguishes_real_timeout_from_command_failure(self):
        executor = self.source[self.source.index('static int bt_run_cmd_deadline('):self.source.index('// Run a command with no output capture')]
        headers = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/wait.h>
#define LOG_error(...) ((void)0)
#define LOG_warn(...) ((void)0)
#define BT_CMD_TIMED_OUT (-2)
'''
        main = r'''
int main(void) {
 char output[32];
 assert(bt_run_cmd_deadline("exit 7",NULL,0,1)==7);
 assert(bt_run_cmd_deadline("printf ready",output,sizeof(output),1)==0);
 assert(strcmp(output,"ready")==0);
 assert(bt_run_cmd_deadline("sleep 4",NULL,0,1)==BT_CMD_TIMED_OUT);
 return 0;
}
'''
        self.compile_run(headers + executor + main)

if __name__ == '__main__':
    unittest.main()
