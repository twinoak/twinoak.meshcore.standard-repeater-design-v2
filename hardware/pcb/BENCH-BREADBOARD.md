# Repeater v2 — Bench / Breadboard Rig (Walter ↔ RAK4630)

Firmware-development stand-in for the five production boards. Replicates every **signal** net of
`INTERCONNECT.md` between the Walter and the RAK4630 (nRESET, SWDCLK, SWDIO, UART, optional I2C/KILL)
using off-the-shelf boards, so Walter firmware written against this rig runs unchanged on the
production adapters. Power management (BQ24650, TPS63001, ideal diode, kill circuit) is deliberately
**not** replicated — see §6 for what that changes.

Written 2026-08-30 from the production netlists (`adapter-walter/production/netlist.ipc`,
`adapter-rak4630/production/netlist.ipc`, `connector-board-a/production/netlist.ipc`) plus the Walter
datasheet rev 0.9 / schematic v1.6 and the RAK4630/RAK4631/RAK19001 datasheets.

## 1. Hardware

| Item | Notes |
|---|---|
| **Walter** (QuickSpot, ESP32-S3 + GM02SP) | 2×14 pins, 2.54 mm, row spacing 17.78 mm (0.7") — straddles a breadboard centre channel. Own USB-C = native S3 USB-Serial/JTAG (flash + console), no UART bridge chip on board. |
| **RAK4631 WisBlock Core** (= RAK4630 stamp on a carrier) on **RAK19001 base** | Does not go on the breadboard; jumper wires from the base's 2×10 headers on the **bottom side** (plated holes, unpopulated on the boards I have seen — solder headers) and from the core's SWD pads. Keep the base's **battery selector on the rechargeable side** (§6, caveat 5). Base USB-C = nRF native USB (UF2 drive / CDC console). |
| 2× USB-C cables, one host (laptop or hub) | Both boards on the **same** host so the two consoles and a common ground come for free. |
| 5× 470 Ω + 5× 2.2 nF (or 10× 1 nF) | Optional but recommended: emulate the A-board series resistors and the two 1000 pF bulkhead filters (§4). |
| LoRa antenna on the RAK4631 LoRa u.FL (and a BLE antenna if the firmware brings BLE up); LTE antenna on the Walter | RAK: "you can damage the RF section … if you power the module without an antenna connected". Same for the Sequans PA. |
| Optional: RAK12002 (RV-3028) in a sensor slot, INA3221 + BME280 breakouts on I2C1 | Mirrors the production I2C bus (0x52 / 0x42 / 0x76) for MeshCore auto-detect and telemetry code. |
| Optional: LED + 4.7 k on the kill line; later a P-FET high-side switch | §5. |

## 2. Power — the short answer

**Yes: power each board from its own USB-C port and share nothing but GND.** That is the whole power
wiring. Do not connect Walter `VIN`/`3V3` or RAK19001 `VBUS`/`3V3`/`VDD`/`VBAT` to anything.

Why not a single cable:

* Walter's `3V3` pin is a **switched 250 mA output that is OFF at boot** (P-FET gated by GPIO0 —
  the DFU strap — active-low). Not a supply for the RAK base.
* Walter's USB-C `VBUS` is **hard-wired to `VIN`** — no diode, no fuse (datasheet: "DO NOT power
  Walter with both the USB-C connection and the VIN-pin!"). Feeding VIN from the RAK base means
  losing the Walter's USB console/flash unless you cut VBUS in a USB cable. The production adapter
  solves this with the LM66100 (CE on VOUT; the boards of 2026-10-10 need the `BRING-UP.md` step 6 rework); the
  bench doesn't have one.
* The GM02SP modem draws its TX bursts **straight from VIN/VBUS** (only the S3 sits behind the
  buck; published ceiling 1.5 A). The RAK19001's `VBUS` header pin is the raw 5 V of a USB port that
  also has to run the base and the charger — don't hang an LTE modem on it.
* The bare RAK4630 stamp **cannot** run from 5 V alone (VBUS only powers the nRF USB PHY; the
  SX1262 rails are 3.7 V recommended max, 3.9 V absolute max) — but on the bench the RAK19001's buck
  makes the 3.3 V from USB and its charger floats the VBAT rail at ~4.2 V, so "RAK on its own USB"
  simply works.

Domains on the bench: `5 V USB → Walter VIN → TPS62088 → +3.3 V (S3)` and
`5 V USB → RAK19001 buck → 3V3 (core)`. Both sides are 3.3 V logic, neither is 5 V tolerant — same
as production, so no level shifting anywhere.

## 3. Pin map (production net → bench wire)

Walter pin numbers are **datasheet numbering (1–28)** = the pin numbers of `DPT_Walter` in
`lib/TwinOak.kicad_sym` = U1 pads in the adapter netlist. The Walter's **silkscreen prints P-numbers,
not GPIOs** (`RST, 0…23, 3V3, GND, VIN`; datasheet pin = silk + 2) — verify against your module before
trusting the "silk" column.

RAK19001 names are the bottom-side silk labels of its two 2×10 headers. RAK4631 SWD pads: the column
on the core's left edge next to the BLE u.FL — RAK's wiring drawing shows four pads, top→bottom
**GND, SWCLK, SWDIO, 3V3** (square pad = 3V3); the RAK4631 schematic draws the same header as 1×5
(3V3, SWDIO, SWCLK, RST, GND), so newer cores may have a fifth `RST` pad. Confirm with a meter (GND
continuity, square pad = 3V3) before soldering. Leave the 3V3 pad unconnected (it is the core's rail,
not an input); if an RST pad exists it is the same net as the base's `RESET` header pin.

| Net (INTERCONNECT.md) | D-sub | Walter pin / silk / GPIO | ↔ | RAK bench point | nRF52840 | Production RAK path |
|---|---:|---|---|---|---|---|
| GND | 1 | 27 / `GND` | — | RAK19001 `GND` (any) + RAK4631 pad 1 | — | — |
| LORA_nRESET | 3 | **14 / `12` / GPIO1** | → | RAK19001 `RESET` | nRESET (P0.18) | U4.17 NRF_RESET + SW1 |
| LORA_SWDCLK | 4 | **13 / `11` / GPIO2** | → | RAK4631 pad 2 `SWCLK` | SWDCLK | U4.18 |
| LORA_SWDIO | 5 | **15 / `13` / GPIO4** | ↔ | RAK4631 pad 3 `SWDIO` | SWDIO | U4.19 |
| LORA_PWR_KILL | 6 (LTE side) | **16 / `14` / GPIO5** | → | nothing (or §5 emulator) | — | A-board R12 10k → Q3 2N7002 → TPS63001 EN |
| SPARE | 7 (LTE side) | 17 / `15` / GPIO6 | — | NC | — | NC |
| WALTER_TX → LORA_UART_RX | 8 | **18 / `16` / GPIO7** | → | RAK19001 `RXD1` (J4 1–2 = Serial1) | P0.15 | U4.6 via J4 |
| WALTER_RX ← LORA_UART_TX | 9 | **23 / `21` / GPIO8** | ← | RAK19001 `TXD1` (J5 1–2 = Serial1) | P0.16 | U4.7 via J5 |
| *(Serial2 alternative, J4/J5 2–3)* | 8 / 9 | GPIO7 / GPIO8 | | RAK19001 `RXD0` / `TXD0` | P0.19 / P0.20 | U4.9 / U4.10 |
| I2C_SCL | 6 (LoRa side) | — | | RAK19001 `I2C1_SCL` → RTC/INA/BME breakouts | P0.14 | U4.5, R1 1k pull-up |
| I2C_SDA | 7 (LoRa side) | — | | RAK19001 `I2C1_SDA` | P0.13 | U4.4, R2 1k pull-up |
| 3V3_LORA | 2 (LoRa side) | — | | RAK19001 makes its own from USB | — | A-board TPS63001 |
| VLOAD+ | 2 (LTE side) | 28 / `VIN` — **leave open** | | — | — | LM66100 → WALTER_VIN |

Naming trap on the RAK side: **RAK's module datasheet calls P0.15/P0.16 "UART2" and P0.19/P0.20
"UART1"; the Arduino BSP (and MeshCore, noisescope) call P0.15/P0.16 `Serial1` and P0.19/P0.20
`Serial2`.** The RAK19001 header labels follow the base-board convention: `TXD1/RXD1` = Serial1,
`TXD0/RXD0` = Serial2. The table above is in Arduino/`Serial1` terms, matching J4/J5 on the adapter.

Walter recovery header (adapter J1) has no bench equivalent and needs none: the Walter's own USB-C
is the native S3 USB, exactly what J1 backs up. `IO0` (pin 4 / silk `2`) is the DFU strap — hold low
during reset/power-up to force the ROM bootloader.

```
   Walter (datasheet pin / GPIO)                                  RAK4631 on RAK19001
   13 IO2  GPIO2  ──470R──●───────────────────────▶  core pad 2  SWCLK
   14 IO1  GPIO1  ──470R──●───────────────────────▶  RAK19001 RESET        (open-drain drive only)
   15 IO4  GPIO4  ──470R──●──────────────────────◀▶  core pad 3  SWDIO
   16 IO5  GPIO5  ────────────────────────────────▶  LED / kill emulator (§5), else open
   17 IO6  GPIO6   NC
   18 IO7  GPIO7  ──470R──●───────────────────────▶  RAK19001 RXD1         (P0.15, Serial1 RX)
   23 IO8  GPIO8  ──470R──●──────────────────────◀   RAK19001 TXD1         (P0.16, Serial1 TX)
   27 GND         ────────────────────────────────── RAK19001 GND + core pad 1 GND
    4 IO0, 26 3V3, 28 VIN: leave open              ● = optional 2.2 nF to GND at the RAK end (§4)
   USB-C ◀── laptop / powered hub ──▶ USB-C (RAK19001)
```

## 4. Making the bench electrically honest (recommended)

Production puts **470 Ω in series** (A-board R1–R5) and **two 1000 pF bulkhead filters (~2.2 nF with
wiring)** on each of nRESET / SWDCLK / SWDIO / UART_RX / UART_TX. τ ≈ 1 µs. Reproduce it on the
breadboard: 470 Ω at the Walter end of each of the five lines, 2.2 nF (or 2 × 1 nF) from each line to
GND at the RAK end. Consequences you want to hit on the bench rather than in the field:

* **Bit-banged SWD must stay ≤ ~300 kHz; start at 100 kHz.** A flasher tuned at 1 MHz on a bare
  breadboard will fail through the filtered D-subs. SWDIO turnaround (target ACK bits) is the tight
  spot, not SWCLK.
* UART at 115200 (8.7 µs bits) is untouched by τ ≈ 1 µs.
* The 470 Ω also caps back-feed into an unpowered RAK at ~7 mA (§6).

If you also bring the I2C bus up, use **one set of 1 k pull-ups to the RAK's 3V3** (as on the adapter)
and 2.2 nF on SDA/SCL too — the 1 k value only makes sense with the filter capacitance; with bare
wires 4.7 k is normal, but 1 k is what production has.

## 5. Kill line (LORA_PWR_KILL, Walter GPIO5)

Production polarity (A-board): `GPIO5 → R12 10 k → Q3 2N7002 gate (R13 100 k to GND) → TPS63001 EN`.
**HIGH = LoRa rail OFF; floating / Walter unpowered = ON.**

* Phase 1 (no consumer): `GPIO5 → 4.7 k → LED → GND`. LED on = "LoRa rail would be dead". Enough to
  develop the firmware sequencing (tristate nRESET/SWD/UART → assert kill → wait → release → re-init).
* Phase 2 (real power-cycle test): feed the RAK19001's battery input (`VBAT` header pin or JST-PH,
  battery selector on *rechargeable*, 3.7–4.2 V from a bench supply, or 5 V with the selector on
  *non-rechargeable*) through a P-FET high-side switch: `GPIO5 → 10 k → 2N7000 gate (100 k to GND);
  2N7000 drain → P-FET gate (100 k to source); P-FET source = supply, drain = RAK19001 VBAT`. Same
  truth table as production. The RAK's USB then has to be a **VBUS-cut cable** (data only), because
  USB VBUS beats the battery in the base's power path and would keep the core alive. This is the same
  cable trick the Walter docs recommend.

## 6. Caveats — read before plugging in

1. **Two independent supplies ⇒ one side can drive a dead side.** Whichever board is powered first
   pushes 3.3 V into the other's pins through its ESD diodes (nRF52840 and ESP32-S3 alike), which
   half-powers it and can wedge it. Production caps this at ~7 mA with the 470 Ω; without them on the
   bench, plug the RAK in first and out last, and keep the production firmware rule: **Walter tristates
   its five RAK-facing pins whenever it believes the RAK is unpowered.**
2. **nRESET is open-drain only.** nRF has an internal ~13 k pull-up; the RAK19001 reset button and
   the core's power-up reset circuit pull the same line low. Walter GPIO1 = `OUTPUT LOW` to assert,
   `INPUT` (Hi-Z) to release — never `OUTPUT HIGH`.
3. **Expect the RAK to reboot when the Walter powers up.** GPIO1 (nRESET) — and GPIO2/4/5/7/8 —
   are in the ESP32-S3 datasheet's "power-up glitch" list (GPIO1–14, 17, 18: ~60 µs low pulse).
   The production path (470 Ω, ~2.2 nF, no cap on the adapter's NRESET net) passes it straight to
   the nRF, so a Walter power-cycle in the field reboots the LoRa side. The bench core has 10 k/100 nF
   on RST (RAK4631 R6/C5), so through 470 Ω the 60 µs pulse only gets the line down to ~1 V — it may
   or may not reset on the bench. Either way: not a firmware bug. GPIO38–42 are not on the glitch
   list if a future rev wants to move nRESET.
4. **Two resets < ~0.5 s apart put the RAK into the UF2 bootloader** (Adafruit double-tap-reset
   detection). It then sits on the USB drive, Serial1 silent, until the next single reset. Space
   Walter-initiated resets ≥ 1 s apart, and never "reset twice to be sure".
5. **Bench RAK4631 runs the nRF52840 in high-voltage mode from the base's VBAT rail; the production
   adapter runs it in normal mode** (adapter ties VDD_NRF, VBAT_NRF, VBAT_SX, VBAT_IO_SX all to 3V3;
   the RAK4631 schematic feeds VBAT_NRF from `VBAT` via R1 0 Ω, the SX rails from 3V3, and leaves
   VDD_NRF to REG0). Two consequences:
   * On a RAK19001 running from USB with no battery, `VBAT` is only alive because the TP4054 charger
     floats it at ~4.2 V — **with the battery selector on the non-rechargeable side and nothing
     plugged in, VBAT floats, the nRF has no VDDH and the core is dead while the SX1262 rails are
     live.** Selector on the rechargeable side; if the core won't enumerate on USB, flip it.
   * If your SWD flasher ever **erases UICR** (CTRL-AP ERASEALL / mass erase), the bench core's
     `REGOUT0` falls back to its erased default and the GPIO domain drops to **1.8 V** — the Walter
     (V_IH 2.5 V) stops reading SWDIO/TX highs and 3.3 V drive into 1.8 V pins exceeds their abs-max.
     Production is immune. Rule for the flasher: page-erase the application region only (keep
     bootloader + UICR); if you must mass-erase, immediately re-flash the full `bootloader+S140`
     hex — its first boot rewrites `REGOUT0 = 3V3` and resets.
6. **APPROTECT may be ON.** nRF52840 **rev 3 (build code Fx0, FICR `INFO.VARIANT` = "AAF0")** ships
   with hardware-enabled access-port protection unless `UICR.APPROTECT == 0x5A`; the Adafruit/RAK
   bootloader never writes that word, and the nrfx MDK only copies UICR → `APPROTECT.DISABLE`. On such
   a module a Walter SWD session will see the DP (IDCODE ok) but the AHB-AP refuses — the only way in
   is CTRL-AP ERASEALL (wipes bootloader + UICR ⇒ caveat 5 on the bench), then flash everything and
   write `UICR.APPROTECT = 0x0000005A` so it stays open — and keep nRESET **released** between the
   ERASEALL and that UICR write: Nordic says the port stays open only "until it executes a pin,
   power, or brownout reset". This is a **module-batch property, not a board property** — the
   production stamps from JLC are just as likely to be rev 3 as the bench core. First thing to do on
   the bench: read `INFO.VARIANT` (from MeshCore/noisescope over USB, or pyOCD) and try an AHB-AP
   read from the Walter; then design the flasher's erase path accordingly.
7. **RAK USB behaves differently on the two boards.** RAK19001: USB alone boots the core (its buck
   feeds 3V3). Production adapter: the USB-C's VBUS only reaches the nRF USB PHY (U4.1); the 3V3 comes
   from the harness. With the rail killed, a laptop on the field USB-C does **not** wake the RAK.
   Don't let bench habits mislead field debugging.
8. **Walter `VIN` and `3V3` pins stay open on the bench.** VIN = USB VBUS (§2); 3V3 is a switched
   output. Don't tie either to the RAK base's `VBUS`/`3V3`/`VDD`/`VBAT` (on a RAK4631 the connector's
   `VDD` pins are simply 3V3; `VBAT` is the nRF's VDDH rail — §5 phase 2 is the only reason to drive
   it, and then from a supply of your own, never from the Walter).
9. **IO2 on the RAK19001 switches its `3V3_S` rail** (P1.02, `WB_IO2`). RAK's examples and the
   RAK4631 board init in MeshCore drive it HIGH for sensor power; on the production adapter P1.02 is NC. Irrelevant to this rig unless you plug
   sensor modules into the base, in which case they are on `3V3_S` and only live when IO2 is HIGH.
10. **Walter USB-Serial/JTAG disappears in deep sleep** and there is no DTR/RTS auto-reset circuit;
    if the S3 stays in download mode after a flash, power-cycle it (or `IO0` high + RST). If your
    Walter firmware ever needs a console that survives deep sleep, that is what the production J1
    header (TX0/RX0 = GPIO43/44) is for — on the bench a USB-UART dongle on pins 2/3 does the same.
11. **Currents.** RAK core ~130–150 mA at +22 dBm; Walter up to 1.5 A on VIN during LTE-M bursts. Both
    are within a USB-C host port each, but a cheap unpowered hub feeding both plus the laptop's
    own limits can brown out the Walter mid-registration — the symptom is a modem that keeps
    re-attaching. Use two host ports or a powered hub.
12. **Antenna before power** on both radios (§1).

## 7. Bring-up order

1. RAK19001 alone on USB: UF2 drive appears on double-tap reset → flash MeshCore or noisescope →
   confirm the CDC console; note `INFO.VARIANT` (caveat 6).
2. Add GND + the two UART lines (with 470 Ω / 2.2 nF) → Walter on its own USB → `Serial1` traffic
   both ways at 115200 (`PROTOCOL.md`). This already proves the D-sub pin-8/9 path.
3. Add nRESET → Walter pulses it low 100 ms → RAK reboots once (watch for caveat 4).
4. Add SWDCLK/SWDIO → DP IDCODE (`0x2BA01477`) → AHB-AP read of `FICR` → decide erase strategy →
   flash a known image → verify by CRC/read-back → release reset → RAK boots the new image.
5. Only now add the kill emulator (§5) and the I2C breakouts if wanted.

## 8. Sources

* Adapter/connector netlists in this repo (`*/production/netlist.ipc`), `INTERCONNECT.md`.
* Walter datasheet rev 0.9 (2026-06-22) and schematic v1.6 — https://www.quickspot.io/datasheet/walter_datasheet.pdf ,
  https://github.com/QuickSpot/walter-hardware ; hardware notes / FAQ — https://github.com/QuickSpot/walter-documentation
* ESP32-S3 datasheet, power-up glitch table and GPIO electrical limits.
* RAK4630 datasheet/quickstart — https://docs.rakwireless.com/product-categories/wisduo/rak4630-module/datasheet/ ,
  https://docs.rakwireless.com/product-categories/wisduo/rak4630-module/quickstart/
* RAK4631 datasheet, RAK-nRF52-Arduino `variants/WisCore_RAK4631_Board/variant.h`, RAK4631 bootloader README
  (SWD pad drawing) — https://github.com/RAKWireless/WisBlock/blob/master/bootloader/RAK4630/README.md
* RAK19001 datasheet (header silk, schematics) — https://docs.rakwireless.com/product-categories/wisblock/rak19001/datasheet/
* Nordic: nRF52840 PS (power supply modes, USBD, GPIO), "Working with the nRF52 Series' improved APPROTECT",
  nrfx MDK `system_nrf52_approtect.h`.
