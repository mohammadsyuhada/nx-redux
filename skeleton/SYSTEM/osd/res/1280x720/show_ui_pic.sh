#!/bin/sh

#assume no jq binary in system

BG=$1
UI_MSG_JSON="\n\
{ \n\
    \"type\":\"ui\", \n\
    \"id\":\"com.trimui.osd.ui.static\", \n\
    \"duration\":1000, \n\
    \"size\":0, \n\
    \"x\":0, \n\
    \"y\":0, \n\
    \"w\":1280, \n\
    \"h\":720, \n\
    \"message\":\" \", \n\
    \"font\":\"\", \n\
    \"bg\":\"$BG\", \n\
    \"icon\":\"\", \n\
    \"fontsize\":24, \n\
    \"fontcolor\":\"FFFFFFFF\" \n\
} \n"

echo -e $UI_MSG_JSON > /tmp/trimui_osd/osd_toast_msg
# echo -e $UI_MSG_JSON > dump.txt

