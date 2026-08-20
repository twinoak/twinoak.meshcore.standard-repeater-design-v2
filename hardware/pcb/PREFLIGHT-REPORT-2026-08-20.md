# TwinOak Standard Repeater v2 — JLCPCB Pre-Order Preflight (round 3)

**Date:** 2026-08-20 · **Scope:** Connector board A only (post-BME280/J_ENV re-route)
**Reference:** `hardware/pcb/INTERCONNECT.md` as the authoritative spec. Supersedes `PREFLIGHT-REPORT-2026-08-14.md` **for Conn A only** — findings for the other four boards in the 08-14 report remain in force.

---

## Verdict

**Order-ready after one two-field flag fix and a re-export.** The re-route is electrically perfect: the BME280 island is correct pin-for-pin (SDO strapped to GND → I²C **0x76**, exactly what stock MeshCore auto-detects), schematic and PCB agree **pad-for-pad across all 21 nets with zero open nets**, the long-standing **F1 clearance blocker at U4 is fixed** (board minimum is now exactly your 0.2 mm rule), and the GND zone fills are fresh — they respect every piece of new copper. One regression slipped in via the schematic sync, and the exports on disk predate everything.

| # | Finding | Severity |
|---|---------|----------|
| A1 | **J3 (J_ENV) lost `exclude_from_bom` / `exclude_from_pos_files`** — its schematic symbol says `in_bom yes` / `in_pos_files yes` (unlike JP1/J1/J2's `no`/`no`), so the F8 sync stripped the PCB excludes. With no LCSC number, Fabrication Toolkit will emit an unmatched JLC BOM line. | **Fix before export — 2 checkboxes** |
| A2 | **U5 (BME280) pin-1 orientation must be eyeballed in the JLC preview** — new part (C92489), LGA-8, no `FT Rotation Offset` field; FT relies on its rotation DB, and LGA sensors are frequent offenders. A wrong rotation puts VDD across GND. | **Resolve in JLC preview** |
| A3 | **`outputs/` gerbers are from July 20, `production/` is empty** — pre-F1-fix, pre-BME280, pre-re-route. Nothing currently on disk is uploadable. | Re-export |
| A4 | **Stock watch: L2 (C4354938 LPS4018) was 12 pcs at JLC on 08-14** — still listed and orderable today, but I could not read a live warehouse count this round. LCSC and Mouser (47 k+) are deep; BME280 C92489 sits at ~7 k at LCSC, SMT-assemblable (Standard). | Verify count at order time |

---

## Findings in detail

### A1 — J3 exclude flags stripped by the schematic sync (regression, trivial fix)
The PCB attr audit shows every hand-fit connector carrying both excludes **except J3**:

| Ref | PCB attrs | Sch `in_bom` / `in_pos_files` |
|---|---|---|
| J1 (D-sub) | through_hole + both excludes | no / no |
| J2 (ribbon) | through_hole + both excludes | no / no |
| JP1 | through_hole + both excludes | no / no |
| **J3 (J_ENV)** | **through_hole only** | **yes / yes** |

Root cause: J3's schematic symbol was created with `in_bom yes` / `in_pos_files yes`, and KiCad's Update-PCB-from-Schematic copies those flags onto the footprint — overwriting the excludes. **Fix it in the schematic** (J3 symbol properties → untick "In BOM" and "In position files"), then F8; fixing only the PCB attrs would regress on the next sync. After the fix, J3 exports exactly like JP1: bare pads, hand-solder the header when you want the ENV breakout.

### A2 — U5 rotation: check the pink pin-1 dot deliberately
U5 sits at (130.06, 77.54) rot −90 on F.Cu. The KiCad footprint is `Bosch_LGA-8_…_ClockwisePinNumbering` with the pin-1 tick at the chamfered corner; JLC's feeder orientation for C92489 is whatever their DB says, and there is no `FT Rotation Offset` field to pin it. In the JLC assembly preview, confirm their pin-1 marker lands on your extended silk tick / fab chamfer corner. While in the same session, re-eyeball U4 (TPS63001 DRC0010J — also no offset field) and confirm D4 is correctly absent (DNP).

### A3 — Stale exports
All 12 gerbers in `connector-board-a/outputs/` are dated **2026-07-20 13:45** — they predate the F1 clearance fix, the SH grounding, the BME280/J_ENV addition, and the re-route. `production/` is empty. After A1: refill zones, run ERC + DRC, export a fresh Fabrication Toolkit package, and consider deleting the July zip so it can't be uploaded by accident.

### A4 — Stock
* **C4354938** (L2, LPS4018-222MRC): the JLC part page is live with SMT assembly offered (Economic + Standard), but no count was readable in this pass — it showed **12 pcs on 08-14**. Confirm the count in the assembly BOM step; fallback: JLC Global Sourcing (LCSC has stock, Mouser holds ~47 k) or the C370436 sub from the 08-14 report.
* **C92489** (U5, BME280, genuine Bosch): ~**6,991 pcs** at LCSC, SMT-assemblable at JLC as a Standard part. One per board — fine, but it's the only humidity-capable part in the design, so don't sit on it.
* All other Conn-A parts (C14663, C15849, C15850, C45783, C149504, C17710, C17414, C28060, C8545) are JLC basics/high-runners; nothing else changed since 08-14.

---

## Medium / low — worth a thought while the files are open

1. **Conformal coating will blind the BME280.** If these boards get sprayed for outdoor duty, mask U5 — the metal-lid vent hole must stay open, and the enclosure needs some air exchange for RH/pressure readings to mean anything. (Sensor self-heating is a non-issue: MeshCore polls in forced mode.)
2. **Bench-test caveat carried over from 08-14:** the I²C pull-ups for this bus live on the RAK adapter (1 k pair). A-board + BME280 alone on the bench has no pull-ups → a bare-board I²C scan will fail. In-system is fine. (RAK-board scope; unchanged.)
3. **I²C stubs to U5** run 0.254 mm, mostly B.Cu, one via per line — electrically fine at 100 kHz. Nothing to do; noted for completeness.

---

## Verified clean — what I checked and found correct

**BME280 island, pin-for-pin (schematic *and* copper):** SDO (pad 5) → GND → address **0x76** ✓ · CSB (pad 2) → 3V3_LORA → I²C mode latched ✓ · VDD (8) + VDDIO (6) → 3V3_LORA ✓ · GND (1, 7) → GND ✓ · SDI (3) → I2C_SDA, SCK (4) → I2C_SCL ✓ · C22/C23 100 nF (C14663) across 3V3_LORA/GND at **2.8 mm and 3.3 mm** from U5 — proper decoupling distance ✓ · J_ENV pinout 1=3V3_LORA, 2=GND, 3=SDA, 4=SCL, pin-1 rect pad, 1.0 mm drills ✓ · stock `RAK_4631_repeater` ENV manager auto-detects BME280@0x76 — no firmware config needed ✓.

**Schematic ↔ PCB sync:** geometric netlist rebuilt from the schematic (all 106 pins attach; zero floating, zero NC conflicts) and compared pad-for-pad against the copper netlist — **100 % agreement, 21/21 nets, 86 connected pads**.

**Routing completeness:** every net is a single connected copper island (pads + 200 tracks + 49 vias + zone fills) — **zero open nets**, including all new BME280/J_ENV copper.

**Clearance sweep (F.Cu + B.Cu, tracks/pads/vias/zone fills, different nets):** **nothing below 0.2 mm anywhere** — the minimum is exactly 0.2000 mm (your rule), comfortably above JLC's 0.127 mm. **The 08-14 F1 blocker (0.111 mm VBAT_RAW at U4) is confirmed fixed.** The GND fills respecting every new track at ≥ 0.2 mm also proves the fills in the file are post-re-route.

**Board level:** outline closed (27.4 × 39.1 mm rect) ✓ · all new parts and courtyards fully inside; no courtyard overlaps anywhere (U5↔C22 gap 0.53 mm, U5↔C23 0.47 mm, U5↔J3 0.56 mm — tight but legal) ✓ · J3 pin-1 hole edge 1.79 mm from the board edge ✓ · min copper-to-edge 0.508 mm ✓ · vias uniform 0.6/0.3 ✓ · min THT drill 0.8 ✓ · track widths only 0.254 / 0.508 ✓ · all SMD front-side, single-side assembly preserved ✓.

**BOM:** 12 unique LCSC numbers across 21 machine-placed parts, every one populated — U5 carries **C92489**, C22/C23 reuse **C14663** ✓ · hand-fit J1/J2/JP1 and DNP'd D4 correctly excluded in both schematic and PCB (J3 is the lone exception → A1) ✓.

**Resolved from 08-14:** the open shield question is closed — **J1's SH pad is now tied to GND** in both schematic and copper ✓.

---

## Pre-order procedure (Conn A)

1. Schematic: J3 → untick In-BOM + In-position-files → save.
2. F8 (Update PCB from Schematic) → confirm only J3's attrs change.
3. Refill zones → ERC + DRC (expect zero; silk-over-pad on the new tight cluster gets its official check here).
4. Fabrication Toolkit export → verify the BOM has 12 LCSC lines and **no** J3 line, POS has no J3 row.
5. JLC preview: U5 pin-1 dot vs silk tick (A2), U4, D4 absent.
6. Assembly BOM step: read the live counts for **C4354938** and **C92489** before paying.
