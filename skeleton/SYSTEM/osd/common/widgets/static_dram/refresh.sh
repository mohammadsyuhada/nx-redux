#!/bin/sh
# Used memory in MB (MemTotal - MemAvailable), shown by the MEM static widget
total=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
avail=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
used_mb=$(( (total - avail) / 1024 ))
mkdir -p /tmp/trimui_osd/toggle_dmem
echo -n "${used_mb}M" > /tmp/trimui_osd/toggle_dmem/status
