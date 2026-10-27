#!/bin/sh
wpa_cli -i wlan0 reconfigure
wpa_cli -i wlan0 reassociate
