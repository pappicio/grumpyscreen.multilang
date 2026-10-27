#!/bin/sh
# Start/stop GrumpyScreen on a FlashForge printer.
DIR=$(dirname "$(readlink -f "$0")")
PIDFILE=/run/grumpyscreen.pid

find_touch() {
	for d in /sys/class/input/event*; do
		case "$(cat "$d/device/name" 2>/dev/null)" in
		*Touchscreen*|*goodix*)
			LVGL_EVDEV_DEV=/dev/input/${d##*/}
			return 0
			;;
		esac
	done
	return 1
}

start() {
	if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
		echo "grumpyscreen: already running"
		return 0
	fi
	if ! find_touch; then
		echo "grumpyscreen: no Touchscreen input device"
		return 1
	fi
	echo "grumpyscreen: touch $LVGL_EVDEV_DEV"
	export LVGL_EVDEV_DEV
	cd "$DIR" || return 1
	start-stop-daemon -S -b -m -p "$PIDFILE" -N 10 --exec "$DIR/grumpyscreen"
}

stop() {
	start-stop-daemon -K -q -p "$PIDFILE" 2>/dev/null
	i=0
	while [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null && [ $i -lt 10 ]; do
		sleep 1
		i=$((i + 1))
	done
	rm -f "$PIDFILE"
}

case "$1" in
start) start ;;
*) start ;;
stop) stop ;;
restart) stop; start ;;
status)
	if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
		echo "running (pid $(cat "$PIDFILE"))"
	else
		echo "stopped"
		exit 1
	fi
	;;
*) echo "usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac