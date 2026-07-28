#!/bin/sh

: ${ECHO:=echo}
: ${SH:=sh}
: ${CP:=cp}
: ${CP_R:=cp -r}

dest=$1

case "$DOWNLOAD_GAMEPACKS" in
	yes)
		LICENSEFILTER=GPL BATCH=1 $SH download-gamepacks.sh
		;;
	allinone)
		LICENSEFILTER=allinone BATCH=1 $SH download-gamepacks.sh
		;;
	all)
		BATCH=1 $SH download-gamepacks.sh
		;;
	*)
		;;
esac

set -e
found_gamepack=no
for GAME in contrib/gamepacks/* games/*Pack; do
	if [ -d "$GAME" ]; then
		found_gamepack=yes
		$SH install-gamepack.sh "$GAME" "$dest"
	fi
done

if [ "$found_gamepack" = no ]; then
	$ECHO "Game packs not found, please run"
	$ECHO "  ./download-gamepacks.sh"
	$ECHO "and then try again!"
fi
