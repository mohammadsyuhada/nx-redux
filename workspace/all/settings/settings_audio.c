#include <stdio.h>
#include <stdlib.h>

#include "settings_audio.h"
#include "config.h"
#include "audio_manager.h"
#include "msettings.h"

#define AUDIO_IDX_OUTPUT 0
#define AUDIO_IDX_VOLUME 1
#define AUDIO_IDX_NEGOTIATION 2
#define AUDIO_IDX_BTRATE 3
#define AUDIO_ITEM_COUNT 4

static const char* neg_labels[] = {"Auto", "Force 48000 Hz"};
static int neg_values[] = {1, 0};
static const char* rate_labels[] = {"44100 Hz", "48000 Hz"};
static int rate_values[] = {44100, 48000};

static int volume_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static const char* volume_labels[] = {
	"Muted", "5%", "10%", "15%", "20%", "25%", "30%", "35%", "40%", "45%", "50%",
	"55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%"};
#define VOLUME_LABEL_COUNT 21

// Ask audiomon to re-probe and republish /tmp/nx_audio_sink with the new
// policy immediately, instead of waiting for the next hotplug event.
static void poke_audiomon(void) {
	system("killall -USR1 audiomon.elf 2>/dev/null");
}

static int audio_get_negotiation(void) {
	return CFG_getAudioRateNegotiation() ? 1 : 0;
}

static void audio_set_negotiation(int val) {
	CFG_setAudioRateNegotiation(val != 0);
	poke_audiomon();
}

static void audio_reset_negotiation(void) {
	audio_set_negotiation(CFG_DEFAULT_AUDIO_RATE_NEGOTIATION ? 1 : 0);
}

static int audio_get_btrate(void) {
	return CFG_getBluetoothSamplingrateLimit();
}

static void audio_set_btrate(int val) {
	CFG_setBluetoothSamplingrateLimit(val);
	poke_audiomon();
}

static void audio_reset_btrate(void) {
	audio_set_btrate(CFG_DEFAULT_BLUETOOTH_MAXRATE);
}

static const char* audio_get_output(void) {
	return AudioMgr_getSinkDescription();
}

static int audio_get_volume(void) {
	return GetVolume();
}

static void audio_set_volume(int val) {
	SetVolume(val);
}

static void audio_reset_volume(void) {
	SetVolume(SETTINGS_DEFAULT_VOLUME);
}

static void audio_on_show(SettingsPage* page) {
	settings_item_sync(&page->items[AUDIO_IDX_VOLUME]);
	settings_item_sync(&page->items[AUDIO_IDX_NEGOTIATION]);
	settings_item_sync(&page->items[AUDIO_IDX_BTRATE]);
}

SettingsPage* audio_page_create(void) {
	SettingsPage* page = calloc(1, sizeof(SettingsPage));
	if (!page)
		return NULL;
	page->title = "Settings | Audio";
	page->is_list = 0;
	page->dynamic_start = -1;
	page->max_items = AUDIO_ITEM_COUNT;
	page->items = calloc(AUDIO_ITEM_COUNT, sizeof(SettingItem));
	if (!page->items) {
		free(page);
		return NULL;
	}

	page->items[AUDIO_IDX_OUTPUT] = (SettingItem)ITEM_STATIC_INIT(
		"Output", "Current audio sink and sample rate", audio_get_output);

	page->items[AUDIO_IDX_VOLUME] = (SettingItem){
		.name = "Volume",
		.desc = "Speaker volume",
		.type = ITEM_CYCLE,
		.visible = 1,
		.labels = volume_labels,
		.label_count = VOLUME_LABEL_COUNT,
		.get_value = audio_get_volume,
		.set_value = audio_set_volume,
		.on_reset = audio_reset_volume,
		.values = volume_values,
	};

	page->items[AUDIO_IDX_NEGOTIATION] = (SettingItem){
		.name = "Rate negotiation",
		.desc = "Auto matches the output device's preferred sample rate",
		.type = ITEM_CYCLE,
		.visible = 1,
		.labels = neg_labels,
		.label_count = 2,
		.get_value = audio_get_negotiation,
		.set_value = audio_set_negotiation,
		.on_reset = audio_reset_negotiation,
		.values = neg_values,
	};

	page->items[AUDIO_IDX_BTRATE] = (SettingItem){
		.name = "Bluetooth max sampling rate",
		.desc = "Cap the sample rate used for Bluetooth audio",
		.type = ITEM_CYCLE,
		.visible = 1,
		.labels = rate_labels,
		.label_count = 2,
		.get_value = audio_get_btrate,
		.set_value = audio_set_btrate,
		.on_reset = audio_reset_btrate,
		.values = rate_values,
	};

	page->item_count = AUDIO_ITEM_COUNT;
	page->on_show = audio_on_show;

	return page;
}

void audio_page_destroy(SettingsPage* page) {
	(void)page; // process exit cleans up, matching bt_page_destroy
}
