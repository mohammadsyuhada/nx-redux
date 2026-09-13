#!/bin/sh

NX_DEVICE_FILE=${NX_DEVICE_FILE:-"$SDCARD_PATH/.nx-device"}
NX_DEVICE_ID=$(cat "$NX_DEVICE_FILE" 2> /dev/null || true)

case "$PLATFORM:$NX_DEVICE_ID" in
	tg5040:tg5040-brick)
		DEVICE="brick"
		TRIMUI_MODEL="Trimui Brick"
		;;
	tg5040:tg5040-brickpro)
		DEVICE="brickpro"
		TRIMUI_MODEL="Trimui Brick Pro"
		;;
	tg5040:tg5040-smartpro)
		DEVICE="smartpro"
		TRIMUI_MODEL="Trimui Smart Pro"
		;;
	tg5050:tg5050-smartpros)
		DEVICE="smartpros"
		TRIMUI_MODEL="Trimui Smart Pro S"
		;;
	*)
		# First boot has not extracted .nx-device yet, so keep a firmware fallback.
		TRIMUI_MODEL=${NX_MAINUI_MODEL:-$(strings /usr/trimui/bin/MainUI | grep '^Trimui')}
		case "$PLATFORM:$TRIMUI_MODEL" in
			tg5040:"Trimui Brick") DEVICE="brick" ;;
			tg5040:"Trimui Brick Pro") DEVICE="brickpro" ;;
			tg5040:*) DEVICE="smartpro" ;;
			tg5050:*) DEVICE="smartpros" ;;
		esac
		;;
esac

export DEVICE TRIMUI_MODEL NX_DEVICE_ID
