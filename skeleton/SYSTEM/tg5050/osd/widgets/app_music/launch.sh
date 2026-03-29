#!/bin/sh
MUSIC_DIR=`dirname "$0"`
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$progdir

cd $MUSIC_DIR/

mkdir -p /tmp/trimui_music

#prebuild default image
#./pic2argb ./music_default.png /tmp/trimui_music/vfb_osd
