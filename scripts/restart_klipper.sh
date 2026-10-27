#!/bin/sh
. "$(dirname "$0")/common.sh"
post "/machine/services/restart?service=klipper"
