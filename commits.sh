#!/bin/bash

show() {
	pushd "$1" >> /dev/null
	HASH=$(git rev-parse --short=8 HEAD)
	NAME=$(basename $PWD)
	DATE=$(git log -1 --pretty='%ad' --date=format:'%Y-%m-%d')
	REPO=$(git config --get remote.origin.url)
	REPO=$(sed -E "s,(^git@github.com:)|(^https?://github.com/)|(.git$)|(/$),,g" <<<"$REPO")
	popd >> /dev/null

	printf '\055 %-24s%-10s%-12s%s\n' $NAME $HASH $DATE $REPO
}
list() {
	pushd "$1" >> /dev/null
	for D in ./*; do
		show "$D"
	done
	popd >> /dev/null
}
rule() {
	echo '----------------------------------------------------------------'	
}
tell() {
	echo $1
	rule
}

cores() {
	echo CORES
	list ./workspace/$1/cores/src
	bump
}

bump() {
	printf '\n'
}

{
	printf '%-26s%-10s%-12s%s\n' NEXTUI HASH DATE USER/REPO
	rule
	show ./
	bump
	
	tell TOOLCHAINS
	list ./toolchains
	bump
	
	tell LIBRETRO
	show ./workspace/all/minarch/libretro-common
	bump

	tell TG5040
	show ./workspace/tg5040/other/evtest
	show ./workspace/tg5040/other/jstest
	show ./workspace/tg5040/other/unzip60
	cores tg5040

	tell CHECK
	echo https://github.com/USER/REPO/compare/HASH...HEAD
	bump
} | sed 's/\n/ /g'