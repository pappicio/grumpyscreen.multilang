#!/bin/sh

SCRIPT_PATH="/usr/data/grumpyscreen/grumpy.sh"

kill_matching() {
    for p in $(ps w | grep -E "$1" | grep -v grep | awk '{print $1}'); do
        [ "$p" != "$$" ] && kill "$p" 2>/dev/null
    done
}


stop() {
    echo "Fermando grumpyscreen..."
    kill_matching "40-grumpyscreen.sh"
    kill_matching "grumpyscreen"
    echo "Fermato."
}

start() {
    echo "Avviando grumpyscreen..."
    if [ -x "$SCRIPT_PATH" ]; then
        "$SCRIPT_PATH" &
        echo "Avviato in background."
    else
        echo "Errore: $SCRIPT_PATH non trovato o non eseguibile."
        exit 1
    fi
}

case "$1" in
    *)
        stop
        sleep 1
        start

        ;;
esac