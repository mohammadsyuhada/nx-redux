#ifndef EMU_OVERLAY_CFG_H
#define EMU_OVERLAY_CFG_H

#include <stdbool.h>

#ifndef EMU_OVL_MAX_SECTIONS
#define EMU_OVL_MAX_SECTIONS 16
#endif
#ifndef EMU_OVL_MAX_ITEMS
#define EMU_OVL_MAX_ITEMS 32
#endif
#ifndef EMU_OVL_MAX_VALUES
#define EMU_OVL_MAX_VALUES 16
#endif
#ifndef EMU_OVL_MAX_STR
#define EMU_OVL_MAX_STR 128
#endif
// Item descriptions; larger in the pre-launch editor build, where core info
// strings routinely exceed EMU_OVL_MAX_STR.
#ifndef EMU_OVL_MAX_DESC
#define EMU_OVL_MAX_DESC EMU_OVL_MAX_STR
#endif

typedef enum {
	EMU_OVL_TYPE_BOOL,
	EMU_OVL_TYPE_CYCLE,
	EMU_OVL_TYPE_INT,
	EMU_OVL_TYPE_ENUM
} EmuOvlItemType;

typedef struct {
	char key[EMU_OVL_MAX_STR];
	char label[EMU_OVL_MAX_STR];
	char description[EMU_OVL_MAX_DESC];
	EmuOvlItemType type;
	int values[EMU_OVL_MAX_VALUES];
	char labels[EMU_OVL_MAX_VALUES][EMU_OVL_MAX_STR];
	// ENUM only: heap-owned value strings (index == the item's internal int
	// value). NULL for other types. Freed by emu_ovl_cfg_free.
	char* svalues[EMU_OVL_MAX_VALUES];
	int value_count;
	int int_min, int_max, int_step;
	int float_scale; // >0: INI value is float; multiply by scale to get int, divide when writing
	int default_value;
	int current_value;
	int staged_value;
	bool dirty;
} EmuOvlItem;

typedef struct {
	char name[EMU_OVL_MAX_STR];
	char ini_section[EMU_OVL_MAX_STR]; // INI section for this group (optional, falls back to global config_section)
	EmuOvlItem items[EMU_OVL_MAX_ITEMS];
	int item_count;
} EmuOvlSection;

typedef struct {
	char emulator[EMU_OVL_MAX_STR];
	char config_file[EMU_OVL_MAX_STR];
	char config_section[EMU_OVL_MAX_STR];
	char options_hint[256];
	bool save_state;
	bool load_state;
	EmuOvlSection sections[EMU_OVL_MAX_SECTIONS];
	int section_count;
} EmuOvlConfig;

int emu_ovl_cfg_load(EmuOvlConfig* cfg, const char* json_path);
void emu_ovl_cfg_free(EmuOvlConfig* cfg);
int emu_ovl_cfg_read_ini(EmuOvlConfig* cfg, const char* ini_path);
int emu_ovl_cfg_write_ini(EmuOvlConfig* cfg, const char* ini_path);
void emu_ovl_cfg_reset_staged(EmuOvlConfig* cfg);
void emu_ovl_cfg_reset_section_to_defaults(EmuOvlSection* sec);
void emu_ovl_cfg_apply_staged(EmuOvlConfig* cfg);
bool emu_ovl_cfg_has_changes(EmuOvlConfig* cfg);

// Resolved INI section name for sections[sec_idx]: the section's own
// ini_section when set, else the global config_section (which is also what an
// out-of-range index returns). Never NULL.
const char* emu_ovl_cfg_section_name(const EmuOvlConfig* cfg, int sec_idx);

// Find the item with `key` in any schema section resolving to `ini_section`.
// out_sec/out_item receive the indices when non-NULL. NULL when not found.
EmuOvlItem* emu_ovl_cfg_find_item(EmuOvlConfig* cfg, const char* ini_section, const char* key, int* out_sec, int* out_item);

// INI string -> internal int, using the exact per-type conversion
// emu_ovl_cfg_read_ini applies. False (and *out_value untouched) when the
// string cannot be parsed as this item's type.
bool emu_ovl_cfg_parse_value(const EmuOvlItem* item, const char* str, int* out_value);

// Internal int -> INI string, the single formatter emu_ovl_cfg_write_ini also
// goes through. `value` is explicit so callers can format either the staged
// or the current value. Always NUL-terminates when out_size > 0.
void emu_ovl_cfg_format_value(const EmuOvlItem* item, int value, char* out, int out_size);

// ENUM only: index of `value` in svalues, appending it (label = the value
// string) when absent and there is room. -1 when full or not an enum.
int emu_ovl_cfg_enum_intern(EmuOvlItem* item, const char* value);

#endif
