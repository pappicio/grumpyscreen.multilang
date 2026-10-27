#!/bin/sh
CFG=/usr/data/grumpyscreen/grumpyscreen.cfg
CURL=/usr/prog/curl-7.55.1/bin/curl
getval() {
awk -v k="$1" '/^\[moonraker\]/{s=1;next} /^\[/{s=0} s && $0 ~ "^"k":"{sub("^"k":[ \t]*","");print;exit}' "$CFG" | tr -d '\r '
}
MOON_HOST=$(getval host)
MOON_PORT=$(getval port)
[ -z "$MOON_HOST" ] && MOON_HOST=127.0.0.1
[ -z "$MOON_PORT" ] && MOON_PORT=7125
MOON_URL="http://$MOON_HOST:$MOON_PORT"
SOCAT=/opt/bin/socat
post() {
printf 'POST %s HTTP/1.0\r\nHost: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' "$1" "$MOON_HOST" | $SOCAT -t5 - TCP:$MOON_HOST:$MOON_PORT
}
