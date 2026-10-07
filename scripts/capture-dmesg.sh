#!/bin/sh
# Poll the small kernel ring buffer and keep every unique driver line, so the
# early init log (set_tx_power, RXFLTMAP, first TX) survives the later scan/AWDL spam.
# Usage: sudo scripts/capture-dmesg.sh [outfile]   (Ctrl-C to stop)
OUT="${1:-$HOME/rtw-full.txt}"
: > "$OUT"
while :; do
  dmesg
  sleep 1
done | grep --line-buffered -iE 'RTL8188EU|rtw88:|AirPortRTW|panic' \
     | grep --line-buffered -vE 'apple80211Request|SCAN_REQ coalesced|Sandbox|so_gencnt|AWDL receive mode|scan start returnState' \
     | awk '!seen[$0]++ { print; fflush() }' >> "$OUT"
