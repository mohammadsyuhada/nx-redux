BLUEZ_SRC = ./output

bluez:
	rm -rf ./build
	mkdir -p ./build/.update_bluez/usr/bin
	mkdir -p ./build/.update_bluez/usr/lib
	mkdir -p ./build/.update_bluez/usr/lib64
	mkdir -p ./build/.update_bluez/usr/lib/alsa-lib
	mkdir -p ./build/.update_bluez/usr/lib64/alsa-lib
	mkdir -p ./build/.update_bluez/etc/dbus-1/system.d
	mkdir -p ./build/.update_bluez/usr/share/alsa/alsa.conf.d
	# bluez
	cp $(BLUEZ_SRC)/bin/bluetoothctl build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/btmon build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/rctest build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/l2test build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/l2ping build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/bluemoon build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/hex2hcd build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/mpris-proxy build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/btattach build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/bluetoothd build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/lib/libbluetooth.so.3.19.15 build/.update_bluez/usr/lib/
	cp $(BLUEZ_SRC)/lib/libbluetooth.so.3.19.15 build/.update_bluez/usr/lib64/
	# bluez-alsa
	cp $(BLUEZ_SRC)/lib/alsa-lib/libasound_module_ctl_bluealsa.so build/.update_bluez/usr/lib/alsa-lib/
	cp $(BLUEZ_SRC)/lib/alsa-lib/libasound_module_pcm_bluealsa.so build/.update_bluez/usr/lib/alsa-lib/
	cp $(BLUEZ_SRC)/lib/alsa-lib/libasound_module_ctl_bluealsa.so build/.update_bluez/usr/lib64/alsa-lib/
	cp $(BLUEZ_SRC)/lib/alsa-lib/libasound_module_pcm_bluealsa.so build/.update_bluez/usr/lib64/alsa-lib/
	cp $(BLUEZ_SRC)/bin/bluealsa build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/bluealsa-aplay build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/bluealsa-cli build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/etc/dbus-1/system.d/bluealsa.conf build/.update_bluez/etc/dbus-1/system.d/
	cp $(BLUEZ_SRC)/share/alsa/alsa.conf.d/20-bluealsa.conf build/.update_bluez/usr/share/alsa/alsa.conf.d/
	# sbc
	cp $(BLUEZ_SRC)/lib/libsbc.so.1.3.1 build/.update_bluez/usr/lib/
	cp $(BLUEZ_SRC)/lib/libsbc.so.1.3.1 build/.update_bluez/usr/lib64/
	cp $(BLUEZ_SRC)/bin/sbcinfo build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/sbcdec build/.update_bluez/usr/bin/
	cp $(BLUEZ_SRC)/bin/sbcenc build/.update_bluez/usr/bin/
	# readline + ncurses (runtime deps)
	cp $(BLUEZ_SRC)/lib/libreadline.so.8.2 build/.update_bluez/usr/lib/
	cp $(BLUEZ_SRC)/lib/libreadline.so.8.2 build/.update_bluez/usr/lib64/
	cp $(BLUEZ_SRC)/lib/libncursesw.so.6.4 build/.update_bluez/usr/lib/
	cp $(BLUEZ_SRC)/lib/libncursesw.so.6.4 build/.update_bluez/usr/lib64/
	# script
	cp support/upgrade_bluez.sh build/post_install.sh
	# zip
	cd build && python3 -m zipfile -c ../nextui.upgrade_bluez.pakz .update_bluez/ post_install.sh

all: bluez
