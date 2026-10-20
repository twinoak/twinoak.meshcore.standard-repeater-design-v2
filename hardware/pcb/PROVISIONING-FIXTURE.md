# RAK provisioning fixture — through the LTE-side D-sub

*2026-10-20. Every RAK4630 stamp ships with RAK's RUI3 firmware and has to get the Arduino bootloader +
SoftDevice S140 6.1.1 once, over SWD (`BRING-UP.md`, RAK adapter step 4). This fixture does that on a
**fully assembled, closed unit**: it replaces the LTE box on connector B's D-sub and drives the same chain
the Walter will use for the rest of the unit's life. It needs no board change and conforms to the frozen
interface (`INTERFACE-FREEZE.md`).*

> **Status:** the Walter now does the same job from its own socket with `software/firmware/walter-swd-bench`
> (a bench tool run by a person; written 2026-10-20, not yet tried on hardware). This fixture is the independent
> reference for that first run and stays as the recovery tool. The production Walter firmware gets it as the
> console command `rak provision` (`software/spec/SYSTEM.md` S-5a, decided 2026-10-20; its phase 5).

## What it is

A female DE-9 (the same gender as the LTE box's bulkhead) wired to a Raspberry Pi Pico running debugprobe
(preferred, below) or to a Raspberry Pi Zero's GPIO header (the original variant).
Plug it onto **connector B J1** in place of the LTE box. Power the unit as it is powered in the field:
bench supply on J7 (3.7 V, 300 mA limit) or the real battery, SW1 ON. Connector A then makes 3V3_LORA, so the
RAK runs exactly as deployed.

```
Pi Zero ──DE-9 fixture──► conn-B J1 ─ribbon─► conn-A (470 Ω on 3/4/5/8/9) ─► LoRa bulkhead (1000 pF) ─► platform ─► RAK
```

## Pin map

| DE-9 pin | Net (LTE side) | Pi Zero | Notes |
|---|---|---|---|
| 1 | GND | pin 20 (GND) | also tie the backshell; SH is GND on both connector boards |
| 2 | VLOAD+ (raw battery) | **not connected — never** | live battery rail |
| 3 | LORA_nRESET | pin 12 (GPIO18) | open-drain in OpenOCD, never driven high |
| 4 | LORA_SWDCLK | pin 22 (GPIO25) | |
| 5 | LORA_SWDIO | pin 18 (GPIO24) | |
| 6 | LORA_PWR_KILL | *optional* pin 16 (GPIO23) | GPIO23 boots pulled down = LoRa rail ON. Only for a kill test, on the LTE side only |
| 7 | spare / NC | not connected | |
| 8 | WALTER_TX → RAK RX | pin 8 (GPIO14, TXD) | 115200 8N1 |
| 9 | WALTER_RX ← RAK TX | pin 10 (GPIO15, RXD) | |

**Safe on the wrong connector.** Pins 1, 3, 4, 5, 8, 9 and SH mean the same thing on conn-A J1 and conn-B J1;
the fixture leaves 2 and 7 open (and 6 open unless you fit the kill wire). Plugged onto conn-A J1 by mistake it
still works, without the 470 Ω and with one filter fewer, and it harms nothing.

Build: solder-cup female DE-9 + backshell, about 30 cm of 7-core cable, a 2×20 female header for the Pi.
Use a spare **FCE17-E09SM** (the filtered bulkhead) instead of a plain DE-9 if you want the fixture's SWD to see
exactly what the Walter sees (both bulkheads' 1000 pF). With a plain DE-9 there is one filter fewer.

## Pico variant (preferred, Picos ordered 2026-10-20)

Same DE-9, wired to a Raspberry Pi Pico running Raspberry Pi's **debugprobe** firmware (hold BOOTSEL, plug in,
copy `debugprobe_on_pico.uf2`). The Pico becomes a CMSIS-DAP probe **and** a USB serial port, so the whole rig
runs from the laptop with pyOCD, with no Pi to set up.

| DE-9 pin | Net | Pico |
|---|---|---|
| 1 + shell | GND | any GND pin |
| 2 | VLOAD+ | **not connected — never** |
| 3 | LORA_nRESET | not needed: pyOCD resets over SWD. Leave open |
| 4 | SWDCLK | GP2 |
| 5 | SWDIO | GP3 |
| 6 / 7 | kill / spare | not connected |
| 8 | to the RAK's RX | GP4 (debugprobe UART TX) |
| 9 | from the RAK's TX | GP5 (debugprobe UART RX) |

Do not connect the Pico's 3V3 or VSYS to the fixture; the Pico is powered from the laptop's USB.

Per unit (laptop, `pip install pyocd`; `-f 100k` because of the 470 Ω + filters in the chain):

```
pyocd list
pyocd cmd   -t nrf52840 -f 100k -c "read32 0x10000104"   # INFO.VARIANT; also read32 0x10000060 / 0x10000064 = device ID
pyocd erase -t nrf52840 -f 100k --chip                   # full erase; pyOCD unlocks a locked nRF52 by mass erase
pyocd flash -t nrf52840 -f 100k <bootloader+S140 6.1.1>.hex
pyocd reset -t nrf52840 -f 100k
```

Raise `-f` in steps (150k, 200k, 300k) and record the highest that still verifies, because that is the Walter's
number. The debugprobe's serial port (115200) is a console to the RAK through pins 8/9. Keep a line per stamp
(device ID, variant, date, result) in a log. `tools/provision-rak.sh` does this on the Pi Zero variant; a
pyOCD equivalent can follow once the rig exists.

## Electrical rules

- **SWD speed through the chain: start at 100 kHz** (470 Ω + ~2.2 nF ≈ 1 µs time constant). The ceiling found
  here is the number the Walter's SWD engine needs (`PROJECT-KNOWLEDGE.md` §7, predicted ≤ ~300 kHz, unproven).
  Record it.
- **Power the unit before starting OpenOCD, and stop OpenOCD before switching the unit off.** The Pi's GPIOs
  idle as inputs. The 470 Ω cap back-feed at ~7 mA either way, but don't rely on it.
- Pin 2 never connects to anything on the fixture. The Pi gets its own power from USB.

## Pi Zero setup (once)

1. Raspberry Pi OS (Bookworm): `sudo apt install openocd python3`. Check `openocd --version` ≥ 0.12.
2. UART for pins 8/9: `sudo raspi-config` → Interface → Serial port → login shell **No**, hardware **Yes**.
   On a Zero W also add `dtoverlay=disable-bt` to `/boot/firmware/config.txt` so GPIO14/15 get the full UART.
3. Copy `tools/openocd-pizero-dsub-fixture.cfg` and `tools/provision-rak.sh` to the Pi, together with the
   bootloader hex (bootloader + S140 6.1.1, from RAK's WisBlock repo, `bootloader/RAK4630`).

## Per unit

```
./provision-rak.sh identify                 # FICR part / variant / device ID; tells you if the port is locked
./provision-rak.sh recover                  # ONLY if identify says locked (mass erase through the CTRL-AP)
BOOTLOADER_HEX=… ./provision-rak.sh flash   # full erase + bootloader/SoftDevice + verify + reset; asks first
```

Each run appends to `~/rak-provisioning/<device-id>.log`, so every stamp's variant and provisioning date are on
record. That's the start of the unit's history in MeshMaster.

**Open before the first real use (same items as the Walter's flasher, `software/spec/SYSTEM.md` §10):**
- APPROTECT on rev 3 parts: read the variant on the first stamp. If the port re-locks after a reset, the
  UICR.APPROTECT write has to be added to the `flash` step. Bring the `identify` output back first.
- Putting the *application* (NoiseScope, later the repeater firmware) on over SWD as well depends on the Adafruit
  bootloader's settings page (§10.3). Until that is settled, put the application on through the UF2 drive or
  `adafruit-nrfutil` after the bootloader, or let the Walter do it once its flasher exists.

## Why it's worth building

- Provisioning without opening the RF-shielded LoRa box.
- Every unit's SWD path (bulkhead, conn-A series resistors, ribbon, conn-B) is proven at the end of its build,
  before the Walter depends on it.
- The UART on pins 8/9 gives the bench a console to the RAK through the same path the Walter's link uses.
- It is the known-good reference to compare against when the Walter's SWD engine is brought up.
