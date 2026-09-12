#!/bin/sh

NX_AUTO_PATH=${NX_AUTO_PATH:-"$USERDATA_PATH/auto.sh"}
NX_AUTO_RESUME_PATH=${NX_AUTO_RESUME_PATH:-"$SHARED_USERDATA_PATH/.minui/auto_resume.txt"}
NX_POWEROFF_PATH=${NX_POWEROFF_PATH:-/tmp/poweroff}
NX_REBOOT_PATH=${NX_REBOOT_PATH:-/tmp/reboot}

NX_DEFER_BOOT_WORK="yes"
if [ -f "$NX_AUTO_PATH" ] || [ -f "$NX_AUTO_RESUME_PATH" ]; then
	# Boot hooks and auto-resume rely on platform setup completing first.
	NX_DEFER_BOOT_WORK="no"
fi

nx_run_boot_work() {
	delay=$1
	shift

	if [ "$NX_DEFER_BOOT_WORK" = "yes" ]; then
		(
			sleep "$delay"
			if [ ! -f "$NX_POWEROFF_PATH" ] && [ ! -f "$NX_REBOOT_PATH" ]; then
				"$@"
			fi
		) &
	else
		"$@"
	fi
}
