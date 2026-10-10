# TwinOak Standard Repeater v2 — Project Knowledge

*Map and memory of this repo for whoever (or whatever) picks it up next. Authoritative detail lives
in the files it points to; keep this one short and current. Last updated 2026-10-10.*

## 1. Status (2026-10-10)

- **Hardware:** five production boards (platform, connector A, connector B, RAK4630 adapter, Walter
  adapter) passed three pre-order preflights (`hardware/pcb/PREFLIGHT-REPORT-*.md`, 08-11 / 08-14 /
  08-20) and have Fabrication Toolkit exports from 2026-08-21 in each `production/` folder.
  **Nothing has been ordered from JLCPCB yet — no boards exist.**
- **Firmware:** **no Walter (ESP32-S3) firmware exists.** NoiseScope (`software/firmware/noisescope-rak4630`)
  is the only software in the repo. The repeater will run **custom MeshCore-based firmware** on the
  RAK4630; stock-MeshCore compatibility is not a design constraint.
- **Removed 2026-10-10:** `MeshCore.FleetManager` (spec) and `MeshCore.NightCrawler` (C# PoC) —
  deprecated; the management system is the separate MeshMaster project (meshmaster.dk).
- **Next step: validate the hardware design** — §9.
- Recent docs: `hardware/pcb/BENCH-BREADBOARD.md` (Walter + RAK4631/RAK19001 bench rig),
  `hardware/pcb/CURRENT-SENSE.md` (shunt design, decided: keep mixed shunts, per-channel values in firmware).

## 2. Purpose and architecture

Built after repeated OTA bricking of remote repeaters. Goals: (1) remote management over LTE-M
(management only, no mesh data over LTE), (2) RF-clean LoRa receiver — shielded boxes, proper coax,
(3) integrated cavity filter by default, (4) modularity — swap just the radio, panel or battery.

**Topology:** two Gainta G103-IP67 alu RF boxes inside one IP67 diecast enclosure. LoRa box =
`[LoRa module] – [LoRa adapter] – [platform board]`; LTE box = `[Walter] – [LTE adapter] – [platform board]`.
Each box exits through a filtered Amphenol FCE17-E09SM-250 DE-9 bulkhead (1000 pF/line). Outside
the boxes: **connector board A** (behind the LoRa D-sub: TPS63001 3V3_LORA buck-boost, kill
circuit, 470 R series protection, BME280) and **connector board B** (behind the LTE D-sub: BQ24650
MPPT charger, INA3221 telemetry, MCP1700 always-on LDO, master switch), linked by a 10-wire ribbon
(TE 8-215570-0).

**Level model:** Power → Platform/Platform-mgmt → Adapter → Radio module. Platform boards are
identical dumb pass-throughs; only adapters and connector boards give pins meaning.

**Radio choices:** LoRa = RAK4630-8-SM-I stamp (u.FL variant, no RF on PCB) — chosen for radio
chain + battery life + MeshCore support; E220/LLCC68 ruled out (no 62.5 kHz BW). Management =
QuickSpot Walter (ESP32-S3 + Sequans GM02SP LTE-M/NB-IoT); whether it is worth its cost vs. a
cheaper MCU was questioned in 2026-09 and is still open. Heltec V3 adapter exists but its PCB is
stale vs. its schematic (don't order); T096 adapter is an empty stub.

**DK channel:** 869.618 MHz, BW 62.5 kHz, SF8, CR8, +22 dBm (EU Narrow).

## 3. Board set (as exported 2026-08-21, not yet ordered)

| Board | Path | Rev / state | Assembly plan | Key parts |
|---|---|---|---|---|
| Platform baseboard | `hardware/pcb/platform-board` | v1.1 (J1 interleave remap 08-14) | Bare PCB, all THT hand-solder, **2 per repeater** | J1 10-pin IDC (82155700 fp), J2–J5 4×5 header rows |
| Connector board A | `hardware/pcb/connector-board-a` | 1.1 + BME280 (08-20) | JLC SMT **Standard** (BME280 forces it) | U4 TPS63001, L2 LPS4018 2.2 µH, Q3 2N7002 kill FET, R1–R5 470 R, U5 BME280 (0x76), J3 J_ENV 1×4 hand-fit |
| Connector board B | `hardware/pcb/connector-board-b` (flattened 2026-10-10; lib tables fixed to `../lib`) | 1.1 (08-14 spine widening, D4/D5, SH→GND) | JLC SMT; J2 MX126, J6 JST-PH (NTC), J7 JST-EH (batt), SW1 CAX toggle hand-fit | U1 BQ24650, U2 INA3221, U3 MCP1700-3.3, Q1/Q2 SI4840DY, L1 IHLP4040 10 µH, D4 SS54, D5 SMBJ26A, F1 3 A PTC, RS1/RS3 20 mΩ, RS2 100 mΩ, RS4 50 mΩ |
| RAK4630 adapter | `hardware/pcb/adapter-rak4630` | post-08-14 | JLC SMT; U2 interface headers, J4/J5 jumpers hand-fit | U4 RAK4630, U5 RV-3028-C7 (rot 180°), U3 USBLC6 (rot +90°), J3 USB-C XUNPU TYPEC-303-ACP16, SW1 Kinghelm reset, C7/C8 2×100 µF RTC backup, R1/R2 1 k I2C pull-ups |
| Walter adapter | `hardware/pcb/adapter-walter` | post-08-14 | JLC SMT: only C3, SW1, U3 LM66100DCKT; C1 470 µF polymer, C2 10 nF, J1 1×6 recovery header, U1 socket, U2 interface hand-fit | "USB-C THIS END" silk at socket pin-1/VIN end |

Shared libs: `hardware/pcb/lib/TwinOak.kicad_sym` + `TwinOak.pretty/`, referenced from every
project via `${KIPRJMOD}/../lib/...` in its `fp-lib-table` / `sym-lib-table` — **if a project
folder moves, fix those two files.** Design rules: 0.2 mm clearance (own rule; JLC min 0.127),
0.254 mm min track, 0.6/0.3 vias, 1.6 mm 2-layer, all SMD front side. KiCad 10 file format.
`outputs/` folders hold stale July gerbers — only `production/` (Fabrication Toolkit) is current.
Stock to re-check at order time: RAK4630 (47 pcs on 08-14), LPS4018 L2 (12 pcs), SI4840DY, CAX
toggle (zero JLC stock — LCSC/TME), BME280.

## 4. Interconnect essentials (full detail: `hardware/pcb/INTERCONNECT.md` — update it first when wiring changes)

**D-subs are deliberately NOT identical:**

| Pin | LoRa box | LTE box |
|---|---|---|
| 1 | GND | GND |
| 2 | 3V3_LORA (from A-board) | VLOAD+ (raw battery, INA ch3) |
| 3 | LORA_nRESET | ← Walter IO1 |
| 4 | LORA_SWDCLK (V3: PRG/GPIO0) | ← Walter IO2 |
| 5 | LORA_SWDIO | ↔ Walter IO4 |
| 6 | I2C_SCL | LORA_PWR_KILL ← Walter IO5 |
| 7 | I2C_SDA | spare / NC (Walter IO6) |
| 8 | LORA_UART_RX | WALTER_TX (IO7) |
| 9 | LORA_UART_TX | WALTER_RX (IO8) |

**A↔B ribbon (1:1):** 1 GND · 2 VBAT_RAW/VLOAD+ (unfused) · 3 nRESET · 4 SWDCLK · 5 SWDIO · 6 KILL
· 7 SDA · 8 WALTER_TX · 9 WALTER_RX · 10 SCL.

**Platform J1 → D-sub cups (v1.1):** conductor n lands on the n-th physical cup: **1,6,2,7,3,8,4,9,5**
(conductor 10 unused). Red stripe at the pin-1/pin-6 end; same map as a crimp-on IDC DE-9. Rows
A‖C and B‖D are paralleled on the platform.

**I2C:** RAK is the single master; RV-3028 @0x52 (RAK adapter), INA3221 @0x42 (B-board, A0→SDA),
BME280 @0x76 (A-board). **One pull-up set only: 1 k on the LoRa adapter** — because SDA/SCL cross
the 1000 pF bulkhead (τ ≈ 1.3 µs); keep ≤ 100 kHz. No pull-ups on A or B → a bare A- or B-board
I2C scan fails without the RAK adapter or temporary pull-ups.

**UART:** 115200 8N1 through 2×1000 pF — fine. RAK Serial1 = P0.15/P0.16 (J4/J5 jumpers 1-2,
MeshCore default); Serial2 = P0.19/P0.20 (2-3). Naming trap: RAK's datasheet calls these
"UART2/UART1" the other way round.

**SWD:** Walter bit-bangs through 470 R + ~2.2 nF → keep ≤ ~300 kHz, start at 100 kHz. nRESET is
open-drain only (OUTPUT LOW / INPUT, never OUTPUT HIGH).

**Kill line:** Walter IO5 → R12 10 k → Q3 gate (R13 100 k pull-down, R14 pull-up to VBAT_RAW) →
TPS63001 EN. **Floating / Walter unpowered = LoRa rail ON; HIGH = OFF.** Firmware rule: tristate
nRESET/SWD/UART before asserting kill; 470 R caps backfeed at ~7 mA.

## 5. Power tree

Panel (Cellevia CL-SM10P 10 W, Vmp 18.2 V, Voc ≈ 21.6 V, cold ≈ 24–25 V) → J2 `PANEL_IN` → D4 SS54
→ `PANEL_RAW` → SW1 → `PANEL+` → RS2 → `VIN` (BQ24650: MPPT 17.83 V via 499k/36k, VFB regulation
at **VCC** = 4.200 V via 499k/499k, 2.0 A fast charge from RS1 20 mΩ, precharge/termination 200 mA,
recharge 4.15 V, TS window ≈ 0.5…41 °C, NTC unplugged = charge suspended) · battery (Meshnology
10 Ah 1S LiPo) `BAT_RAW` → SW1 → `BAT+` → F1 → `VBAT` → RS3 → `VCC` → RS4 → `VLOAD+` → LTE D-sub 2
+ ribbon 2. **The load hangs on VCC between RS1 and RS3**, so the charger sees battery + load
current; termination only when both sum below 200 mA. INA3221 ch1 = panel (PANEL+→VIN over RS2
100 mΩ), ch2 = battery (VCC→VBAT over RS3 20 mΩ, positive = charging), ch3 = load (VCC→VLOAD+ over
RS4 50 mΩ); bus voltages read at VIN, VBAT, VLOAD+. MCP1700 always-on 3.3 V feeds the INA3221.
Master switch SW1 (2-pole) kills panel + battery → everything dead except RTC on backup caps.

A-board: `VBAT_RAW` → TPS63001 → `3V3_LORA` (≈1.2 A capability, LoRa side draws ≤ ~0.3 A). Walter:
`VCC` → LM66100 ideal diode → `WALTER_VIN` (+470 µF polymer); USB-C 5 V on the Walter wins the rail
and cannot back-charge the LiPo. Walter LTE bursts up to 1.5 A on VIN. Series drop pack→Walter at
an 1.8 A burst ≈ 0.25–0.35 V (RS3+RS4+F1+SW1+JST+LM66100) — check against the Walter's VIN minimum
at an empty pack. RTC backup 2×100 µF MLCC → hold-up tens of minutes to ~1.5 h, **only if firmware
sets RV-3028 TCE=1 and BSM=LSM (factory default OFF)**.

Energy: always-listening MeshCore node ≈ 8–15 mA at 3.3 V; 10 Ah → ~35–40 days no-sun autonomy for
a single-RAK unit.

## 6. Design decisions and the reasons (lore worth keeping)

- 3V3_LORA regulator moved from B to A-board (B ran out of room; also keeps switching noise out of
  the LoRa box — the regulator sits outside the box, the rail enters through the filter).
- Ribbon VBAT_RAW feed deliberately **unfused** (F2 dropped). PTC C438898 is the option if wanted.
- **Shunts (CURRENT-SENSE.md):** RS1 20 mΩ is the BQ24650 sense (40 mV → 2 A); RS2 100 mΩ / RS3
  20 mΩ / RS4 50 mΩ are INA3221 shunts each sized to its channel's max current (1.64 A / ±8.2 A /
  3.28 A full scale; 0.4 / 2 / 0.8 mA per LSB). All valid; kept mixed, firmware carries per-channel
  values. RS3 was kept low for battery-path impedance at the price of 2 mA/LSB. RS2 is a 0.25 W
  thick-film (±800 ppm/°C); RS1/RS3/RS4 are 1 W Milliohm sense parts.
- RV-3028 footprint saga: the 08-11 preflight claimed it was mirrored; TH proved via Micro Crystal
  eval-board photos that the **original footprint was correct**; restored 08-13, U5 rerouted twice,
  rotation offset 180° verified against the JLC pink-dot preview. Lesson: trust physical
  photos/STEP over datasheet text extraction.
- RAK4630 stamp chirality verified by TH against the datasheet (USB pins in the right place).
- Fabrication Toolkit reads the field name **`FT Rotation Offset`** (not "JLCPCB Rotation
  Offset"). Offsets: RAK U3 +90°, RAK U5 180°, conn-B U1 −90°.
- D-sub shield pads tied to GND on both connector boards (uniform shield-bond scheme).
- USB-C receptacle (drawn for 0.8 mm PCB) kept on 1.6 mm board — shell tabs solder from the top;
  maintenance-port duty only.
- MPPSET 36 k (36k5 unobtainable) → V_MPP 17.8 V; with D4's drop the panel sits ≈ 18.2 V ≈ Vmp.
- Walter has no DTR/RTS auto-reset and native USB only on its own connector → J1 recovery header
  (VIN/TX0/RX0/nRST/IO0/GND) for esptool via dongle.
- ESP32-S3 power-up glitch (~60 µs low on GPIO1–14, 17, 18) passes straight through to nRESET →
  **a Walter power-cycle reboots the RAK in the field. Not a bug; GPIO38–42 are glitch-free if a
  future rev moves nRESET.**
- Two RAK resets < 0.5 s apart = UF2 bootloader (double-tap). Space Walter resets ≥ 1 s.
- nRF52840 rev 3 (`INFO.VARIANT` = AAF0) may ship with **APPROTECT enabled** → Walter SWD sees the
  DP but AHB-AP refuses; only CTRL-AP ERASEALL opens it, after which bootloader+SoftDevice must be
  reflashed and `UICR.APPROTECT = 0x5A` written. Module-batch property — read `INFO.VARIANT` first.
- Never chip-erase the RAK in service (kills bootloader, SoftDevice, MeshCore identity/config in
  InternalFS, and UICR → REGOUT0 drops to 1.8 V on a bare core). Application-range erase only.
- Dual-RAK adapter (`DUAL-RAK4630-FEASIBILITY.md`): feasible with zero adapter firmware —
  TS5A23157 analog muxes + 2-bit shift register clocked by "SWDCLK while nRESET low"; segmented I2C
  (second RAK gets a private bus; INA3221 visible to RAK-A only); same-channel omni + yagi bridge is
  the planned use; ERP on the yagi chain needs asymmetric TX power. Deferred: layout density study.
- Remote admin over the mesh at 2–3 hops is unreliable in practice (observed 2026-08) — one
  motivation for LTE management.

## 7. Open items

**Decide before ordering:**
1. Walter-or-cheaper-MCU question (2026-09) — affects whether the Walter adapter is ordered as-is.
2. Stock watch list (§3) at order time; regenerate `connector-board-a` exports if the J3 flag fix
   (08-20 preflight A1) post-dates the 08-21 export.
3. Meshnology pack's protection-board charge-current limit vs. the 2 A charger.

**First-article inspection:**
4. Rotations: RAK U5 (RV-3028, 180°), RAK U3 (USBLC6), conn-B U1 (BQ24650), conn-A U4 (TPS63001,
   no offset field), **conn-A U5 BME280 (LGA-8, no offset field)**, all diodes (B: D1–D5; RAK: D1/D2).
5. USB-C shell tab seating on the 1.6 mm RAK adapter.
6. Hand-fit parts to source: TE 8-215570-0 headers, L717DFE09PT PCB D-subs, FCE17 bulkheads,
   Walter socket (Würth 61301411821-class), platform headers, conn-A J3, conn-B J2/J6/J7/SW1,
   Walter C1/C2.
7. Harness pin-1 discipline when fanning the ribbon onto the D-sub cups.

**Firmware-dependent:**
8. **No Walter firmware.** SWD-over-D-sub, UART link, kill sequencing and LM66100 behaviour cannot
   be validated without at least a bring-up sketch (`BENCH-BREADBOARD.md` §7 gives the order).
9. Custom MeshCore build must carry per-channel INA3221 shunts (stock MeshCore applies one
   `TELEM_INA3221_SHUNT_VALUE` = 0.1 Ω to all channels — wrong for ch2/ch3 here).
10. RV-3028 TCE=1 / BSM=LSM must be written once (EEPROM) or the backup caps do nothing.
11. Heltec V3 MeshCore RS232-bridge builds default to GPIO5/6 — override to 44/43 if the V3 adapter
    is ever used.

**Predicted but unproven:**
12. I2C through the bulkhead: rise time with 1 k + ~1.2 nF at 100 kHz ("workable, drop clock if flaky").
13. SWD bit-bang ceiling through 470 R + 2.2 nF (predicted ≤ ~300 kHz; SWDIO turnaround is the tight spot).
14. Walter power-up glitch resetting the RAK (expected in production).
15. Kill-line default-ON with Walter unpowered; backfeed through 470 R when one side is dead.
16. B-board thermals on the 2 A charge spine (widened to 40 mil → predicted ~+7 °C).
17. LM66100 drop (20–90 mV) and no back-charge with USB plugged into a live Walter.
18. Noise floor inside the shielded assembly with charger switching and LTE bursts active vs. quiet
    (NoiseScope, through the real cavity filter) — the point of goals 2/3.
19. RTC hold-up through a kill cycle; "dead-battery dawn" recovery (precharge 200 mA vs. a booting
    Walter); ch1 common mode at cold-dawn Voc (~25 V vs. INA3221 26 V abs-max).

## 8. Software map

| Item | Path | State |
|---|---|---|
| NoiseScope (RAK4630 SX1262 scanner, RX-only, NDJSON over Serial1@115200, UF2/SWD flashable, 8 s WDT, reports INA3221 + RTC) | `software/firmware/noisescope-rak4630` | v0.1.0, PlatformIO, pinned to MeshCore's Adafruit core fork + RadioLib commit. Per-channel shunts since 2026-10-10. |
| software-goals.md | `software/` | Must-haves: A/B OTA with health-gated rollback, autonomous watchdog/power-cycle, power telemetry + alerts, remote console tunnel to MeshCore CLI, continuous RF health + NoiseScope on demand. Should: boot-loop detection, store-and-forward, LTE self-monitoring, enclosure environment. |
| Repeater firmware (custom, MeshCore-based) | — | Planned, not started. |
| Walter firmware | — | Does not exist. |
| FleetManager / NightCrawler | removed 2026-10-10 | Deprecated → MeshMaster (separate repo). |

LTE providers tested: Lebara (cheapest plan works), NexCon (25 MB/month ≈ 1 €/month, good portal,
possibly business-only).

## 9. Validation plan

### A. Before ordering (design validation — now)

1. **Desk review of behaviour, not nets** (the preflights covered nets): charger/load interaction
   (termination, recharge cycling, precharge at dawn), Walter VIN drop budget at an empty pack, ch1
   common mode in winter, RTC backup, kill/backfeed paths. `CURRENT-SENSE.md` is the first of
   these; the same treatment for the A-board rail + kill circuit and the Walter power path is pending.
2. **Bench the two signal-integrity bets with real parts before paying for PCBs:** two
   FCE17-E09SM-250 bulkheads back-to-back (or one plus 1 nF caps) on the breadboard rig — I2C at
   100 kHz with 1 k pull-ups and 2.2 nF, scope the rise time; SWD bit-banged from the Walter through
   470 R + 2.2 nF, find the real clock ceiling; UART at 115200. `BENCH-BREADBOARD.md` specifies the
   rig; this needs the first Walter bring-up sketch (GPIO control of nRESET/SWD/KILL + UART passthrough).
3. Resolve the Walter-vs-cheaper-MCU question — if the Walter goes, the Walter adapter and the LTE
   D-sub map change before the order.
4. Re-run the 08-14/08-20 pre-order procedures (fresh exports, DRC, JLC preview rotations, live stock).

### B. First article (after boards arrive)

0. Incoming inspection, unpowered — rotation/polarity items (§7.4–5); ohmmeter: every rail to GND
   (3V3_LORA, VBAT/VCC/VLOAD+, WALTER_VIN), D-sub SH→GND, platform J1 fan-out 1,6,2,7,3,8,4,9,5,
   row A‖C / B‖D paralleling, adapter-to-platform grid fit.
1. Connector board B alone — bench PSU as panel (CC-limited, 18–21 V) + real LiPo or simulator +
   NTC: MCP1700 3.3 V; MPPT point ≈ 17.8 V; CC 2.0 A → CV 4.200 V → terminate ≈ 200 mA → DONE;
   CHG/DONE LEDs; NTC unplug stops charge; master switch kills all; reverse panel survives; spine
   temperature at 2 A; INA3221 offsets, sign and calibration on all three channels (needs an I2C
   master with pull-ups — B has none). Full list in `CURRENT-SENSE.md` §6.
2. Connector board A alone — VBAT_RAW 2.8–4.2 V via J2; 3V3_LORA on D-sub pin 2 at 0–500 mA,
   ripple; kill floating → ON, HIGH → OFF; JP1 forced-PWM; BME280 at 0x76 with temporary pull-ups.
3. Platform + harness — buzz every conductor through the bulkhead; 1000 pF per line to shell.
4. RAK adapter — USB-C enumerates, UF2 on double-tap, flash NoiseScope; `info` shows RTC=1; read
   `INFO.VARIANT`; set TCE/BSM; LEDs; Serial1 via J4/J5; idle/RX/TX current; RTC hold-up through a kill.
5. Walter adapter — LM66100 drop; USB plugged into a live system → zero current into battery;
   reset switch; J1 header flash via dongle; socket orientation vs. silk.
6. Integration — UART both ways at 115200; nRESET pulse → one RAK reboot; SWD: DP IDCODE
   0x2BA01477 → FICR → flash → verify; I2C scope at the RAK end with INA3221 + BME280 + RTC
   answering; kill cycle with tristate rule; Walter power-cycle → observe RAK reset.
7. System/RF — assemble in the RF boxes with cavity filter; NoiseScope sweep/hist/dwell with
   charger idle vs. charging vs. LTE active; compare against the Heltec prototype's −115/−100 dBm
   pattern; multi-day solar log.

## 10. Mechanical / BOM (external)

Antenna Vinnant CC868/8-PEL 10 dBi (Høeg Teknik) · LTE antenna SR Passives 5 dBi N · LMR240 N-N
80 cm · 1" galvanised pipe 2 m + 60 mm clamps · diecast alu IP67 enclosure · Cellevia 10 W panel ·
M16 vent valve · 2× Gainta G103-IP67 RF boxes · Sysmocom CF866.5KT30 cavity filter · Meshnology
10 Ah LiPo · pigtails U.FL→SMA, N-f→SMA, SMA→SMA · Amphenol FCE17-E09SM-250 ×2, L717DFE09PT ×2 ·
Panasonic 16SEPG470M, AISHI 100 µF ×2, 10 nF ×2. Brackets (DXF/STEP/PDF, 2026-08-15) in
`hardware/brackets/`: enclosure bracket (Z-profile 41.2×37.2, two 90° bends) and solar panel
holder parts A/B (A: 338.9 mm, 70°+90°+90° bends). Total ≈ 3,250 DKK + PCBs + consumables.
