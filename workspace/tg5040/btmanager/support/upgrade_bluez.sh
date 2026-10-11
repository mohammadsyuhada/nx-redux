#! /bin/sh

# The pakz unpacks .update_bluez/ into the card root before this runs, and
# it ships with every release, so every exit path must remove it (an
# already-upgraded device used to keep ~26 MB of it on the card).
BLUEZ_PATH=/mnt/SDCARD/.update_bluez

TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
if [ "$TRIMUI_MODEL" = "Trimui Smart Pro S" ]; then
	rm -rf "$BLUEZ_PATH"
	return 0
fi

# if /usr/lib/libsbc.so.1.3.1 exists, we are already updated
if [ -f "/usr/lib/libsbc.so.1.3.1" ]; then
	rm -rf "$BLUEZ_PATH"
    return 0
fi

if [ -d "$BLUEZ_PATH" ]; then
	echo "Updating bluez..."
else
    echo "Unable to locate update files, exiting."
    return 0
fi

# No backup of the stock stack is kept: nothing ever restored from it, and
# a firmware reflash is the real revert path.

# deploy update
cd $BLUEZ_PATH

# copy relative to root
#bluez
mv ./usr/bin/bluetoothctl /usr/bin/
mv ./usr/bin/btmon /usr/bin/
mv ./usr/bin/rctest /usr/bin/
mv ./usr/bin/l2test /usr/bin/
mv ./usr/bin/l2ping /usr/bin/
mv ./usr/bin/bluemoon /usr/bin/
mv ./usr/bin/hex2hcd /usr/bin/
mv ./usr/bin/mpris-proxy /usr/bin/
mv ./usr/bin/btattach /usr/bin/
mv ./usr/bin/isotest /usr/bin/
mv ./usr/bin/bluetoothd /usr/bin/
mv ./usr/bin/obexd /usr/bin/
mv ./usr/lib/libbluetooth.so.3.19.15 /usr/lib/
mv ./usr/lib64/libbluetooth.so.3.19.15 /usr/lib64/
#mv ./etc/dbus-1/system.d/bluetooth.conf /etc/dbus-1/system.d/
#mv ./etc/dbus-1/system.d/bluetooth-mesh.conf /etc/dbus-1/system.d/
#mv ./etc/dbus-1/system.d/bluetooth-mesh-adapter.conf /etc/dbus-1/system.d/

# bluealsa
mv ./usr/lib/alsa-lib/libasound_module_ctl_bluealsa.so /usr/lib/alsa-lib/
mv ./usr/lib/alsa-lib/libasound_module_pcm_bluealsa.so /usr/lib/alsa-lib/
mv ./usr/lib64/alsa-lib/libasound_module_ctl_bluealsa.so /usr/lib64/alsa-lib/
mv ./usr/lib64/alsa-lib/libasound_module_pcm_bluealsa.so /usr/lib64/alsa-lib/
mv ./usr/bin/bluealsa /usr/bin/
mv ./usr/bin/bluealsa-aplay /usr/bin/
mv ./etc/dbus-1/system.d/bluealsa.conf /etc/dbus-1/system.d/
mv ./usr/share/alsa/alsa.conf.d/20-bluealsa.conf /usr/share/alsa/alsa.conf.d/

# sbc
mv ./usr/lib/libsbc.so.1.3.1 /usr/lib/
mv ./usr/lib64/libsbc.so.1.3.1 /usr/lib64/
mv ./usr/bin/sbcinfo /usr/bin/
mv ./usr/bin/sbcdec /usr/bin/
mv ./usr/bin/sbcenc /usr/bin/

# symlinks
cd /usr/lib/ && ln -s -f libbluetooth.so.3.19.15 libbluetooth.so.3
cd /usr/lib/ && ln -s -f libbluetooth.so.3.19.15 libbluetooth.so
cd /usr/lib64/ && ln -s -f libbluetooth.so.3.19.15 libbluetooth.so.3
cd /usr/lib64/ && ln -s -f libbluetooth.so.3.19.15 libbluetooth.so
cd /usr/lib/ && ln -s -f libsbc.so.1.3.1 libsbc.so.1
cd /usr/lib/ && ln -s -f libsbc.so.1.3.1 libsbc.so
cd /usr/lib64/ && ln -s -f libsbc.so.1.3.1 libsbc.so.1
cd /usr/lib64/ && ln -s -f libsbc.so.1.3.1 libsbc.so

# clean up
rm -rf $BLUEZ_PATH

echo "Finished upgrading bluez."