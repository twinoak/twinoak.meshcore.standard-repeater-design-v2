#!/usr/bin/env bash
# Provision one RAK4630 through the D-sub fixture (PROVISIONING-FIXTURE.md). Run on the Pi Zero.
#   ./provision-rak.sh identify                 read FICR part/variant/device id
#   ./provision-rak.sh recover                  mass erase through the CTRL-AP (only if identify says locked)
#   BOOTLOADER_HEX=x.hex ./provision-rak.sh flash   full erase + program + verify + reset (asks first)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CFG="${OPENOCD_CFG:-$HERE/openocd-pizero-dsub-fixture.cfg}"
LOGDIR="${LOG_DIR:-$HOME/rak-provisioning}"
mkdir -p "$LOGDIR"
ts() { date -u +%Y-%m-%dT%H:%M:%SZ; }
ocd() { sudo openocd -f "$CFG" -c "$1" 2>&1; }

read_word() {  # $1 = address, prints the 32-bit word as 8 hex digits, or nothing
  ocd "init; mdw $1; exit" | sed -n "s/^$1: \([0-9a-fA-F]\{8\}\).*/\1/p" | head -n1
}
ascii() { python3 -c "import sys;w=int(sys.argv[1],16);print(w.to_bytes(4,'big').decode('ascii','replace'))" "$1"; }

identify() {
  local part variant id0 id1
  part=$(read_word 0x10000100); variant=$(read_word 0x10000104)
  id0=$(read_word 0x10000060); id1=$(read_word 0x10000064)
  if [ -z "$part" ]; then
    echo "No answer from the chip. Check: unit powered (SW1 ON), fixture on conn-B J1, GND."
    echo "If OpenOCD printed an AP/APPROTECT lock message above, run: $0 recover"
    ocd "init; exit" | tail -n 15
    return 1
  fi
  DEVID="${id1}${id0}"
  echo "INFO.PART    = 0x$part"
  echo "INFO.VARIANT = 0x$variant ($(ascii "$variant"))"
  echo "DEVICEID     = $DEVID"
  echo "$(ts) identify part=0x$part variant=$(ascii "$variant") cfg=$(basename "$CFG")" >> "$LOGDIR/$DEVID.log"
}

case "${1:-identify}" in
  identify) identify ;;
  recover)
    read -r -p "Mass-erase this chip through the CTRL-AP (wipes everything incl. UICR)? [yes/N] " a
    [ "$a" = "yes" ] || { echo "aborted"; exit 1; }
    ocd "init; nrf52_recover; exit" | tail -n 20
    echo "$(ts) recover" >> "$LOGDIR/unknown-$(date +%s).log"
    echo "Now run: $0 identify" ;;
  flash)
    [ -n "${BOOTLOADER_HEX:-}" ] && [ -f "$BOOTLOADER_HEX" ] || { echo "set BOOTLOADER_HEX=<bootloader+S140 6.1.1 hex>"; exit 1; }
    identify || exit 1
    read -r -p "Full erase and flash $(basename "$BOOTLOADER_HEX") onto $DEVID? [yes/N] " a
    [ "$a" = "yes" ] || { echo "aborted"; exit 1; }
    out=$(ocd "init; halt; nrf5 mass_erase; program $BOOTLOADER_HEX verify; reset run; exit")
    echo "$out" | tail -n 20
    if echo "$out" | grep -q "Verified OK"; then
      echo "$(ts) flash OK $(basename "$BOOTLOADER_HEX") sha256=$(sha256sum "$BOOTLOADER_HEX" | cut -c1-16)" >> "$LOGDIR/$DEVID.log"
      echo "Done. Double-tap reset on the unit (or power-cycle) and check for the UF2 drive / bootloader."
    else
      echo "$(ts) flash FAILED $(basename "$BOOTLOADER_HEX")" >> "$LOGDIR/$DEVID.log"
      echo "Flash did not verify. Try a lower 'adapter speed' in $(basename "$CFG")."; exit 1
    fi ;;
  *) echo "usage: $0 identify|recover|flash"; exit 2 ;;
esac
