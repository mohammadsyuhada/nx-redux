#!/bin/sh
# Regenerate the arcade display-name tables the launcher falls back to when a
# ROM has no map.txt alias (content.c ArcadeNames). One table per emulator
# tag, written to skeleton/SYSTEM/res/arcade/<TAG>.txt as "<zip stem>\t<title>"
# lines, sorted by stem. BIOS sets are written with a "." title so hide()
# drops them from game lists, the same way a "."-prefixed map.txt alias does.
# Titles are plain: trailing "(...)"/"[...]" groups (revision, region, date,
# cartridge id) are dropped and only the first of " / " alternate names kept,
# so "1942 (Revision B)" shows as "1942". Clones that collapse to the same
# title in one folder fall back to their filenames (Directory_index's run
# disambiguation).
#
# The tables are committed rather than generated at build time: the FBNeo
# core source is only fetched when cores are compiled, and the Flycast
# checkout (workspace/all/other/flycast/flycast) is gitignored.
#
#   FBN.txt  FinalBurn Neo Arcade + Neo Geo DATs (ClrMame Pro XML), the core
#            pinned by fbneo_HASH in workspace/tg5040/cores/Makefile
#   DC.txt   Flycast's Naomi/Naomi 2/Atomiswave/System SP tables
#            (core/hw/naomi/naomi_roms.cpp Games[] and BIOS[])
#
# usage: scripts/gen-arcade-names.sh [fbneo_src_dir] [flycast_src_dir]
set -eu
cd "$(dirname "$0")/.."

FBNEO=${1:-workspace/tg5040/cores/src/fbneo}
FLYCAST=${2:-workspace/all/other/flycast/flycast}
OUT=skeleton/SYSTEM/res/arcade
export LC_ALL=C

mkdir -p "$OUT"

# "<stem>\t<title>" in, plain title out (see above); a title that would be
# stripped to nothing is kept whole
plain_titles() {
	awk -F '\t' -v OFS='\t' '
		{
			t = $2
			if (t != ".") {
				i = index(t, " / ")
				if (i > 1)
					t = substr(t, 1, i - 1)
				# drop trailing bracket groups, nested ones included:
				# "Dragon Spirit (new version (DS3))" -> "Dragon Spirit"
				for (;;) {
					sub(/[ \t]+$/, "", t)
					n = length(t)
					close_ch = substr(t, n, 1)
					if (close_ch != ")" && close_ch != "]")
						break
					depth = 0
					for (j = n; j > 0; j--) {
						c = substr(t, j, 1)
						if (c == ")" || c == "]")
							depth++
						else if ((c == "(" || c == "[") && --depth == 0)
							break
					}
					if (j <= 1)
						break # unbalanced, or the whole title is one group
					t = substr(t, 1, j - 1)
				}
				if (t != "")
					$2 = t
			}
			print
		}
	'
}

# FBNeo: <game [isbios="yes"] name="stem" ...> followed by <description>
fbneo_dat() {
	awk '
		/<game / {
			name = $0
			sub(/.* name="/, "", name)
			sub(/".*/, "", name)
			bios = ($0 ~ /isbios="yes"/)
			next
		}
		/<description>/ && name != "" {
			d = $0
			sub(/.*<description>/, "", d)
			sub(/<\/description>.*/, "", d)
			gsub(/&lt;/, "<", d)
			gsub(/&gt;/, ">", d)
			gsub(/&quot;/, "\"", d)
			gsub(/&apos;/, "\047", d)
			gsub(/&amp;/, "\\&", d)
			print name "\t" (bios ? "." : d)
			name = ""
		}
	' "$1"
}

{
	fbneo_dat "$FBNEO/dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat"
	fbneo_dat "$FBNEO/dats/FinalBurn Neo (ClrMame Pro XML, Neogeo only).dat"
} | plain_titles | sort -t "$(printf '\t')" -k1,1 -u >"$OUT/FBN.txt"

# Flycast: each Games[] entry opens with "{", then the name, parent (string or
# nullptr) and description lines, with "//" comment lines interleaved; each
# BIOS[] entry opens with "{", then its name. Games[] ends with a { nullptr } sentinel, which the string match skips.
awk '
	BEGIN { field = -1 }
	/^const BIOS_t BIOS\[\]/ { section = "bios"; next }
	/^const Game Games\[\]/ { section = "games"; next }
	/^};/ { section = ""; next }
	section != "" && /^[ \t]*\{[ \t]*$/ { field = 0; name = ""; next }
	section != "" && field >= 0 {
		line = $0
		sub(/^[ \t]*/, "", line)
		sub(/[ \t]*\/\/.*$/, "", line) # MAME-style "// title" and "// FIXME" notes
		if (line == "")
			next
		field++
		if (field == 1) {
			if (line !~ /^"[^"]*",$/) { field = -1; next }
			name = substr(line, 2, length(line) - 3)
			if (section == "bios") { print name "\t."; field = -1 }
		} else if (field == 3) {
			if (line ~ /^"/) {
				d = line
				sub(/^"/, "", d)
				sub(/",?[ \t]*$/, "", d)
				gsub(/\\"/, "\"", d)
				print name "\t" d
			}
			field = -1
		}
	}
' "$FLYCAST/core/hw/naomi/naomi_roms.cpp" | plain_titles | sort -t "$(printf '\t')" -k1,1 -u >"$OUT/DC.txt"

wc -l "$OUT/FBN.txt" "$OUT/DC.txt"
