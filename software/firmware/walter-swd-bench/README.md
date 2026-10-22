# Walter SWD bench tool

*2026-10-20. The Walter provisions a factory RAK4630 (RUI3) with the RAK4630 bootloader + SoftDevice S140
6.1.1, over SWD, through the production chain:*

```
Walter ─ adapter ─ LTE D-sub ─ conn-B J1 ─ ribbon ─ conn-A (470 Ω) ─ LoRa D-sub ─ platform ─ RAK adapter
GPIO1 nRESET (pin 3) · GPIO2 SWCLK (4) · GPIO4 SWDIO (5) · GPIO5 KILL (6) · GPIO7 TX (8) · GPIO8 RX (9)
```

> **Superseded when the Walter firmware's phase 5 lands** (`rak provision`, `spec/SYSTEM.md` S-5a). Until then this
> is the way to give the first radios their base from a Walter; afterwards it stays as an independent reference.

A **bench tool**, run by a person from a console. It is not the Walter's production firmware, and it does not
change the spec: S-5 forbids *automatic* chip erases, and this one only erases on the typed command
`provision yes`. It is the bench version of step 3 in `software/spec/proposals/04-the-walter-provisions-a-new-radio.md`,
and its SWD engine (`src/swd.cpp`, `src/nrf52.cpp`) is meant to be carried into the production Walter firmware.

**Status:** host-tested against a bit-level simulated nRF52840 (`host-test/`, 15 scenarios) and compiled for the
ESP32-S3 against Arduino-ESP32 2.0.17. **Not yet run on hardware.** The first run on the real chain is the test.

## What you need

1. **The image.** RAKwireless' WisBlock repo, `bootloader/RAK4630/`: the combined bootloader + S140 6.1.1 `.hex`.
   Put it in `images/` (see `images/README.md`). The build refuses a bootloader-only hex.
2. **The unit, as in the field** (`hardware/pcb/BRING-UP.md` step 7): LoRa box (RAK adapter on its platform) on
   conn-A J1, Walter adapter + platform on conn-B J1, the ribbon between the connector boards. Antennas on both
   radios. Power: bench supply **3.7 V, 300 mA limit** on J7, SW1 ON — **not the battery** until U3 on the Walter adapter is reworked (`BRING-UP.md` step 6). Then the Walter's USB-C to
   the laptop. (On the boards of 2026-10-10, U3 lets the USB's 5 V onto the battery rail: harmless with the lab supply,
   not with a cell.)
   - *Not assembled yet?* The firmware doesn't care what is between the pins. A bare Walter on its own USB, wired
     to the RAK adapter's interface rows as in `hardware/pcb/BENCH-BREADBOARD.md` §3 (470 Ω in series, GND
     common) with the adapter on 3.30 V from the bench supply, runs the same session. Power the RAK first.
3. **PlatformIO** (the same as for NoiseScope).

## Session

```
pio run -t upload          # Walter's USB-C; if it stays in download mode, unplug/replug the Walter
pio device monitor         # type 'help'
```

| Step | Command | Good answer | If not |
|---|---|---|---|
| 1 | `probe` | nRESET and SWDIO *held HIGH*, SWCLK *held LOW*: "the RAK is powered and the chain is connected" | all *held LOW*: the RAK is unpowered (`power on`, SW1, supply). All *floating*: the chain is open (D-subs, ribbon). |
| 2 | `idcode` | `DPIDR 0x2BA01477 … the SWD chain works` | `speed 20`, then `idcode` again. Still nothing: continuity of D-sub pins 4/5 end to end. |
| 3 | `info` | `access port LOCKED` (RUI3 or a rev 3 part) **or** the identity: `INFO.VARIANT "AAD0"` / `"AAF0"` | `CTRL-AP IDR …`: not an nRF52 on the far end. |
| 4 | `provision yes` | six steps, `PROVISIONED  DEVICEID …  port after reset: open`, under **1 minute** at 125 kHz | it stops and says why; nothing it does can brick the radio (see below). |
| 5 | RAK adapter USB-C → laptop | a UF2 drive and a COM port: the bootloader waits in DFU mode (no application yet) | `verify` compares the radio with the image. |
| 6 | flash MeshCore or NoiseScope as usual (UF2 / `adafruit-nrfutil`) | | |

Keep the console output of steps 3 and 4 from the first unit: `INFO.VARIANT` and the `port after reset` line
settle `SYSTEM.md` §10.2 (APPROTECT on revision 3 parts) and go into `PROJECT-KNOWLEDGE.md`.

## What `provision yes` does

1. **attach** — line reset, DPIDR must be `0x2BA01477`, the CTRL-AP IDR `0x02880000`, then APPROTECTSTATUS. If
   the port is open, the identity is read **before** anything is erased, and an unknown part or variant stops
   here with *nothing erased*.
2. **erase** — CTRL-AP ERASEALL (works on a locked part), wait for ERASEALLSTATUS. Then APPROTECTSTATUS decides:
   open → go on (a revision 3 part stays open until the next pin/power/brownout reset); closed → one soft reset
   through the CTRL-AP (a legacy part re-reads its erased UICR), attach again, still closed → stop. The identity
   is read now if it wasn't before; flash, NRFFW[0] and APPROTECT must read blank. The core is halted.
3. **write** — NVMC write enable, every non-blank image word (blank words are never written), TAR re-written at
   every 1 KB boundary, NVMC READY polled after every block. Then **UICR.APPROTECT = 0x5A only on a revision 3
   part** (INFO.VARIANT build code F; on a legacy part that value would *enable* protection, so it stays erased).
4. **verify** — every image word read back and compared, then UICR.APPROTECT.
5. **pin reset** — the only reset of the procedure, after everything is written and verified.
6. **attach again** — reports whether the bootloader kept the access port open.

**Known (2026-10-22): power-cycle the radio after `provision yes` or `info`.** On the first unit the bootloader
only started (LED, UF2 drive) after a power cycle. The tool leaves the radio in the nRF52's *Debug Interface
mode*: it never withdraws CDBGPWRUPREQ/CSYSPWRUPREQ, and step 5 clears halting debug in one DHCSR write instead
of resuming the core first. A power-on reset clears both. The Walter firmware's own driver does both properly
(`Nrf52::run()`, `Nrf52::close()`, called by `rak provision` before its pin reset); this stopgap is not fixed.

The variant table is a lookup, not a guess (`nrf52.cpp`, `protectKind`): build code A–D legacy, F revision 3,
anything else stops. Every step that fails stops, and every stop leaves the radio where `provision yes` can start
again from the top: the only state it can leave behind is "erased", which is what it starts from anyway.

## Rules it keeps (`BENCH-BREADBOARD.md` §6)

- nRESET is open-drain: driven LOW or left Hi-Z, never driven HIGH.
- Between commands every RAK-facing line is Hi-Z; `power off` tristates them before it raises KILL.
- `reset` refuses a second reset within 1 s (two within 0.5 s put the RAK in its UF2 bootloader).
- No reset between ERASEALL and the UICR.APPROTECT write.
- SWD at 125 kHz by default — the nRF52840's specified minimum clock (its product specification, *Debug and trace*;
  the Walter firmware found it first) — `speed` 10–500 kHz; DPIDR reads clean through the filtered chain at 500 kHz (2026-10-22).
- Expect the RAK to reboot when the Walter powers up or is flashed (ESP32-S3 GPIO power-up glitch, caveat 3).

## Other commands

`verify` (radio vs. built-in image) · `reset` · `power off|on|cycle` (LoRa rail through the kill line) ·
`uart` (bridge to the RAK's Serial1 at 115200 8N1, Ctrl-X ends: RUI3 leaves these pins in Custom Mode and answers nothing; watch NoiseScope or the repeater's console) ·
`speed [kHz]` · `image`.

## Files

| Path | What |
|---|---|
| `src/link.h` | what the SWD engine needs from a board: lines, open-drain nRESET, release, time, console |
| `src/swd.cpp` | bit-banged SWD: line reset, request/turnaround/ACK/parity, WAIT retry, FAULT and sticky-error handling, posted AP reads, 1 KB TAR wrap |
| `src/nrf52.cpp` | nRF52840: CTRL-AP, FICR/UICR identity, ERASEALL, NVMC write, verify, the provisioning sequence |
| `src/main.cpp` | the Walter: GPIO register access, console, probe, kill line, UART bridge |
| `tools/hex2image.py` | pre-build: `images/*.hex` → `src/image_data.gen.cpp` (checked: MBR, SoftDevice, NRFFW[0], no APPROTECT) |
| `host-test/` | `run.sh`: the same `swd.cpp`/`nrf52.cpp` against `sim_target.cpp`, a simulated nRF52840 (SW-DP protocol incl. DPIDR-first and turnarounds, WAIT injection, sticky errors, AHB-AP 1 KB wrap, CTRL-AP, NVMC write rules, legacy and improved APPROTECT, resets) |

`host-test/run.sh` needs `g++` and `python3`; it also checks that every scenario drives SWDIO from one end at a
time and never violates the protocol.
