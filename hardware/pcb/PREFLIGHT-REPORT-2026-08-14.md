# TwinOak Standard Repeater v2 — JLCPCB Pre-Order Preflight (round 2)

**Date:** 2026-08-14 · **Scope:** Walter adapter · RAK4630 adapter · Connector board A · Connector board B · Platform board
**Reference:** `hardware/pcb/INTERCONNECT.md` (2026-08-14) as the authoritative spec. Supersedes `PREFLIGHT-REPORT-2026-08-11.md`.

---

## Verdict

**Order-ready after one copper fix, one BOM edit, and one rotation-offset resolution.** Everything that changed since the 2026-08-11 preflight was re-verified from scratch and came back correct: the **RV-3028 second reroute is electrically perfect**, the **platform J1 interleave remap is machine-verified end-to-end across all five boards**, the STAT-LED flip and the D4/D5 panel protection are in and correctly oriented, and every schematic now matches its PCB pad-for-pad with **zero mismatches and zero open (unrouted) nets anywhere**.

| # | Board | Finding | Severity |
|---|-------|---------|----------|
| F1 | Conn A | **VBAT_RAW track 0.111 mm from U4 (TPS63001) L1 node** — under JLC's 0.127 mm fab minimum. Unchanged since last preflight. | **Blocker — must fix** |
| F2 | Walter | **LM66100DCKR (C2869734) has 9 pcs JLC stock** — effectively unorderable | **Blocker — 1 field edit** |
| F3 | RAK | **U5 rotation offset: files say 180°, INTERCONNECT.md says it was zeroed** — one of them is wrong, and a wrong value = dead RTC. Plus U3 offset −90 (sch) vs +90 (pcb). | **Resolve in JLC preview** |
| F4 | All | **Every file in `outputs/` is from July 20** — pre-preflight-fixes, pre-rewire. `production/` is empty. | Re-export everything |
| F5 | RAK / Conn A | Stock watch: RAK4630 **47 pcs**, LPS4018 L2 **12 pcs**, SI4840DY 152, CAX toggle absent from catalog mirror | Order soon / global-source |

Details below, then the verified-clean list, then the updated pre-order procedure.

---

## Critical / high findings

### F1 — Conn A: 0.111 mm clearance at the TPS63001 — BLOCKER, confirmed twice (measured both preflights)
The VBAT_RAW 0.508 mm track ending at **(131.08, 99.438)** passes the U4 L1-node tracks and **U4 pad 4** (at 131.58, 99.438) with **0.111 mm** track-to-track and **0.126 mm** pad-to-track clearance on F.Cu. JLCPCB's 2-layer minimum is 0.127 mm — this **can fail fabrication**, and it violates your own 0.2 mm rule, so KiCad DRC will flag it too. Fix: pull that VBAT_RAW ending ~0.15 mm away from U4 pad 4 (it's the same two routes flagged on 2026-08-11 — the fix never landed).

### F2 — Walter: LM66100DCKR down to 9 pcs at JLC — BLOCKER (1 field edit)
U3's **C2869734** (LM66100DCKR) shows **9 pcs** in the JLC assembly catalog — below any sane MOQ headroom. Same die, drop-in alternatives, both in stock:

| Option | LCSC | Stock (2026-08-14) | Note |
|---|---|---|---|
| **LM66100DCKT** (cut tape) | **C2832141** | 182 | Same part, same SC-70-6 — recommended |
| LM66100QDCKRQ1 (automotive) | C5219211 | 548 | Same footprint, AEC-Q100 |

### F3 — RAK: rotation-offset state is internally inconsistent — resolve before ordering
Fabrication Toolkit reads the footprint field **`FT Rotation Offset`** (verified against FT's docs; `JLCPCB Rotation Offset` is *not* a recognized name). Current live values in the PCBs: RAK **U5 = 180**, RAK **U3 = 90**, Conn B **U1 = −90**. Three problems:

1. **U5 (RV-3028):** INTERCONNECT.md's 2026-08-13 correction says the offset was **zeroed** after the footprint numbering was restored — but the file still carries **180**. One of the two is stale. A wrong 180° on the RTC swaps VDD/VSS diagonally → dead/damaged part on every board. Only the JLC preview (their pink pin-1 dot on your silk dot) settles which is right — **check it deliberately, don't assume**.
2. **U3 (USBLC6):** schematic says **−90**, PCB says **+90** — 180° apart on a SOT-23-6 = supply pins mirrored. The PCB value is the live one; verify it in the preview and fix the schematic field.
3. Hygiene: the schematic still carries the dead-name `JLCPCB Rotation Offset` fields on U3/U5. Rename them to `FT Rotation Offset` with the settled values (or delete them and keep only the PCB fields), so a future field re-sync can't reintroduce the conflict.

Also eyeball in the same preview session: Conn B U1 (BQ24650, −90), Conn A U4 (TPS63001 DRC0010J, no offset field — relies on FT's rotation DB), the USB-C receptacle, the RAK4630 module, and diode polarity on D4/D5/D1/D2/D3 (Conn B) and D1/D2 (RAK).

### F4 — Stale exports: do not upload anything currently on disk
All gerbers in every `outputs/` folder date from **July 20** (pre-preflight fixes, pre-J1-remap), including `platform-board/outputs/platform-board.zip` — uploading that zip would order a **pre-remap platform board** that needs the old harness. `production/` folders are empty. After F1: refill zones, re-run DRC, export fresh Fabrication Toolkit packages on all boards, and consider deleting the stale `outputs/` zips so they can't be uploaded by accident. (Good news: the platform zone fills in the *file* are **fresh** — I verified they clear all foreign nets and complete the GND net, so the INTERCONNECT warning about stripped fills has already been actioned. The stale part is only the exported outputs.)

### F5 — Stock watch (JLC assembly catalog via jlcsearch mirror, 2026-08-14)

| Part | Ref | Stock | Action |
|---|---|---|---|
| C32315253 RAK4630-8-SM-I | rak U4 | **47** (unchanged since 08-11) | 1/board, no substitute — order soon |
| C4354938 LPS4018-222MRC 2.2 µH | conn-a L2 | **12** (unchanged) | JLC Global Sourcing (LCSC has stock) or sub C370436 |
| C558269 SI4840DY (VBsemi) | conn-b Q1/Q2 | 152 | 2/board — fine for a small run, don't sit on it |
| C55159901 CAX toggle | conn-b SW1 | **absent from mirror** | Known: zero JLC warehouse stock — Global Sourcing or hand-solder |
| C3304278 RV-3028-C7 | rak U5 | 429 | OK |
| C2293 LED blue 0805 | rak D2 | 19 k | OK |

All **48 unique LCSC numbers** across the four assembled boards were re-checked today; descriptions/packages match the schematic values in every case. Everything else is deep in stock (thousands to millions).

---

## Medium / low — worth a look while the files are open

1. **RAK: I²C pull-ups still on the D-row** (R1/R2 on U2 pins 17/18; the bus runs on 7/8). In-system fine (platform parallels the rows); bench bring-up over USB with no platform under it has no pull-ups → RTC scan fails. Same nag as last time; move to pins 7/8 if you ever expect to bench-test bare.
2. **RAK: GND via 0.19 mm from the USB-C shell slot** — via at (139.332, 83.058) sits 0.19 mm hole-edge-to-hole-edge from J3's S1 oval slot, under JLC's 0.254 mm hole-to-hole guideline. Same net (GND), so worst case is a DFM remark / drill breakout; nudging the via ~0.1 mm away is free.
3. **Walter: the 2×14 socket still has no orientation hint on silk** (only the A–D row letters on the platform interface). One "USB-C ➜ this end" text prevents a 180° module insertion in the field.
4. **Platform: the J1 fan-out legend documented in INTERCONNECT.md is not on the silk.** The board carries only "Platform baseboard v1.1". Either add the `1,6,2,7,3,8,4,9,5` legend text or amend the doc — as-is, the harness builder needs the doc open.
5. **D-sub shield pads (SH) still float on A and B** — the deliberate-or-not decision from last preflight remains open. Make it conscious.
6. **USB-C vs 1.6 mm board** (repeat): the XUNPU TYPEC-303-ACP16 drawing assumes 0.8 ± 0.1 mm PCB for the through-shell tabs. Confirm the C720628 variant against 1.6 mm before assembly, or order the RAK adapter at 0.8–1.0 mm thickness.
7. **EXCLUDE DNP option differs per board** (rak/conn-a: true; walter/conn-b: false). Now moot — I verified **every** DNP/hand-fit part on all four assembled boards carries both `exclude_from_bom` and `exclude_from_pos_files` in the PCB, so exports come out clean either way — but ticking it everywhere costs nothing and removes the trap.
8. **Conn B: charge-spine widths unchanged** (0.508 mm / 20 mil bottleneck, ~+20 °C at 2.0 A peak-sun). Concrete fix: widen **20 mil → 40 mil (1.0 mm)** on the 2 A battery-charge spine — that takes the rise from ~+20 °C to **~+7 °C** (30 mil ≈ +10 °C, 50 mil ≈ +4.5 °C, IPC-2221 1 oz external). The runs that carry the full 2 A (KiCad coords, F.Cu unless noted):
   * SRP node `Net-(U1-SRP)`: L1.2 (146.8, 57.0) → C10.1 → RS1.1 (143.8, 53.3), ≈ 8 mm (the C8/C9 branch at x ≈ 151.6 carries ripple — widen if easy)
   * `VCC` spine link RS1.2 → RS3.1: (143.8, 50.4) → (143.8, 48.5)
   * `VBAT`: RS3.2 (143.8, 45.5) → F1.1 (142.5, 42.2), ≈ 4.5 mm
   * `BAT+`: F1.2 (146.8, 42.2) → SW1.5 (149.8, 38.2), ≈ 6.5 mm
   * `BAT_RAW` (B.Cu): SW1.4 (149.8, 42.9) → J7.2 (143.8, 34.3), ≈ 12 mm
   * PH node `Net-(U1-PH)`: Q1 source (145.3, 73.4) → Q2 drains (y = 69.5) → L1.1 (146.8, 66.0), ≈ 8.5 mm — same 2 A, and shorter/wider is also better for EMI

   The panel-side ~1.2 A chain sits at ~+6 °C and is nice-to-have at 30–40 mil: `Net-(D4-A)` J2.1 → D4/D5 (≈ 40 mm, mostly B.Cu at x ≈ 166.3), `PANEL_RAW` D4.1 → SW1.1 (≈ 21 mm), `PANEL+` SW1.2 → RS2.1 (≈ 17 mm B.Cu), `VIN` RS2.2 → C1/C2 (≈ 8 mm) plus the 14.6 mm feed at x = 142.7 down to the Q1 drains. While in there, add a **second via in parallel** at the three spots where panel current crosses layers through a single 0.6/0.3 via: (167.1, 58.7), (149.9, 47.2), (149.9, 50.2). Do **not** widen the 0.254 mm kelvin/sense taps — U1 pins 9/10 (SRN/SRP), the four long INA3221 taps at x ≈ 141.9–143.3, the U2.1 run at y = 93.1, and the R3 VFB tap are all correct thin (widening them adds no capacity and hurts sense accuracy). The 2 A spine itself never crosses a via — all F.Cu/THT — so tracks are the whole story.
9. **Conn B: D5 drawn with a bidirectional-TVS symbol** but SMBJ26A is unidirectional. Copper is right (cathode band to panel+, forward-conducts on reversed panel — which even complements D4). Cosmetic only.
10. **RAK: J3 S1 slot annular 0.125 mm** and **Conn B: SW1 mounting-post pad at 0.024 mm from board edge / zero-annular oval** — both are the connector-manufacturer land patterns, both known; expect at most DFM remarks.

---

## Verified clean — what I checked and found correct

**Schematic ↔ PCB sync:** independent geometric netlists rebuilt for all five schematics and compared pad-for-pad against the copper netlists in the PCBs — **100 % agreement, all five boards** (conn-a's stale name sync from last time is gone).

**Routing completeness:** every net on every board is a single connected copper island (pads + tracks + vias + zone fills) — **zero open nets**. This specifically confirms: **U5's second reroute is correct** (SCL→pad 3, SDA→4, VSS→5, VBACKUP→6, VDD→7, EVI→8 on the restored numbering, matching the shared lib exactly), the **platform rewire is fully routed**, and Conn B's **D4/D5 are placed and routed**.

**Platform J1 remap, machine-verified end-to-end:** RAK pin → adapter interface → platform row → J1 conductor → (1,6,2,7,3,8,4,9,5) solder-cup fan → D-sub → A-board → ribbon → B-board → LTE D-sub → platform → Walter pin, for every signal:

| Signal | LoRa D-sub | Ribbon wire | LTE D-sub | Walter pin | Result |
|---|---|---|---|---|---|
| nRESET | 3 | 3 | 3 | IO1 | **PASS** |
| SWDCLK | 4 | 4 | 4 | IO2 | **PASS** |
| SWDIO | 5 | 5 | 5 | IO4 | **PASS** |
| I2C_SCL | 6 | **10** | — (INA3221) | — | **PASS** |
| I2C_SDA | 7 | 7 | — (INA3221 + A0=0x42) | — | **PASS** |
| UART RX←TX | 8 | 8 | 8 | IO7 | **PASS** |
| UART TX→RX | 9 | 9 | 9 | IO8 | **PASS** |
| KILL | — | 6 | 6 | IO5 | **PASS** |
| 3V3_LORA / VLOAD+ | 2 | 2 (VBAT_RAW) | 2 | VIN via LM66100 | **PASS** |
| GND / spare | 1 / — | 1 | 1 / 7 NC'd | GND / IO6 | **PASS** |

J1 pad 10 has no net (unused conductor) ✓; odd conductors = A/C rows, even = B/D rows exactly as documented ✓; silk bumped to v1.1 ✓.

**Fixes from the 08-11 preflight, confirmed landed:** STAT LEDs D2/D3 now anode→R7/R8→VREF, cathode→STAT1/2 (open-drain sink — correct) ✓ · D4 SS54 anode→J2.1, cathode→PANEL_RAW ✓ · D5 SMBJ26A cathode→panel input, anode→GND ✓ · R13–R15 = C149504 ✓ · L1 = C845066 ✓ · Walter U1/U2 now DNP + excluded ✓ · Walter C1/C2/J1, RAK J4/J5, A-board JP1/J1/J2, B-board J1/J5 all carry exclude-BOM **and** exclude-POS attributes in the PCBs ✓ (the 08-13 cleanup is complete — no unmatched JLC lines regardless of options).

**Electrical re-checks (this round):** kill chain J2.6→R12→Q3 (R13 pulldown, R14 pullup to VBAT_RAW) → TPS63001 EN, default-ON when floating ✓ · TPS63001 pin-complete, FB→VOUT (fixed version), PS/SYNC low via R15 with JP1 forced-PWM option ✓ · BQ24650 network recomputed: MPPT divider 499k/36k, 4.2 V float (499k/499k), TS window (5k23/30k1 vs VREF), TERM_EN=enabled, bootstrap D1 A→REGN/K→BTST, SRP/SRN across RS1 with C10 differential cap ✓ · INA3221 ch1=RS2(panel), ch2=RS3(battery, bidirectional), ch3=RS4(load), A0→SDA=0x42, powered from the always-on MCP1700 ✓ · LM66100: VCC→VIN, VOUT→WALTER_VIN, CE→GND ✓ · USB path J3→USBLC6 flow-through→nRF USB, D+→D+, CC 5.1 k, shield→GND ✓ · RV-3028: VDD/VSS/backup bank/EVI pull-up ✓ · UART jumpers J4/J5 1-2 = Serial1 (P0.15/16) ✓ · rail chain PANEL_IN→D4→PANEL_RAW→SW1→PANEL+→RS2→VIN and BAT_RAW→SW1→BAT+→F1→VBAT→RS3→VCC→RS4→VLOAD+ ✓ (ribbon feed deliberately unfused, per spec).

**NC / DNP audit:** zero unconnected pins without NC flags, zero NC-but-connected conflicts, on all five boards (61 explicit NCs). B-board J1.7 spare NC'd ✓.

**Board-level:** outlines closed on all five ✓ · min drill 0.3 mm everywhere ✓ · via 0.6/0.3 ✓ · zone fills present and fresh on all boards (they clear all foreign nets — platform's post-rewire refill is in the file) ✓ · different-net clearance sweep: **nothing below 0.127 mm anywhere except F1**; the only sub-0.2 items are the known USB differential pair on the RAK (0.146–0.196 mm, deliberate) and a handful of 0.17–0.19 via gaps in the same USB area — DRC nags, fab-safe ✓ · all SMD front-side, single-side assembly ✓.

**BOM:** every machine-placed part on all four assembled boards carries `LCSC Part #` ✓; platform is all hand-solder THT (order bare, **2 per repeater**) ✓; 48/48 part numbers live and matching their schematic values ✓ (stock exceptions in F2/F5).

---

## Pre-order procedure (do in this order)

1. Conn A: nudge the VBAT_RAW ending at (131.08, 99.438) ≥ 0.2 mm off the U4 L1 node (F1).
2. Walter schematic: U3 `LCSC Part #` → **C2832141** (F2). Update PCB from schematic.
3. Field hygiene: rename the schematic `JLCPCB Rotation Offset` fields on RAK U3/U5 to `FT Rotation Offset` and settle their values; decide U5 = 0 or 180 **only** via the JLC preview (F3).
4. Decide/confirm: L2 sourcing (Global Sourcing vs C370436), CAX toggle (Global Sourcing vs hand-solder), SH-to-GND, USB-C vs 1.6 mm board.
5. Every touched board: *Update PCB from Schematic* → refill zones → **run real ERC + DRC in KiCad 10 locally** (this sandbox still can't run v10 — my geometric sweep is not a substitute; expect it to also show the known USB-pair nags).
6. Fabrication Toolkit export per board (EXCLUDE DNP ticked for tidiness) → fresh `production/` zips only — treat everything in `outputs/` as stale (F4).
7. In the JLC order tool: confirm rotations against the pink-dot preview (U5, U3, conn-b U1, conn-a U4, USB-C, RAK module, all diodes), confirm live stock for the F5 list, expect unmatched lines only if a hand-solder part slipped through (none should).
8. Order: platform ×2 per repeater (bare), other boards with front-side SMD assembly; conn-b THT set via JLC THT or hand.

## Open items only you can close

U5 rotation offset truth (JLC preview) · USB-C vs 1.6 mm (or thinner RAK board) · SH-to-GND decision · CAX toggle sourcing · RAK module stock timing (47 pcs) · harness pin-1 discipline at build time · RTC TCE=1 / BSM=LSM firmware config (unchanged from last report).

---

*Method: independent geometric netlister (transform-validated, 100 % pad-net agreement with all five PCBs) + full-copper connectivity (pads/tracks/arcs/vias/zone fills) + different-net clearance sweep + hole/annular/edge audits, all rebuilt from the current working-tree files (which match git HEAD apart from line-ending churn — no uncommitted design changes). Cross-board walk executed programmatically against INTERCONNECT.md's 2026-08-14 map. Stock: jlcsearch mirror of the JLC assembly catalog, 2026-08-14 — re-verify the F5 watch-list at upload time. FT field-name behaviour verified against the Fabrication Toolkit documentation.*


---

## Addendum — 2026-08-14 evening (post-review fixes, verified)

* **F1 FIXED and re-verified:** the conn-A reroute landed — minimum different-net spacing on the board is now exactly 0.2 mm (your rule limit, passes), zero open nets, schematic↔PCB sync intact.
* **F2 applied:** Walter U3 → **C2832141** (LM66100DCKT, 182 pcs), schematic + PCB fields both updated.
* **F3 resolved:** PCB values confirmed correct per TH's JLC preview check. Fields renamed to `FT Rotation Offset` everywhere; RAK sch U3 corrected −90 → **+90** to match the verified PCB value; conn-B U1's −90° field added to the schematic so a field re-sync can't drop it. INTERCONNECT.md corrected (the "zeroed" claim was the stale statement).
* **Walter silk hint added:** "USB-C / THIS END" on F.Silk at (148.6, 67.8), inside the module outline at the pin-1/VIN end — visible while inserting, clear of all pads and footprint silk.
* **EXCLUDE DNP** ticked in walter + conn-B Fabrication Toolkit options (now true on all four assembled boards). No DNP/exclude attributes were missing — verified again.
* **USB-C vs 1.6 mm (medium #6):** no *verified* drop-in found. Same-series XUNPU height variants exist — TYPEC-303-ACP16H056 (C53278835), H065-B/-O (C53278836/37), H068-B/-O (C53278838/39), 180–600 pcs each — one of these may be the taller-stack/longer-tab version, but the drawings are image-only and need a 2-minute human check on LCSC. Fallbacks: order the RAK adapter at 0.8–1.0 mm (JLC standard; board is small and carried by the 20-pin interface), or keep 1.6 mm and accept top-side-soldered shell tabs (reduced retention; maintenance-port duty only).
