#! /bin/sh

TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
if [ "$TRIMUI_MODEL" != "Trimui Smart Pro S" ]; then
	return 0
fi

# if /usr/lib/libsbc.so.1.3.1 exists, we are already updated
if [ -f "/usr/lib/libsbc.so.1.3.1" ]; then
    return 0
fi

BLUEZ_PATH=/mnt/SDCARD/.update_bluez
if [ -d "$BLUEZ_PATH" ]; then
	echo "Updating bluez..."
else
    echo "Unable to locate update files, exiting."
    return 0
fi

# backup existing binaries
rm -rf /mnt/SDCARD/btmgr_backup
mkdir -p /mnt/SDCARD/btmgr_backup/usr/bin
mkdir -p /mnt/SDCARD/btmgr_backup/usr/lib/alsa-lib
mkdir -p /mnt/SDCARD/btmgr_backup/usr/lib64/alsa-lib
mkdir -p /mnt/SDCARD/btmgr_backup/etc/dbus-1/system.d
mkdir -p /mnt/SDCARD/btmgr_backup/usr/share/alsa/alsa.conf.d/

# bluez
for bin in bluetoothctl btmon rctest l2test l2ping bluemoon hex2hcd mpris-proxy btattach bluetoothd obexd; do
    cp /usr/bin/$bin /mnt/SDCARD/btmgr_backup/usr/bin/ 2>/dev/null
done
# backup existing libbluetooth (version may vary)
cp /usr/lib/libbluetooth.so.3.* /mnt/SDCARD/btmgr_backup/usr/lib/ 2>/dev/null
cp /usr/lib64/libbluetooth.so.3.* /mnt/SDCARD/btmgr_backup/usr/lib64/ 2>/dev/null
cp /etc/dbus-1/system.d/bluetooth.conf /mnt/SDCARD/btmgr_backup/etc/dbus-1/system.d/ 2>/dev/null
cp /etc/dbus-1/system.d/bluetooth-mesh.conf /mnt/SDCARD/btmgr_backup/etc/dbus-1/system.d/ 2>/dev/null

# bluealsa
cp /usr/lib/alsa-lib/libasound_module_ctl_bluealsa.so /mnt/SDCARD/btmgr_backup/usr/lib/alsa-lib/ 2>/dev/null
cp /usr/lib/alsa-lib/libasound_module_pcm_bluealsa.so /mnt/SDCARD/btmgr_backup/usr/lib/alsa-lib/ 2>/dev/null
cp /usr/lib64/alsa-lib/libasound_module_ctl_bluealsa.so /mnt/SDCARD/btmgr_backup/usr/lib64/alsa-lib/ 2>/dev/null
cp /usr/lib64/alsa-lib/libasound_module_pcm_bluealsa.so /mnt/SDCARD/btmgr_backup/usr/lib64/alsa-lib/ 2>/dev/null
cp /usr/bin/bluealsa /mnt/SDCARD/btmgr_backup/usr/bin/ 2>/dev/null
cp /usr/bin/bluealsa-aplay /mnt/SDCARD/btmgr_backup/usr/bin/ 2>/dev/null
cp /usr/share/alsa/alsa.conf.d/20-bluealsa.conf /mnt/SDCARD/btmgr_backup/usr/share/alsa/alsa.conf.d/ 2>/dev/null

# sbc
cp /usr/lib/libsbc.so.1.* /mnt/SDCARD/btmgr_backup/usr/lib/ 2>/dev/null
cp /usr/lib64/libsbc.so.1.* /mnt/SDCARD/btmgr_backup/usr/lib64/ 2>/dev/null
for bin in sbcinfo sbcdec sbcenc; do
    cp /usr/bin/$bin /mnt/SDCARD/btmgr_backup/usr/bin/ 2>/dev/null
done

# compress backup and clean up
backupfile="/mnt/SDCARD/btmgr_$(date +%Y%m%d_%H%M%S).tar"
tar cf $backupfile /mnt/SDCARD/btmgr_backup/
rm -rf /mnt/SDCARD/btmgr_backup

# deploy update
cd $BLUEZ_PATH

# bluez binaries
for bin in bluetoothctl btmon rctest l2test l2ping bluemoon hex2hcd mpris-proxy btattach bluetoothd; do
    mv ./usr/bin/$bin /usr/bin/ 2>/dev/null
done
# Also update bluetoothd at libexec (stock init wrapper starts from there)
cp /usr/bin/bluetoothd /usr/libexec/bluetooth/bluetoothd 2>/dev/null

mv ./usr/lib/libbluetooth.so.3.19.15 /usr/lib/
mv ./usr/lib64/libbluetooth.so.3.19.15 /usr/lib64/

# bluealsa
mv ./usr/lib/alsa-lib/libasound_module_ctl_bluealsa.so /usr/lib/alsa-lib/
mv ./usr/lib/alsa-lib/libasound_module_pcm_bluealsa.so /usr/lib/alsa-lib/
mv ./usr/lib64/alsa-lib/libasound_module_ctl_bluealsa.so /usr/lib64/alsa-lib/
mv ./usr/lib64/alsa-lib/libasound_module_pcm_bluealsa.so /usr/lib64/alsa-lib/
mv ./usr/bin/bluealsa /usr/bin/
mv ./usr/bin/bluealsa-aplay /usr/bin/
mv ./usr/bin/bluealsa-cli /usr/bin/ 2>/dev/null
mv ./etc/dbus-1/system.d/bluealsa.conf /etc/dbus-1/system.d/
mv ./usr/share/alsa/alsa.conf.d/20-bluealsa.conf /usr/share/alsa/alsa.conf.d/

# sbc
mv ./usr/lib/libsbc.so.1.3.1 /usr/lib/
mv ./usr/lib64/libsbc.so.1.3.1 /usr/lib64/
mv ./usr/bin/sbcinfo /usr/bin/
mv ./usr/bin/sbcdec /usr/bin/
mv ./usr/bin/sbcenc /usr/bin/

# readline + ncurses (runtime deps for bluetoothctl)
mv ./usr/lib/libreadline.so.8.2 /usr/lib/
mv ./usr/lib64/libreadline.so.8.2 /usr/lib64/
mv ./usr/lib/libncursesw.so.6.4 /usr/lib/
mv ./usr/lib64/libncursesw.so.6.4 /usr/lib64/

# symlinks
cd /usr/lib/
ln -s -f libbluetooth.so.3.19.15 libbluetooth.so.3
ln -s -f libbluetooth.so.3.19.15 libbluetooth.so
ln -s -f libsbc.so.1.3.1 libsbc.so.1
ln -s -f libsbc.so.1.3.1 libsbc.so
ln -s -f libreadline.so.8.2 libreadline.so.8
ln -s -f libreadline.so.8.2 libreadline.so
ln -s -f libncursesw.so.6.4 libncursesw.so.6
ln -s -f libncursesw.so.6.4 libncursesw.so
ln -s -f libncursesw.so.6.4 libtinfo.so.6
ln -s -f libncursesw.so.6.4 libtinfo.so

cd /usr/lib64/
ln -s -f libbluetooth.so.3.19.15 libbluetooth.so.3
ln -s -f libbluetooth.so.3.19.15 libbluetooth.so
ln -s -f libsbc.so.1.3.1 libsbc.so.1
ln -s -f libsbc.so.1.3.1 libsbc.so
ln -s -f libreadline.so.8.2 libreadline.so.8
ln -s -f libreadline.so.8.2 libreadline.so
ln -s -f libncursesw.so.6.4 libncursesw.so.6
ln -s -f libncursesw.so.6.4 libncursesw.so
ln -s -f libncursesw.so.6.4 libtinfo.so.6
ln -s -f libncursesw.so.6.4 libtinfo.so

# clean up
rm -rf $BLUEZ_PATH

echo "Finished upgrading bluez."
