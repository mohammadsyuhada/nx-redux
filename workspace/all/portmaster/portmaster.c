#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "ui_components.h"
#include "ui_list.h"
#include "utils.h"
#include "wget_fetch.h"

// PortMaster paths
#define PORTS_PAK_DIR SDCARD_PATH "/Emus/" PLATFORM "/PORTS.pak"
#define PORTMASTER_DIR SDCARD_PATH "/Emus/shared/PortMaster"
#define PUGWASH_PATH PORTMASTER_DIR "/pugwash"
#define PORTS_ROM_DIR ROMS_PATH "/Ports (PORTS)"
#define PORTS_LAUNCH_SRC TOOLS_PATH "/PortMaster.pak/ports_launch.sh"

// PortMaster release URL
#define PM_RELEASE_URL "https://github.com/PortsMaster/PortMaster-GUI/releases/latest/download/PortMaster.zip"
#define PM_ZIP_PATH "/tmp/PortMaster.zip"

// PortMaster runtime dependencies (bundled in skeleton/files/)
#define PM_FILES_DIR PORTMASTER_DIR "/files"
#define PM_BUNDLED_BIN PM_FILES_DIR "/bin.zip"
#define PM_BUNDLED_LIB PM_FILES_DIR "/lib.zip"
#define PM_BUNDLED_LIBS PM_FILES_DIR "/libs.zip"
#define PM_BUNDLED_PYLIBS PM_FILES_DIR "/pylibs.zip"
#define PM_BUNDLED_CERT PM_FILES_DIR "/ca-certificates.crt"
#define PM_BUNDLED_PROGRESSOR PM_FILES_DIR "/progressor"
#define PM_BUNDLED_DISABLE_PY PM_FILES_DIR "/disable_python_function.py"

enum PMState {
	PM_STATE_CHECK,
	PM_STATE_NOT_INSTALLED,
	PM_STATE_INSTALLED,
	PM_STATE_DOWNLOADING,
	PM_STATE_EXTRACTING,
	PM_STATE_PATCHING,
	PM_STATE_INSTALL_DONE,
	PM_STATE_INSTALL_FAILED,
	PM_STATE_LAUNCHING,
	PM_STATE_MENU,
	PM_STATE_CONFIRM_UNINSTALL,
};

static SDL_Surface* screen;
static enum PMState state = PM_STATE_CHECK;
static volatile int download_progress = 0;
static volatile bool download_cancel = false;
static volatile int download_speed = 0;
static volatile int download_eta = 0;
static volatile bool download_done = false;
static int download_result = 0;
static pthread_t download_thread;
static bool download_thread_active = false;

// Menu state
static int menu_selected = 0;
static int menu_scroll = 0;
static bool is_nintendo = true;
static const char* extract_status = "Extracting PortMaster...";

// Layout marker
#define LAYOUT_MARKER SHARED_USERDATA_PATH "/PORTS-portmaster/xbox_layout"

static bool is_nintendo_layout(void) {
	return access(LAYOUT_MARKER, F_OK) != 0;
}

static void toggle_layout(void);

// Menu items
#define MENU_COUNT 3
#define MENU_OPEN 0
#define MENU_LAYOUT 1
#define MENU_UNINSTALL 2

// Download label for UI display


static void* download_thread_func(void* arg) {
	(void)arg;
	download_result = wget_download_file(
		PM_RELEASE_URL, PM_ZIP_PATH,
		&download_progress, &download_cancel,
		&download_speed, &download_eta);
	download_done = true;
	return NULL;
}


static bool portmaster_installed(void) {
	return access(PUGWASH_PATH, F_OK) == 0;
}

static void invalidate_emulist_cache(void) {
	unlink(EMULIST_CACHE_PATH);
	unlink(ROMINDEX_CACHE_PATH);
}

// Sync cover/screenshot images from .ports/*/  to .media/ for NextUI artwork
static void sync_port_artwork(void) {
	char ports_dir[512];
	char media_dir[512];
	snprintf(ports_dir, sizeof(ports_dir), "%s/.ports", PORTS_ROM_DIR);
	snprintf(media_dir, sizeof(media_dir), "%s/.media", PORTS_ROM_DIR);
	mkdir_p(media_dir);

	DIR* dir = opendir(ports_dir);
	if (!dir)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		// Read port.json to get the .sh script name
		char json_path[512];
		snprintf(json_path, sizeof(json_path), "%s/%s/port.json", ports_dir, entry->d_name);

		FILE* fp = fopen(json_path, "r");
		if (!fp)
			continue;

		// Extract .sh filename from "items" array (first .sh entry)
		char line[512];
		char sh_name[256] = {0};
		while (fgets(line, sizeof(line), fp)) {
			char* p = strstr(line, ".sh\"");
			if (p) {
				// Walk backwards to find opening quote
				char* q = p;
				while (q > line && *q != '"')
					q--;
				if (*q == '"') {
					q++;				 // skip opening quote
					int len = p + 3 - q; // include ".sh"
					if (len > 0 && len < (int)sizeof(sh_name)) {
						strncpy(sh_name, q, len);
						sh_name[len] = '\0';
					}
				}
				break;
			}
		}
		fclose(fp);

		if (!sh_name[0])
			continue;

		// Strip .sh to get base name
		char base_name[256];
		strncpy(base_name, sh_name, sizeof(base_name));
		char* dot = strrchr(base_name, '.');
		if (dot)
			*dot = '\0';

		// Check if .media/{base_name}.png already exists
		char media_path[512];
		snprintf(media_path, sizeof(media_path), "%s/%s.png", media_dir, base_name);
		if (access(media_path, F_OK) == 0)
			continue;

		// Find best available image: cover.png > cover.jpg > screenshot.png > screenshot.jpg
		const char* candidates[] = {"cover.png", "cover.jpg", "screenshot.png", "screenshot.jpg"};
		char src_path[512] = {0};
		for (int i = 0; i < 4; i++) {
			snprintf(src_path, sizeof(src_path), "%s/%s/%s", ports_dir, entry->d_name, candidates[i]);
			if (access(src_path, F_OK) == 0)
				break;
			src_path[0] = '\0';
		}

		if (!src_path[0])
			continue;

		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "cp -f '%s' '%s'", src_path, media_path);
		system(cmd);
	}

	closedir(dir);
}

static void cleanup_portmaster(void) {
	char cmd[1024];
	// Remove all PortMaster files except the bundled files/ directory from skeleton
	snprintf(cmd, sizeof(cmd),
			 "find '%s' -mindepth 1 -maxdepth 1 "
			 "! -name 'files' "
			 "! -name 'patchedScripts' "
			 "-exec rm -rf {} +",
			 PORTMASTER_DIR);
	system(cmd);
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", PORTS_PAK_DIR);
	system(cmd);
	invalidate_emulist_cache();
}

static void create_ports_pak(void) {
	char cmd[512];
	mkdir_p(PORTS_PAK_DIR);
	snprintf(cmd, sizeof(cmd), "cp -f '%s' '%s/launch.sh'", PORTS_LAUNCH_SRC, PORTS_PAK_DIR);
	system(cmd);
	invalidate_emulist_cache();
}

static int extract_portmaster(void) {
	char cmd[1024];
	mkdir_p(PORTMASTER_DIR);
	snprintf(cmd, sizeof(cmd), SHARED_BIN_PATH "/7zzs.aarch64 x '%s' -o'%s' -aoa >/dev/null 2>&1", PM_ZIP_PATH, SDCARD_PATH "/Emus/shared/");
	int ret = system(cmd);
	unlink(PM_ZIP_PATH);
	if (ret != 0) {
		cleanup_portmaster();
	}
	return ret;
}

static void render_screen(void); // forward declaration

static int extract_deps(void) {
	char cmd[1024];
	int ret;

	// Extract bundled bin.zip to PortMaster/bin/
	extract_status = "Extracting binaries...";
	render_screen();
	mkdir_p(PORTMASTER_DIR "/bin");
	snprintf(cmd, sizeof(cmd), SHARED_BIN_PATH "/7zzs.aarch64 x '%s' -o'" PORTMASTER_DIR "/bin' -aoa >/dev/null 2>&1", PM_BUNDLED_BIN);
	ret = system(cmd);
	if (ret != 0)
		return ret;

	// Extract bundled lib.zip to PortMaster/lib/
	extract_status = "Extracting libraries...";
	render_screen();
	mkdir_p(PORTMASTER_DIR "/lib");
	snprintf(cmd, sizeof(cmd), SHARED_BIN_PATH "/7zzs.aarch64 x '%s' -o'" PORTMASTER_DIR "/lib' -aoa >/dev/null 2>&1", PM_BUNDLED_LIB);
	ret = system(cmd);
	if (ret != 0)
		return ret;

	// Extract bundled libs.zip to PortMaster/libs/
	extract_status = "Extracting additional libraries...";
	render_screen();
	mkdir_p(PORTMASTER_DIR "/libs");
	snprintf(cmd, sizeof(cmd), SHARED_BIN_PATH "/7zzs.aarch64 x '%s' -o'" PORTMASTER_DIR "/libs' -aoa >/dev/null 2>&1", PM_BUNDLED_LIBS);
	ret = system(cmd);
	if (ret != 0)
		return ret;

	// Extract bundled pylibs.zip to PortMaster/ root (contains exlibs/ and pylibs/ folders)
	extract_status = "Extracting Python libraries...";
	render_screen();
	snprintf(cmd, sizeof(cmd), SHARED_BIN_PATH "/7zzs.aarch64 x '%s' -o'" PORTMASTER_DIR "' -aoa >/dev/null 2>&1", PM_BUNDLED_PYLIBS);
	ret = system(cmd);
	if (ret != 0)
		return ret;

	// Copy bundled SSL certificates
	mkdir_p(PORTMASTER_DIR "/ssl/certs");
	snprintf(cmd, sizeof(cmd), "cp -f '%s' '%s/ssl/certs/ca-certificates.crt'", PM_BUNDLED_CERT, PORTMASTER_DIR);
	system(cmd);

	// Copy disable_python_function.py to PortMaster root for patching
	snprintf(cmd, sizeof(cmd), "cp -f '%s' '%s/disable_python_function.py'", PM_BUNDLED_DISABLE_PY, PORTMASTER_DIR);
	system(cmd);

	// Move compat libs (newer glib, fontconfig, freetype, brotli) to lib/compat/
	// These override system libs and break pugwash's SDL2 stack, so they must only
	// be in the LD_LIBRARY_PATH for port launches (set in ports_launch.sh), not pugwash.
	mkdir_p(PORTMASTER_DIR "/lib/compat");
	snprintf(cmd, sizeof(cmd),
			 "cd '%s/lib' && "
			 "for f in libglib-2.0.so* libfontconfig.so* libfreetype.so* libbrotlidec.so* libbrotlicommon.so*; do "
			 "[ -f \"$f\" ] && mv -f \"$f\" compat/; "
			 "done 2>/dev/null",
			 PORTMASTER_DIR);
	system(cmd);

	// Create unversioned .so copies for libs that only have versioned names
	// (FAT32 doesn't support symlinks, and some ports link against unversioned .so names)
	// In lib/: skip GL/EGL/DRM libs — unversioned copies override system GPU drivers and crash gl4es
	// In lib/compat/: create all unversioned copies (ports need them and compat isn't in pugwash's path)
	snprintf(cmd, sizeof(cmd),
			 "cd '%s/lib' && for f in *.so.*; do "
			 "case \"$f\" in libEGL*|libGL*|libGLU*|libGLX*|libGLd*|libdrm*|libOpenGL*) continue ;; esac; "
			 "base=$(echo \"$f\" | sed 's/\\.so\\..*/\\.so/'); "
			 "[ ! -e \"$base\" ] && cp -f \"$f\" \"$base\"; "
			 "done 2>/dev/null; "
			 "cd '%s/lib/compat' && for f in *.so.*; do "
			 "base=$(echo \"$f\" | sed 's/\\.so\\..*/\\.so/'); "
			 "[ ! -e \"$base\" ] && cp -f \"$f\" \"$base\"; "
			 "done 2>/dev/null",
			 PORTMASTER_DIR, PORTMASTER_DIR);
	system(cmd);

	// Make binaries executable
	snprintf(cmd, sizeof(cmd), "chmod -R +x '%s/bin/' 2>/dev/null", PORTMASTER_DIR);
	system(cmd);

	return 0;
}

static void patch_platform_py(void) {
	char cmd[1024];
	char platform_py[512];
	snprintf(platform_py, sizeof(platform_py), "%s/pylibs/harbourmaster/platform.py", PORTMASTER_DIR);

	// Disable portmaster_install in first_run (causes crash on TrimUI)
	// Uses disable_python_function.py to inject 'return' at the start of the function
	snprintf(cmd, sizeof(cmd),
			 PORTMASTER_DIR "/bin/python3 " PORTMASTER_DIR "/disable_python_function.py '%s' portmaster_install",
			 platform_py);
	system(cmd);

	// Fix hardcoded ROM/image paths for NextUI directory naming
	// PortMaster expects /mnt/SDCARD/Roms/PORTS and /mnt/SDCARD/Imgs/PORTS
	// but NextUI uses /mnt/SDCARD/Roms/Ports (PORTS)
	snprintf(cmd, sizeof(cmd),
			 "sed -i 's|/mnt/SDCARD/Roms/PORTS|" SDCARD_PATH "/Roms/Ports (PORTS)|g;"
			 "s|/mnt/SDCARD/Imgs/PORTS|" SDCARD_PATH "/Roms/Ports (PORTS)/.media|g' '%s'",
			 platform_py);
	system(cmd);
}

static void patch_device_info(void) {
	char device_info[512];
	snprintf(device_info, sizeof(device_info), "%s/device_info.txt", PORTMASTER_DIR);

	// Patch TrimUI detection to distinguish tg5050 (Smart Pro S) from tg5040 (Smart Pro / Brick)
	// tg5050 uses Allwinner T527 (sun55iw3 in device tree) vs tg5040's A133Plus (sun8iw20)
	//
	// Two patches needed:
	// 1. The CFW_NAME=="TrimUI" block (initial DEVICE_NAME assignment) — replace the hardcoded
	//    DEVICE_NAME="TrimUI Smart Pro" with a runtime check for sun55iw3
	// 2. The FIXES case statement — add a "trimui smart pro s" case before "trimui smart pro"
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
			 "if ! grep -q 'Smart Pro S' '%s' 2>/dev/null; then "
			 // Patch 1: Replace the hardcoded DEVICE_NAME in the CFW_NAME=="TrimUI" block
			 // Original:  DEVICE_NAME="TrimUI Smart Pro"
			 // Patched:   runtime check via /proc/device-tree/model
			 "sed -i '/\\$CFW_NAME.*TrimUI/{n;"
			 "s|DEVICE_NAME=\"TrimUI Smart Pro\"|"
			 "if grep -q sun55iw3 /proc/device-tree/model 2>/dev/null; then "
			 "DEVICE_NAME=\"TrimUI Smart Pro S\"; "
			 "else DEVICE_NAME=\"TrimUI Smart Pro\"; fi|"
			 ";}' '%s';"
			 // Patch 2: Add tg5050 case in the FIXES section before the tg5040 case
			 "sed -i '/\"trimui smart pro\"|\"trimui-smart-pro\")/i\\"
			 "    \"trimui smart pro s\"|\"trimui-smart-pro-s\")\\n"
			 "        DEVICE_CPU=\"t527\"\\n"
			 "        DEVICE_NAME=\"TrimUI Smart Pro S\"\\n"
			 "        ;;' '%s';"
			 "fi",
			 device_info, device_info, device_info);
	system(cmd);

	// Patch hardware.py — PortMaster's Python GUI uses its own device detection
	// independent of device_info.txt. Need to add tg5050 support to:
	// 1. nice_device_to_device(): map sun55iw3 → trimui-smart-pro-s
	// 2. DEVICE_TO_NICE_DEVICE: map "TrimUI Smart Pro S" → device/manufacturer/cfw
	// 3. DEVICES: map trimui-smart-pro-s → resolution/cpu/capabilities
	// 4. GLIBC_DEVICE: map trimui-smart-pro-s* → glibc version
	char hardware_py[512];
	snprintf(hardware_py, sizeof(hardware_py), "%s/pylibs/harbourmaster/hardware.py", PORTMASTER_DIR);
	snprintf(cmd, sizeof(cmd),
			 "if ! grep -q 'smart-pro-s' '%s' 2>/dev/null; then "
			 // Add sun55iw3 → trimui-smart-pro-s mapping (before sun50iw10 → trimui-smart-pro)
			 "sed -i \"s|('sun50iw10', 'trimui-smart-pro'),|"
			 "('sun55iw3',  'trimui-smart-pro-s'),\\n"
			 "        ('sun50iw10', 'trimui-smart-pro'),|\" '%s';"
			 // Add TrimUI Smart Pro S to DEVICE_TO_NICE_DEVICE (after TrimUI Smart Pro line)
			 "sed -i '/\"TrimUI Smart Pro\":.*trimui-smart-pro/a\\"
			 "    \"TrimUI Smart Pro S\": {\"device\": \"trimui-smart-pro-s\", \"manufacturer\": \"TrimUI\", \"cfw\": [\"TrimUI\"]},' '%s';"
			 // Add trimui-smart-pro-s to DEVICES dict (after trimui-smart-pro line)
			 "sed -i '/\"trimui-smart-pro\":.*a133plus/a\\"
			 "    \"trimui-smart-pro-s\": {\"resolution\": (1280, 720), \"analogsticks\": 2, \"cpu\": \"t527\", \"capabilities\": [\"power\"], \"ram\": 1024},' '%s';"
			 // Add trimui-smart-pro-s* glibc entry (after trimui-* line)
			 "sed -i '/\"trimui-\\*\":/i\\"
			 "    \"trimui-smart-pro-s*\": \"2.33\",' '%s';"
			 // Clear Python cache so patched .py files take effect
			 "rm -rf '%s/pylibs/harbourmaster/__pycache__';"
			 "fi",
			 hardware_py, hardware_py, hardware_py, hardware_py, hardware_py, PORTMASTER_DIR);
	system(cmd);
}

static void ensure_default_config(void) {
	char config_dir[512];
	char config_path[512];
	snprintf(config_dir, sizeof(config_dir), "%s/config", PORTMASTER_DIR);
	snprintf(config_path, sizeof(config_path), "%s/config.json", config_dir);

	// Only write if config doesn't exist yet
	if (access(config_path, F_OK) == 0)
		return;

	mkdir_p(config_dir);

	FILE* fp = fopen(config_path, "w");
	if (!fp)
		return;

	fprintf(fp, "{\n");
	fprintf(fp, "    \"disclaimer\": true,\n");
	fprintf(fp, "    \"show_experimental\": false,\n");
	fprintf(fp, "    \"theme\": \"default_theme\",\n");
	fprintf(fp, "    \"theme-scheme\": \"Darkest Mode\"\n");
	fprintf(fp, "}\n");

	fclose(fp);
}

static void create_busybox_wrappers(void) {
	char bin_dir[512];
	char marker[512];
	char bb_path[512];
	snprintf(bin_dir, sizeof(bin_dir), "%s/bin", PORTMASTER_DIR);
	snprintf(marker, sizeof(marker), "%s/busybox_wrappers.done", bin_dir);
	snprintf(bb_path, sizeof(bb_path), "%s/busybox", bin_dir);

	// Skip if already done
	if (access(marker, F_OK) == 0)
		return;

	// Skip if busybox not installed yet
	if (access(bb_path, F_OK) != 0)
		return;

	// Get list of busybox applets and create wrapper scripts
	// Shell printf gets: '#!/bin/sh\nexec <bb_path> %s "$@"\n' "$cmd"
	// where %s is the shell printf format specifier for $cmd
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "cd '%s' && BB='%s' && created='' && "
			 "for cmd in $(\"$BB\" --list); do "
			 "  case \"$cmd\" in sh) continue ;; esac; "
			 "  if [ ! -e \"$cmd\" ] || grep -q 'exec .*/busybox .*\\$@' \"$cmd\" 2>/dev/null; then "
			 "    printf '#!/bin/sh\\nexec %%s %%s \"$@\"\\n' \"$BB\" \"$cmd\" > \"$cmd\"; "
			 "    created=\"$created $cmd\"; "
			 "  fi; "
			 "done && "
			 "[ -n \"$created\" ] && chmod +x $created; "
			 "touch '%s'",
			 bin_dir, bb_path, marker);
	system(cmd);
}

static void patch_mod_trimui(void) {
	char mod_path[512];
	snprintf(mod_path, sizeof(mod_path), "%s/mod_TrimUI.txt", PORTMASTER_DIR);

	// Patch HOME override: upstream uses /mnt/SDCARD/Data/home which doesn't exist
	// Replace with our shared userdata path that ports_launch.sh already sets as HOME
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "sed -i 's|/mnt/SDCARD/Data/home|" SHARED_USERDATA_PATH "/PORTS-portmaster|g' '%s' 2>/dev/null",
			 mod_path);
	system(cmd);
}

static void patch_control_txt(void) {
	char control_path[512];
	snprintf(control_path, sizeof(control_path), "%s/control.txt", PORTMASTER_DIR);

	FILE* fp = fopen(control_path, "w");
	if (!fp)
		return;

	fprintf(fp, "#!/bin/sh\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# SPDX-License-Identifier: MIT\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# Patched for NextUI\n");
	fprintf(fp, "\n");
	fprintf(fp, "CUR_TTY=/dev/tty0\n");
	fprintf(fp, "\n");
	fprintf(fp, "export controlfolder=\"%s\"\n", PORTMASTER_DIR);
	fprintf(fp, "export directory=\"mnt/SDCARD/.ports_temp\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "PM_SCRIPTNAME=\"$(basename \"${PM_SCRIPTNAME:-$0}\")\"\n");
	fprintf(fp, "PM_PORTNAME=\"${PM_SCRIPTNAME%%.sh}\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "if [ -z \"$PM_PORTNAME\" ]; then\n");
	fprintf(fp, "  PM_PORTNAME=\"Port\"\n");
	fprintf(fp, "fi\n");
	fprintf(fp, "\n");
	fprintf(fp, "export ESUDO=\"\"\n");
	fprintf(fp, "export ESUDOKILL=\"-1\"\n");
	fprintf(fp, "export SDL_GAMECONTROLLERCONFIG_FILE=\"$controlfolder/gamecontrollerdb.txt\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "get_controls() {\n");
	fprintf(fp, "  sleep 0.5\n");
	fprintf(fp, "}\n");
	fprintf(fp, "\n");
	fprintf(fp, ". $controlfolder/device_info.txt\n");
	fprintf(fp, ". $controlfolder/funcs.txt\n");
	fprintf(fp, "\n");
	fprintf(fp, "export GPTOKEYB2=\"$ESUDO env LD_PRELOAD=$controlfolder/libinterpose.aarch64.so $controlfolder/gptokeyb2 $ESUDOKILL\"\n");
	fprintf(fp, "export GPTOKEYB=\"$ESUDO $controlfolder/gptokeyb $ESUDOKILL\"\n");

	fclose(fp);
}

static void apply_patched_scripts(void) {
	// Overwrite port scripts in Roms dir with patched versions from patchedScripts/
	// Patched scripts fix issues like broken symlinks on exFAT, incorrect mount paths, etc.
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "PATCH_DIR='" PORTMASTER_DIR "/patchedScripts' && "
			 "ROM_DIR='" PORTS_ROM_DIR "' && "
			 "[ -d \"$PATCH_DIR\" ] && "
			 "for f in \"$PATCH_DIR\"/*.sh; do "
			 "[ -f \"$f\" ] && [ -f \"$ROM_DIR/$(basename \"$f\")\" ] && "
			 "cp -f \"$f\" \"$ROM_DIR/$(basename \"$f\")\"; "
			 "done");
	system(cmd);
}

static void fix_port_scripts(void) {
	// Fix hardcoded /roms/ports/PortMaster paths in port scripts after pugwash installs them
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
			 "export PATH=" PORTMASTER_DIR "/bin:" SHARED_BIN_PATH ":$PATH && "
			 "ROM_DIR='" SDCARD_PATH "/Roms/Ports (PORTS)' && "
			 "find \"$ROM_DIR\" -maxdepth 1 -type f -name '*.sh' | while IFS= read -r f; do "
			 "if grep -q '/roms/ports/PortMaster' \"$f\" 2>/dev/null; then "
			 "sed -i 's|/roms/ports/PortMaster|" PORTMASTER_DIR "|g' \"$f\"; "
			 "fi; "
			 "if head -1 \"$f\" | grep -q '#!/bin/bash'; then "
			 "sed -i '1s|#!/bin/bash|#!/usr/bin/env bash|' \"$f\"; "
			 "fi; "
			 "done");
	system(cmd);
}

static void ensure_bash_symlink(void) {
	// Many port scripts use #!/bin/bash shebangs but the device has no /bin/bash.
	// Create a symlink to PortMaster's bundled bash so these scripts can execute.
	if (access("/bin/bash", F_OK) != 0) {
		symlink(PORTMASTER_DIR "/bin/bash", "/bin/bash");
	}
}

static void replace_progressor_binaries(void) {
	// Port-bundled progressor binaries are SDL2/OpenGL GUI apps that fail on our display.
	// Replace them with our show2.elf-based shell script that shows a loading screen.
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "for f in '%s/.ports'/*/progressor; do "
			 "[ -f \"$f\" ] && cp -f '%s' \"$f\" && chmod +x \"$f\"; "
			 "done",
			 PORTS_ROM_DIR, PM_BUNDLED_PROGRESSOR);
	system(cmd);
}

static void set_controller_layout(const char* layout) {
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "cp -f '%s/gamecontrollerdb_%s.txt' '%s/gamecontrollerdb.txt'",
			 PM_FILES_DIR, layout, PORTMASTER_DIR);
	system(cmd);
}

static void toggle_layout(void) {
	if (is_nintendo_layout()) {
		// Switch to Xbox: create marker file
		FILE* fp = fopen(LAYOUT_MARKER, "w");
		if (fp)
			fclose(fp);
		set_controller_layout("xbox");
	} else {
		// Switch to Nintendo: remove marker file
		unlink(LAYOUT_MARKER);
		set_controller_layout("nintendo");
	}
	is_nintendo = is_nintendo_layout();
}

static void launch_pugwash(void) {
	// Ensure /bin/bash exists for port scripts with #!/bin/bash shebangs
	ensure_bash_symlink();

	// Always ensure NextUI patches are applied before launching
	// (PortMaster's first_run or updates may have overwritten them)
	patch_control_txt();
	patch_device_info();
	patch_platform_py();
	patch_mod_trimui();

	// PortMaster GUI (pugwash) uses Xbox button layout internally
	// (its XBOX FIXER always swaps to Nintendo in the UI regardless)
	set_controller_layout("xbox");

	char cmd[3072];
	// tg5050 ships newer lib versions than what some bundled binaries expect
	system("[ ! -e " PORTMASTER_DIR "/lib/libffi.so.7 ] && [ -e /usr/lib/libffi.so.8 ] && "
		   "cp /usr/lib/libffi.so.8 " PORTMASTER_DIR "/lib/libffi.so.7;"
		   "[ ! -e " PORTMASTER_DIR "/lib/libncurses.so.5 ] && [ -e /usr/lib/libncurses.so.6 ] && "
		   "cp /usr/lib/libncurses.so.6 " PORTMASTER_DIR "/lib/libncurses.so.5");

	snprintf(cmd, sizeof(cmd),
			 "export LD_LIBRARY_PATH=" SYSTEM_PATH "/lib:" PORTMASTER_DIR "/lib:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH && "
			 "export PATH=" SYSTEM_PATH "/bin:" PORTMASTER_DIR "/bin:" SHARED_BIN_PATH ":/usr/trimui/bin:$PATH && "
			 "export PYSDL2_DLL_PATH=/usr/trimui/lib:/usr/lib && "
			 "export SSL_CERT_FILE=" PORTMASTER_DIR "/ssl/certs/ca-certificates.crt && "
			 "export HOME=" SHARED_USERDATA_PATH "/PORTS-portmaster && "
			 "export XDG_DATA_HOME='" SDCARD_PATH "/Emus/shared' && "
			 "export HM_TOOLS_DIR='" SDCARD_PATH "/Emus/shared' && "
			 "export HM_PORTS_DIR='" SDCARD_PATH "/Roms/Ports (PORTS)/.ports' && "
			 "export HM_SCRIPTS_DIR='" SDCARD_PATH "/Roms/Ports (PORTS)' && "
			 "export SDL_GAMECONTROLLERCONFIG_FILE='%s/gamecontrollerdb.txt' && "
			 "cd '%s' && "
			 "rm -f .pugwash-reboot && "
			 "RETRIES=0; "
			 "while true; do "
			 // Patch platform.py before each pugwash run — fixes hardcoded PORTS paths
			 // and disables portmaster_install (crashes on TrimUI). This handles both
			 // first-run (pylibs just extracted by pugwash) and self-update scenarios.
			 "PP=pylibs/harbourmaster/platform.py; "
			 "if [ -f \"$PP\" ]; then "
			 "sed -i 's|/mnt/SDCARD/Roms/PORTS|" SDCARD_PATH "/Roms/Ports (PORTS)|g;"
			 "s|/mnt/SDCARD/Imgs/PORTS|" SDCARD_PATH "/Roms/Ports (PORTS)/.media|g' \"$PP\"; " PORTMASTER_DIR "/bin/python3 " PORTMASTER_DIR "/disable_python_function.py "
			 "\"$PP\" portmaster_install 2>/dev/null; "
			 "rm -rf pylibs/harbourmaster/__pycache__; "
			 "fi; " PORTMASTER_DIR "/bin/python3 pugwash 2>&1 | tee " SDCARD_PATH "/.userdata/" PLATFORM "/logs/portmaster_pugwash.txt; "
			 "[ -f .pugwash-reboot ] && rm -f .pugwash-reboot && continue; "
			 // If platform.py still has unpatched paths, pugwash just extracted fresh
			 // pylibs (first_run or update) and crashed — retry once with patches applied
			 "if grep -q '/Roms/PORTS' \"$PP\" 2>/dev/null && [ $RETRIES -lt 1 ]; then "
			 "RETRIES=$((RETRIES+1)); continue; fi; "
			 "break; "
			 "done",
			 PORTMASTER_DIR, PORTMASTER_DIR);
	system(cmd);

	// Re-patch after pugwash exits
	// (pugwash first_run or updates may have overwritten these)
	patch_control_txt();
	patch_device_info();
	patch_platform_py();
	patch_mod_trimui();

	// Restore user's preferred controller layout
	// (pugwash or updates may have overwritten gamecontrollerdb.txt)
	set_controller_layout(is_nintendo_layout() ? "nintendo" : "xbox");

	// Fix hardcoded paths in any newly installed port scripts
	fix_port_scripts();

	// Replace port scripts with patched versions (fixes exFAT symlink issues, mount paths, etc.)
	apply_patched_scripts();

	// Replace port-bundled progressor binaries with our show2.elf-based script
	//replace_progressor_binaries();
}

static void format_speed(int bps, char* buf, int buf_size) {
	if (bps >= 1048576)
		snprintf(buf, buf_size, "%.1f MB/s", bps / 1048576.0);
	else if (bps >= 1024)
		snprintf(buf, buf_size, "%d KB/s", bps / 1024);
	else
		snprintf(buf, buf_size, "%d B/s", bps);
}

static void start_download(void) {
	PWR_disableSleep();
	state = PM_STATE_DOWNLOADING;
	download_progress = 0;
	download_cancel = false;
	download_speed = 0;
	download_eta = 0;
	download_result = 0;
	download_done = false;
	pthread_create(&download_thread, NULL, download_thread_func, NULL);
	download_thread_active = true;
}

static void render_screen(void) {
	GFX_clear(screen);

	switch (state) {
	case PM_STATE_CHECK:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, "Checking installation...");
		break;

	case PM_STATE_NOT_INSTALLED:
		UI_renderMenuBar(screen, "PortMaster");
		{
			char* lines = "PortMaster is not installed.\nPress A to download and install.";
			int line_h = SCALE1(FONT_LARGE + 4);
			int y = screen->h / 2 - line_h;
			GFX_blitText(font.large, lines, line_h, COLOR_WHITE, screen,
						 &(SDL_Rect){SCALE1(PADDING), y, screen->w - SCALE1(PADDING * 2), screen->h});
		}
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "INSTALL", NULL});
		break;

	case PM_STATE_DOWNLOADING:
		UI_renderMenuBar(screen, "PortMaster");
		{
			char status_msg[128];
			snprintf(status_msg, sizeof(status_msg), "Downloading PortMaster...");

			char detail[128];
			char speed_str[64];
			format_speed(download_speed, speed_str, sizeof(speed_str));
			if (download_eta > 0)
				snprintf(detail, sizeof(detail), "%s, %ds left", speed_str, download_eta);
			else
				snprintf(detail, sizeof(detail), "%s", speed_str);

			UI_renderDownloadProgress(screen, &(UIDownloadProgress){
												  .status = status_msg,
												  .detail = detail,
												  .progress = download_progress,
												  .show_bar = true,
											  });
		}
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
		break;

	case PM_STATE_EXTRACTING:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, extract_status);
		break;

	case PM_STATE_PATCHING:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, "Configuring PortMaster...");
		break;

	case PM_STATE_INSTALL_DONE:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, "Installation complete!");
		UI_renderButtonHintBar(screen, (char*[]){"A", "LAUNCH", "B", "BACK", NULL});
		break;

	case PM_STATE_INSTALL_FAILED:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, "Installation failed. Check WiFi and try again.");
		UI_renderButtonHintBar(screen, (char*[]){"A", "RETRY", "B", "BACK", NULL});
		break;

	case PM_STATE_INSTALLED:
	case PM_STATE_LAUNCHING:
		UI_renderMenuBar(screen, "PortMaster");
		UI_renderCenteredMessage(screen, "Launching PortMaster...");
		break;

	case PM_STATE_MENU: {
		UI_renderMenuBar(screen, "PortMaster");
		ListLayout layout = UI_calcListLayout(screen);

		UISettingsItem items[] = {
			{.label = "Open PortMaster", .swatch = -1, .desc = "Launch the PortMaster GUI"},
			{.label = "Button Layout", .value = is_nintendo ? "Nintendo" : "Xbox", .swatch = -1, .cycleable = 1, .desc = "Button layout for in-game port controls"},
			{.label = "Uninstall PortMaster", .swatch = -1, .desc = "Remove PortMaster from your device"},
		};

		UI_renderSettingsPage(screen, &layout, items, MENU_COUNT,
							  menu_selected, &menu_scroll, NULL);

		bool is_layout = (menu_selected == MENU_LAYOUT);
		UI_renderButtonHintBar(screen, (char*[]){
										   is_layout ? "LEFT/RIGHT" : "A",
										   is_layout ? "CHANGE" : "OPEN",
										   "B", "EXIT",
										   NULL});
		break;
	}

	case PM_STATE_CONFIRM_UNINSTALL:
		UI_renderConfirmDialog(screen, "Uninstall PortMaster?", "This cannot be undone.");
		break;
	}

	GFX_flip(screen);
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "PortMaster");

	InitSettings();
	PAD_init();
	PWR_init();
	setup_signal_handlers();

	// Start in menu if installed, otherwise show install screen
	if (portmaster_installed()) {
		ensure_default_config();
		is_nintendo = is_nintendo_layout();
		state = PM_STATE_MENU;
	} else {
		state = PM_STATE_NOT_INSTALLED;
	}

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		switch (state) {
		case PM_STATE_MENU:
			if (PAD_navigateMenu(&menu_selected, MENU_COUNT))
				dirty = true;
			if (menu_selected == MENU_LAYOUT &&
				(PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_RIGHT))) {
				toggle_layout();
				dirty = true;
			}
			if (PAD_justPressed(BTN_A)) {
				switch (menu_selected) {
				case MENU_OPEN:
					state = PM_STATE_LAUNCHING;
					dirty = true;
					break;
				case MENU_LAYOUT:
					toggle_layout();
					dirty = true;
					break;
				case MENU_UNINSTALL:
					state = PM_STATE_CONFIRM_UNINSTALL;
					dirty = true;
					break;
				}
			}
			if (PAD_justPressed(BTN_B))
				app_quit = true;
			break;

		case PM_STATE_CONFIRM_UNINSTALL:
			if (PAD_justPressed(BTN_A)) {
				cleanup_portmaster();
				state = PM_STATE_NOT_INSTALLED;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				state = PM_STATE_MENU;
				dirty = true;
			}
			break;

		case PM_STATE_NOT_INSTALLED:
			if (PAD_justPressed(BTN_A)) {
				start_download();
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				app_quit = true;
			}
			break;

		case PM_STATE_DOWNLOADING:
			dirty = true; // always redraw for progress
			if (PAD_justPressed(BTN_B)) {
				download_cancel = true;
				if (download_thread_active) {
					pthread_join(download_thread, NULL);
					download_thread_active = false;
				}
				download_done = false;
				unlink(PM_ZIP_PATH);
				PWR_enableSleep();
				state = PM_STATE_NOT_INSTALLED;
			}
			// Check if download thread finished (success or failure)
			if (download_thread_active && download_done) {
				pthread_join(download_thread, NULL);
				download_thread_active = false;
				download_done = false;
				if (download_result > 0) {
					state = PM_STATE_EXTRACTING;
				} else {
					PWR_enableSleep();
					state = PM_STATE_INSTALL_FAILED;
				}
			}
			break;

		case PM_STATE_EXTRACTING:
			extract_status = "Extracting PortMaster...";
			render_screen();
			{
				int ret = extract_portmaster();
				if (ret == 0 && portmaster_installed()) {
					render_screen();
					ret = extract_deps();
					if (ret == 0) {
						state = PM_STATE_PATCHING;
					} else {
						cleanup_portmaster();
						PWR_enableSleep();
						state = PM_STATE_INSTALL_FAILED;
					}
				} else {
					cleanup_portmaster();
					PWR_enableSleep();
					state = PM_STATE_INSTALL_FAILED;
				}
			}
			dirty = true;
			break;

		case PM_STATE_PATCHING:
			render_screen();
			ensure_bash_symlink();
			patch_control_txt();
			patch_platform_py();
			patch_device_info();
			patch_mod_trimui();
			ensure_default_config();
			invalidate_emulist_cache();
			{
				char cmd[512];
				snprintf(cmd, sizeof(cmd), "chmod -R +x '%s' 2>/dev/null", PORTMASTER_DIR);
				system(cmd);
			}
			create_busybox_wrappers();
			create_ports_pak();
			PWR_enableSleep();
			state = PM_STATE_INSTALL_DONE;
			dirty = true;
			break;

		case PM_STATE_INSTALL_DONE:
			if (PAD_justPressed(BTN_A)) {
				state = PM_STATE_LAUNCHING;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				app_quit = true;
			}
			break;

		case PM_STATE_INSTALL_FAILED:
			if (PAD_justPressed(BTN_A)) {
				start_download();
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				app_quit = true;
			}
			break;

		case PM_STATE_LAUNCHING:
			render_screen();
			QuitSettings();
			PWR_quit();
			PAD_quit();
			GFX_quit();

			launch_pugwash();
			create_busybox_wrappers(); // Re-create if pugwash update wiped them
			sync_port_artwork();
			invalidate_emulist_cache();

			// Re-init SDL after pugwash returns
			screen = GFX_init(MODE_MAIN);
			InitSettings();
			PAD_init();
			PWR_init();

			// Check if user uninstalled from within pugwash
			if (portmaster_installed()) {
				is_nintendo = is_nintendo_layout();
				state = PM_STATE_MENU;
			} else {
				state = PM_STATE_NOT_INSTALLED;
			}
			dirty = true;
			break;

		default:
			break;
		}

		if (dirty) {
			render_screen();
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	if (download_thread_active) {
		download_cancel = true;
		pthread_join(download_thread, NULL);
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
