"""Behavioral tests compiling the checked-out discovery/session and Settings code."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
GENERIC = (ROOT / 'workspace/all/common/generic_bt.c').read_text()
SETTINGS = (ROOT / 'workspace/all/settings/settings_bt.c').read_text()
ENUM = '''enum { BT_DISCOVERY_IDLE, BT_DISCOVERY_STARTING, BT_DISCOVERY_ACTIVE,
BT_DISCOVERY_COMPLETE, BT_DISCOVERY_FAILED, BT_DISCOVERY_INTERRUPTED, BT_DISCOVERY_STOPPED };'''
LIST_LAYOUT = (ROOT / 'workspace/all/common/ui_list_layout.h').read_text()
HEADERS = '''#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/wait.h>
#define LOG_error(...) ((void)0)
#define LOG_warn(...) ((void)0)
'''


class DiscoveryRegression(unittest.TestCase):
    def run_c(self, code, cli=None, cli_c=None):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            if cli:
                tool = path / 'bluetoothctl'
                tool.write_text(cli)
                tool.chmod(0o755)
            if cli_c:
                (path / 'bluetoothctl.c').write_text(cli_c)
                subprocess.run(['cc', '-std=c99', '-Werror', '-o', str(path / 'bluetoothctl'),
                                str(path / 'bluetoothctl.c')], check=True)
            (path / 'test.c').write_text(code)
            subprocess.run(['cc', '-std=c99', '-Werror', '-pthread', '-o', str(path / 'test'),
                            str(path / 'test.c')], check=True)
            env = dict(os.environ, PATH=str(path) + ':' + os.environ['PATH'])
            subprocess.run([str(path / 'test')], check=True, env=env, cwd=path, timeout=20)

    def test_buffered_c_child_startup_and_foreign_scan_rejection(self):
        globals_ = GENERIC[GENERIC.index('// Serialize session start/stop'):
                           GENERIC.index('static volatile bool bt_initialized')]
        session = GENERIC[GENERIC.index('static bool bt_property_yes(const char* output, const char* property);'):
                          GENERIC.index('\nint PLAT_bluetoothScan(')]
        properties = GENERIC[GENERIC.index('// Match a complete bluetoothctl property line'):
                             GENERIC.index('int PLAT_bluetoothPaired(')]
        harness = HEADERS + ENUM + r'''
int PLAT_bluetoothDiscoveryState(void);
static bool PLAT_bluetoothEnabled(void) {return true;}
static int query_calls;
static int bt_run_cmd_deadline(const char* cmd,char* out,size_t len,int timeout) {
 assert(strcmp(cmd,"bluetoothctl show 2>/dev/null")==0 && timeout==2);
 query_calls++; snprintf(out,len,"Discovering: yes\n");return 0;
}
''' + globals_ + session + properties + r'''
static void await_state(int expected) {
 for(int i=0;i<325;i++) {
  int actual=PLAT_bluetoothDiscoveryState();
  if(actual==expected)return;
  if(expected==BT_DISCOVERY_ACTIVE && actual==BT_DISCOVERY_FAILED)break;
  usleep(20000);
 }
 fprintf(stderr,"buffered C child: expected %d, observed %d\n",expected,PLAT_bluetoothDiscoveryState());
 assert(0);
}
int main(void) {
 setenv("SCENARIO","buffered",1);PLAT_bluetoothDiscovery(1);await_state(BT_DISCOVERY_ACTIVE);
 usleep(5200000);assert(PLAT_bluetoothDiscovering()); // Actual lifetime exceeds old5s startup cutoff.
 assert(query_calls>=5);PLAT_bluetoothDiscovery(0);
 setenv("SCENARIO","reject",1);query_calls=0;PLAT_bluetoothDiscovery(1);
 await_state(BT_DISCOVERY_FAILED);assert(!query_calls && !PLAT_bluetoothDiscovering());
 PLAT_bluetoothDiscovery(0);
 setenv("SCENARIO","unack",1);query_calls=0;PLAT_bluetoothDiscovery(1);
 await_state(BT_DISCOVERY_FAILED);assert(!query_calls && !PLAT_bluetoothDiscovering());
 PLAT_bluetoothDiscovery(0);return 0;
}
'''
        # Ordinary libc stdio: no fflush/setvbuf/direct write. This reproduces
        # BlueZ noninteractive printf: pipes block-buffer stdout, terminals
        # line-buffer it. A shell's printf builtin cannot catch this failure.
        cli_c = r'''
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv) {
 if(argc!=5 || strcmp(argv[1],"--timeout") || strcmp(argv[2],"60") ||
    strcmp(argv[3],"scan") || strcmp(argv[4],"on"))return 9;
 const char* scenario=getenv("SCENARIO");
 if(strcmp(scenario,"reject")==0) {printf("Failed to start discovery: rejected\n");return 1;}
 if(strcmp(scenario,"unack")==0)printf("Alias: Discovery started\n");
 else printf("Discovery started\n");
 sleep(15);return 0;
}
'''
        self.run_c(harness, cli_c=cli_c)

    def test_owned_acknowledgement_liveness_completion_and_stop(self):
        globals_ = GENERIC[GENERIC.index('// Serialize session start/stop'):
                           GENERIC.index('static volatile bool bt_initialized')]
        session = GENERIC[GENERIC.index('static bool bt_property_yes(const char* output, const char* property);'):
                          GENERIC.index('\nint PLAT_bluetoothScan(')]
        properties = GENERIC[GENERIC.index('// Match a complete bluetoothctl property line'):
                             GENERIC.index('int PLAT_bluetoothPaired(')]
        # Accelerate only the monotonic clock, not subprocess handling, acknowledgement,
        # polling, ownership, deadline decisions or cleanup. CLI still gets exactly60.
        session = session.replace('clock_gettime(CLOCK_MONOTONIC, &now);', 'test_clock(&now);')
        harness = HEADERS + ENUM + r'''
static bool powered = true;
static int query_result, radio_yes = 1, query_calls;
static double speed = 1;
static struct timespec epoch;
int PLAT_bluetoothDiscoveryState(void);
static bool PLAT_bluetoothEnabled(void) { return powered; }
static void test_clock(struct timespec* now) {
 struct timespec real; clock_gettime(CLOCK_MONOTONIC,&real);
 double elapsed=(real.tv_sec-epoch.tv_sec)+(real.tv_nsec-epoch.tv_nsec)/1e9;
 double scaled=elapsed*speed; now->tv_sec=(time_t)scaled; now->tv_nsec=(long)((scaled-now->tv_sec)*1e9);
}
static int bt_run_cmd_deadline(const char* cmd,char* out,size_t len,int timeout) {
 assert(strcmp(cmd,"bluetoothctl show 2>/dev/null")==0 && timeout==2);
 query_calls++; snprintf(out,len,"Alias: Discovering: yes\nDiscovering: %s\n",radio_yes?"yes":"no");
 return query_result;
}
''' + globals_ + session + properties + r'''
static void start(const char* scenario,double scale) {
 PLAT_bluetoothDiscovery(0); setenv("SCENARIO",scenario,1);
 query_result=0; radio_yes=1; query_calls=0; speed=scale;
 clock_gettime(CLOCK_MONOTONIC,&epoch); PLAT_bluetoothDiscovery(1);
}
static void await_state(int wanted) {
 for(int i=0;i<100;i++) {
  if(PLAT_bluetoothDiscoveryState()==wanted) return;
  usleep(20000);
 }
 fprintf(stderr,"wanted%d got%d\n",wanted,PLAT_bluetoothDiscoveryState()); assert(0);
}
int main(void) {
 start("unack",100); await_state(BT_DISCOVERY_FAILED);
 assert(!query_calls && !PLAT_bluetoothDiscovering()); // Foreign radio yes isn't our success.
 start("reject",1); await_state(BT_DISCOVERY_FAILED);
 start("early",1); await_state(BT_DISCOVERY_ACTIVE); await_state(BT_DISCOVERY_INTERRUPTED);
 start("hold",1); await_state(BT_DISCOVERY_ACTIVE);
 assert(PLAT_bluetoothDiscovering()); int calls=query_calls;
 PLAT_bluetoothDiscovery(1); usleep(300000); assert(query_calls==calls); // No overlap;1s query cadence.
 radio_yes=0; await_state(BT_DISCOVERY_INTERRUPTED);
 start("hold",1); await_state(BT_DISCOVERY_ACTIVE);
 query_result=-1; await_state(BT_DISCOVERY_INTERRUPTED); // Unknown is never active.
 start("complete",100); await_state(BT_DISCOVERY_COMPLETE); // Normal child exit around60s.
 start("hold",100); await_state(BT_DISCOVERY_COMPLETE); // Parent's hard60s cap also bounds wedged CLI.
 start("hold",1); await_state(BT_DISCOVERY_ACTIVE);
 pid_t foreign=fork(); if(foreign==0) { setpgid(0,0); int nullfd=open("/dev/null",O_WRONLY);dup2(nullfd,STDOUT_FILENO);close(nullfd); execlp("bluetoothctl","bluetoothctl","--timeout","60","scan","on",(char*)NULL); _exit(127); }
 usleep(100000);
 PLAT_bluetoothDiscovery(0); assert(PLAT_bluetoothDiscoveryState()==BT_DISCOVERY_STOPPED);
 assert(kill(foreign,0)==0); kill(-foreign,SIGKILL); waitpid(foreign,NULL,0);
 pthread_mutex_lock(&bt_discovery_ops); bt_adapter_changing=true; pthread_mutex_unlock(&bt_discovery_ops);
 PLAT_bluetoothDiscovery(1); assert(PLAT_bluetoothDiscoveryState()==BT_DISCOVERY_STOPPED);
 bt_adapter_changing=false; powered=false; PLAT_bluetoothDiscovery(1);
 assert(PLAT_bluetoothDiscoveryState()==BT_DISCOVERY_STOPPED);
 return 0;
}
'''
        cli = '''#!/bin/sh
[ "$1 $2 $3 $4" = "--timeout 60 scan on" ] || exit 9
case "$SCENARIO" in
unack) sleep 10;;
reject) printf 'Failed to start discovery: rejected\n'; exit 0;;
early) printf 'Discovery started\n'; sleep .3;;
complete) printf 'Discovery started\n'; sleep .58;;
hold) printf 'Discovery started\n'; sleep 10;;
esac
'''
        self.run_c(harness, cli)

    def test_exit_is_bounded_even_when_bluez_property_query_hangs(self):
        globals_ = GENERIC[GENERIC.index('// Serialize session start/stop'):
                           GENERIC.index('static volatile bool bt_initialized')]
        executor = GENERIC[GENERIC.index('#define BT_CMD_TIMED_OUT'):
                           GENERIC.index('// Run a command with no output capture')]
        session = GENERIC[GENERIC.index('static bool bt_property_yes(const char* output, const char* property);'):
                          GENERIC.index('\nint PLAT_bluetoothScan(')]
        properties = GENERIC[GENERIC.index('// Match a complete bluetoothctl property line'):
                             GENERIC.index('int PLAT_bluetoothPaired(')]
        harness = HEADERS + ENUM + r'''
int PLAT_bluetoothDiscoveryState(void);
static bool PLAT_bluetoothEnabled(void) {return true;}
''' + globals_ + executor + session + properties + r'''
int main(void) {
 PLAT_bluetoothDiscovery(1);
 for(int i=0;i<100 && access("query.pid",F_OK)!=0;i++)usleep(20000);
 assert(access("query.pid",F_OK)==0);
 double begin=bt_discovery_now(); PLAT_bluetoothDiscovery(0);
 assert(bt_discovery_now()-begin<2.3);
 assert(PLAT_bluetoothDiscoveryState()==BT_DISCOVERY_STOPPED);
 FILE* f=fopen("scan.pid","r");int pid;assert(f && fscanf(f,"%d",&pid)==1);fclose(f);
 assert(kill(pid,0)==-1 && errno==ESRCH);
 return 0;
}
'''
        cli = '#!/bin/sh\nif [ "$1" = show ]; then echo $$ > query.pid; sleep 10; else echo $$ > scan.pid; printf "Discovery started\\n"; sleep 10; fi\n'
        self.run_c(harness, cli)

    def test_settings_one_session_retry_status_and_exit(self):
        globals_ = SETTINGS[SETTINGS.index('static pthread_t bt_scanner_thread;'):
                            SETTINGS.index('// ============================================\n// BT device options submenu')]
        info = SETTINGS[SETTINGS.index('typedef struct {'):SETTINGS.index('// ============================================\n// Static items')]
        scanner = SETTINGS[SETTINGS.index('// Interruptible sleep:'):
                            SETTINGS.index('// ============================================\n// Page Creation')]
        callback = SETTINGS[SETTINGS.index('// Scan again queues'):
                             SETTINGS.index('// ============================================\n// Device action callbacks')]
        toggle = SETTINGS[SETTINGS.index('static void* bt_toggle_thread('):
                          SETTINGS.index('\nstatic int bt_get_toggle(')]
        harness = HEADERS + ENUM + r'''
#define BT_STATIC_COUNT 4
#define BT_IDX_TOGGLE 0
#define BT_IDX_DIAG 1
#define BT_IDX_SCAN 3
#define BT_IDX_BOOT 2
#define BT_MAX_ITEMS 16
#define ITEM_BUTTON 1
#define ITEM_STATIC 2
#define BT_PAIR_OK 0
#define BT_PAIR_FAILED -1
#define BT_PAIR_TRUST_FAILED -2
#define BT_PAIR_CONNECT_FAILED -3
typedef int BluetoothDeviceType;
#define BLUETOOTH_NONE 0
struct BT_devicePaired {char remote_name[249],remote_addr[18];int is_bonded,is_connected;};
struct BT_device {char name[249],addr[18];int kind;};
typedef struct SettingItem {const char* name;const char* desc;int type,visible;void(*on_press)(void);void(*custom_draw)(void);void* user_data;} SettingItem;
typedef struct SettingsPage {SettingItem* items;int item_count,dynamic_start,max_items,selected,needs_layout,status_in_description;pthread_rwlock_t lock;const char* status_msg;} SettingsPage;
static struct {int val; volatile int done,busy;} bt_toggle_ctx;
static volatile int bt_boot_value;
static struct {int val; volatile int done,busy,result; int handled;} bt_boot_ctx;
static struct {volatile int done;} bt_action_ctx;
static int bt_pair_result;
static volatile int state=BT_DISCOVERY_IDLE, powered=1, starts,stops,refreshes,block_inventory,insert_device;
static int fail_create;
static int test_create(pthread_t* t,const pthread_attr_t* a,void*(*fn)(void*),void* arg) {
 if(fail_create) { fail_create=0; return EAGAIN; } return pthread_create(t,a,fn,arg);
}
#define pthread_create test_create
static void* bt_scanner(void*);
static int BT_discoveryState(void) {return state;}
static int BT_enabled(void) {return powered;}
static int BT_bootReconnectEnabled(void) {return bt_boot_value;}
static void BT_init(void) {}
static void BT_enable(bool on) {powered=on;state=BT_DISCOVERY_STOPPED;}
static void BT_discovery(int on) {if(on){starts++;state=BT_DISCOVERY_STARTING;}else{stops++;state=BT_DISCOVERY_STOPPED;}}
static void settings_item_sync(SettingItem* i) {(void)i;}
static void settings_scanner_sleep(int secs,volatile int* run,volatile int* wake) {
 for(int i=0;i<3 && *run && !*wake;i++) usleep(20000);
}
static int BT_pairedDevices(struct BT_devicePaired* p,int max) {
 refreshes++; while(block_inventory) usleep(20000); return 0;
}
static int BT_availableDevices(struct BT_device* p,int max) {
 if(insert_device) {
  snprintf(p[0].name,sizeof(p[0].name),"New device"); snprintf(p[0].addr,sizeof(p[0].addr),"11:22:33:44:55:66");
 }
 int old=insert_device?1:0;
 snprintf(p[old].name,sizeof(p[old].name),"Controller"); snprintf(p[old].addr,sizeof(p[old].addr),"AA:BB:CC:DD:EE:FF"); return old+1;
}
static int settings_page_visible_count(SettingsPage* p) {
 int count=0;for(int i=0;i<p->item_count;i++)count+=p->items[i].visible;return count;
}
static SettingItem* settings_page_visible_item(SettingsPage* p,int i) {
 for(int j=0;j<p->item_count;j++)if(p->items[j].visible && i--==0)return &p->items[j];return NULL;
}
static int settings_page_actual_to_visible(SettingsPage* p,int i) {
 int count=0;for(int j=0;j<p->item_count;j++){if(!p->items[j].visible)continue;if(j==i)return count;count++;}return -1;
}
static void bt_device_press(void) {}
static void bt_device_draw(void) {}
''' + info + globals_ + toggle + callback + scanner + r'''
static void tick(SettingsPage* page) {pthread_rwlock_rdlock(&page->lock);bt_on_tick(page);pthread_rwlock_unlock(&page->lock);}
static void await_starts(int wanted) {for(int i=0;i<100 && starts<wanted;i++)usleep(20000);assert(starts==wanted);}
static void await_items(SettingsPage* p,int count) {for(int i=0;i<100 && p->item_count!=count;i++)usleep(20000);assert(p->item_count==count);}
int main(void) {
 SettingItem items[BT_MAX_ITEMS]={0}; SettingsPage page={.items=items,.dynamic_start=4,.item_count=4,.max_items=BT_MAX_ITEMS};
 for(int i=0;i<3;i++)items[i].visible=1;
 items[3].type=ITEM_BUTTON;
 pthread_rwlock_init(&page.lock,NULL); bt_on_show(&page); await_starts(1);
 state=BT_DISCOVERY_ACTIVE; tick(&page);
 assert(strcmp(page.status_msg,"Scanning for devices...")==0 && !page.status_in_description && !items[3].visible);
 await_items(&page,5);
 page.selected=3; // First device is visible index 3 while the scan row is hidden.
 insert_device=1;bt_needs_refresh=1;await_items(&page,6);
 assert(page.selected==4 && strcmp(((BtDeviceInfo*)items[5].user_data)->addr,"AA:BB:CC:DD:EE:FF")==0);
 insert_device=0;bt_needs_refresh=1;await_items(&page,5);
 assert(page.selected==3);
 bt_scan_again(); assert(!bt_scan_requested);
 state=BT_DISCOVERY_COMPLETE; usleep(200000); assert(starts==1 && refreshes>1);
 tick(&page); assert(strcmp(page.status_msg,"Scan complete")==0 && !page.status_in_description && items[3].visible);
 assert(page.item_count==5); // Cached result survives completion and new scan.
 page.selected=4; // Keep the device selected as the scan row appears and disappears.
 bt_scan_again(); bt_scan_again(); await_starts(2); assert(page.item_count==5);
 tick(&page); assert(strcmp(page.status_msg,"Starting scan...")==0 && !page.status_in_description && !items[3].visible && page.selected==3);
 state=BT_DISCOVERY_COMPLETE; tick(&page); assert(items[3].visible && page.selected==4);
 state=BT_DISCOVERY_ACTIVE; bt_action_ctx.done=1;bt_pair_result=BT_PAIR_FAILED;
 tick(&page); assert(strstr(page.status_msg,"Pairing failed") && page.status_in_description);
 bt_action_ctx.done=0; tick(&page);
 assert(strcmp(page.status_msg,"Scanning for devices...")==0 && !page.status_in_description);
 bt_action_ctx.done=0; state=BT_DISCOVERY_COMPLETE;
 pthread_rwlock_wrlock(&page.lock);page.item_count=4;bt_on_tick(&page);
 assert(strcmp(page.status_msg,"No devices found. Put your device in pairing mode and scan again.")==0 && page.status_in_description);
 pthread_rwlock_unlock(&page.lock);
 state=BT_DISCOVERY_FAILED; tick(&page);
 assert(strcmp(page.status_msg,"Could not start scan. Select Scan again to retry.")==0 && page.status_in_description);
 bt_scan_again(); tick(&page);
 assert(strcmp(page.status_msg,"Starting scan...")==0 && !page.status_in_description); await_starts(3);
 state=BT_DISCOVERY_INTERRUPTED; tick(&page);
 assert(strcmp(page.status_msg,"Scan stopped early. Select Scan again to retry.")==0 && page.status_in_description);
 state=BT_DISCOVERY_ACTIVE; tick(&page);
 assert(strcmp(page.status_msg,"Scanning for devices...")==0 && !page.status_in_description);
 // Enable completion queues a scan even when the inventory worker missed Off.
 bt_toggle_ctx.val=1; bt_toggle_thread(NULL); await_starts(4);
 block_inventory=1; usleep(100000); int before=starts;
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);bt_on_hide(&page);clock_gettime(CLOCK_MONOTONIC,&end);
 assert((end.tv_sec-begin.tv_sec)+(end.tv_nsec-begin.tv_nsec)/1e9 < .1);
 assert(!bt_scanner_running && !bt_scan_requested && stops);
 bt_scan_again(); assert(starts==before && !bt_scan_requested);
 block_inventory=0; pthread_join(bt_scanner_thread,NULL);bt_scanner_started=0;
 fail_create=1;bt_on_show(&page);assert(bt_scanner_failed && !bt_scan_requested && !bt_scanner_started);
 tick(&page);assert(items[3].visible && items[3].type==ITEM_BUTTON);
 bt_scan_again(); await_starts(before+1);assert(!bt_scanner_failed);
 bt_on_hide(&page);pthread_join(bt_scanner_thread,NULL);
 return 0;
}
'''
        self.run_c(harness)

    def test_status_render_routing_with_full_and_scrolled_device_list(self):
        menu = (ROOT / 'workspace/all/settings/settings_menu.c').read_text()
        render = menu[menu.index('static void render_settings_mode('):
                      menu.index('// ============================================\n// Button Hint Bar helpers')]
        harness = HEADERS + r'''
#define ITEM_CYCLE 1
#define ITEM_COLOR 2
#define ITEM_STATIC 3
#define ITEM_TEXT_INPUT 4
typedef void SDL_Surface;
typedef struct {int unused;} ListLayout;
typedef struct SettingItem {const char* name;const char* desc;void* custom_draw;
 const char** labels;int current_idx,label_count,type;const char*(*get_display)(void);
 char display_text[128];const char*(*get_text)(void);char text_value[128];int* values;} SettingItem;
typedef struct SettingsPage {int item_count,selected,scroll,status_in_description;SettingItem* items;const char* status_msg;} SettingsPage;
typedef struct {const char*label;const char*value;int swatch,cycleable;const char*desc;
 void(*custom_draw)(void);void*custom_draw_ctx;} UISettingsItem;
static int renders;
static int settings_page_visible_count(SettingsPage*p){return p->item_count;}
static SettingItem* settings_page_visible_item(SettingsPage*p,int i){return &p->items[i];}
static void settings_custom_draw_wrapper(void){}
static const char* expected_desc;static const char* expected_status;
static void UI_renderSettingsPage(SDL_Surface*s,ListLayout*l,UISettingsItem*i,int count,int selected,int*scroll,const char*status){
 assert(count==14 && selected==12 && *scroll==9);
 assert(strcmp(i[selected].desc,expected_desc)==0);
 assert(status==expected_status);renders++;
}
''' + render + r'''
int main(void){
 SettingItem items[14];for(int i=0;i<14;i++)items[i]=(SettingItem){.name="Device",.desc="Normal description"};
 const char* messages[]={"Scanning for devices...","Scan complete","No devices found. Put your device in pairing mode and scan again.","Pairing failed. Put device in pairing mode and retry.","Could not save boot reconnect setting. Retry."};
 const int in_description[]={0,0,1,1,1};
 SettingsPage page={.items=items,.item_count=14,.selected=12,.scroll=9,.status_in_description=1};ListLayout layout;
 for(int i=0;i<5;i++){
  page.status_msg=messages[i];page.status_in_description=in_description[i];
  expected_desc=in_description[i]?messages[i]:"Normal description";
  expected_status=in_description[i]?NULL:messages[i];
  render_settings_mode(NULL,&page,&layout);
 }
 // A routine state after an error must restore the selected row's own help.
 page.status_msg=messages[0];page.status_in_description=0;
 expected_desc="Normal description";expected_status=messages[0];render_settings_mode(NULL,&page,&layout);
 assert(renders==6);return 0;
}
'''
        self.run_c(harness)

    def test_completed_device_list_keeps_titled_settings_layout(self):
        menu = (ROOT / 'workspace/all/settings/settings_menu.c').read_text()
        render = menu[menu.index('void settings_menu_render('):]
        harness = HEADERS + LIST_LAYOUT + r'''
typedef int IndicatorType;
typedef void SDL_Surface;
typedef struct {int titled;} ListLayout;
typedef struct SettingsPage {const char* title;int dynamic_start,is_list,top_aligned;pthread_rwlock_t lock;} SettingsPage; // top_aligned is a legacy regression sentinel, not a production field.
static SettingsPage page;
static int title_calls,hint_calls,settings_calls;
static int captured_titled;
static void assert_shared_wireless_layout(void){
 UIListBlock wifi=UI_optionsBlock(57,702,67,2+2,26,-1);
 UIListBlock bt=UI_optionsBlock(57,702,67,3+2,26,-1);
 assert(wifi.rows==4 && wifi.strip==0 && bt.rows==5 && bt.strip==0);
 int wifi_top_gap=wifi.top-57,wifi_bottom_gap=702-(wifi.top+wifi.rows*67);
 int bt_top_gap=bt.top-57,bt_bottom_gap=702-(bt.top+bt.rows*67);
 assert(wifi_top_gap-wifi_bottom_gap<=1 && wifi_bottom_gap-wifi_top_gap<=1);
 assert(bt_top_gap-bt_bottom_gap<=1 && bt_bottom_gap-bt_top_gap<=1);
 assert(wifi.top!=bt.top); // Each page centers its own row count.
 wifi=UI_optionsBlock(57,702,67,2+12,26,-1);
 bt=UI_optionsBlock(57,702,67,3+12,26,-1);
 assert(wifi.rows==8 && bt.rows==8 && wifi.strip==26 && bt.strip==26);
}
static SettingsPage* settings_menu_current(void){return &page;}
static void GFX_clear(SDL_Surface*s){(void)s;}
static void UI_renderMenuBar(SDL_Surface*s,const char*t){(void)s;assert(strcmp(t,"Bluetooth")==0);title_calls++;}
static ListLayout UI_calcListLayout(SDL_Surface*s){(void)s;return (ListLayout){.titled=1};}
static void render_list_mode(SDL_Surface*s,SettingsPage*p,ListLayout*l){(void)s;(void)p;(void)l;assert(0);}
static void render_settings_mode(SDL_Surface*s,SettingsPage*p,ListLayout*l){(void)s;(void)p;captured_titled=l->titled;assert_shared_wireless_layout();settings_calls++;}
static void render_hints_for_page(SDL_Surface*s,SettingsPage*p){(void)s;(void)p;hint_calls++;}
''' + render + r'''
int main(void){
 page=(SettingsPage){.title="Bluetooth",.dynamic_start=0,.top_aligned=0};
 assert(pthread_rwlock_init(&page.lock,NULL)==0);
 settings_menu_render(NULL,0);
 assert(captured_titled && title_calls==1 && hint_calls==1 && settings_calls==1);
 // Simulate the former completion state: discovered device rows can make
 // the page's layout sentinel true, but the titled shared layout stays active.
 page.top_aligned=1;
 settings_menu_render(NULL,0);
 assert(captured_titled && title_calls==2 && hint_calls==2 && settings_calls==2);
 pthread_rwlock_destroy(&page.lock);return 0;
}
'''
        self.run_c(harness)

    def test_radio_sessions_never_clear_cached_inventory_and_status_uses_wrapped_area(self):
        session = GENERIC[GENERIC.index('static void* bt_discovery_monitor('):
                          GENERIC.index('\nint PLAT_bluetoothScan(')]
        self.assertNotIn('bt_clear_discovered_devices', session)
        self.assertNotIn('bluetoothctl remove', session)
        self.assertNotIn('pgrep', session)
        self.assertNotIn('scan off', session)
        menu = (ROOT / 'workspace/all/settings/settings_menu.c').read_text()
        self.assertIn('page->status_in_description && page->status_msg', menu)
        self.assertIn('page->status_in_description ? NULL : page->status_msg', menu)
        self.assertIn('page->status_in_description = 1;', SETTINGS)
        renderer = (ROOT / 'workspace/all/common/ui/ui_list.c').read_text()
        self.assertIn('GFX_wrapText(font.tiny, wrapped, desc_max_w, 2)', renderer)


if __name__ == '__main__':
    unittest.main()
