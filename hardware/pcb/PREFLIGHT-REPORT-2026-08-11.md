# TwinOak Standard Repeater v2 — JLCPCB Pre-Order Preflight

**Date:** 2026-08-11 · **Scope:** Walter adapter · RAK4630 adapter · Connector board A · Connector board B · Platform board
**Reference:** `hardware/pcb/INTERCONNECT.md` (2026-08-09) treated as the authoritative spec.

---

## Verdict

**Not ready to order yet — but very close.** The system design is sound and the cross-board wiring is verified correct end-to-end. There are **2 critical items, 1 confirmed wiring bug and 2 BOM blockers**, all cheap to fix:

| # | Board | Finding | Effort |
|---|-------|---------|--------|
| F1 | RAK adapter | **RV-3028 RTC footprint is mirrored** → dead RTC | Renumber 8 pads + reroute a small area |
| F2 | RAK adapter | **RAK4630 stamp footprint chirality unverified** — must be closed before ordering | 2-min STEP/print check (you) |
| F3 | Connector B | **Both STAT LEDs reversed** → CHG/DONE never light | Flip 2 symbols, no reroute |
| F4 | Conn A + B | **C17407 and C511353 delisted from JLC catalog** | 2 field edits (drop-in subs found) |
| F5 | Connector B | No reverse-panel protection (decision needed, not necessarily a change) | Your call |

Everything else — including all the things you asked about explicitly — checks out. Details below, worst first, then the clean list, then your 9 questions answered, then the exact pre-export procedure.

---

## Critical / high findings

### F1 — RV-3028-C7 footprint mirrored (RAK adapter U5) — CRITICAL, confirmed twice
`lib/TwinOak.pretty/RV-3028-C7.kicad_mod` pad layout, measured from the file:

```
top row  L→R:  4  3  2  1      real part (Micro Crystal DS, top view):  1  2  3  4
bottom   L→R:  5  6  7  8                                               8  7  6  5
```

The real C7 package numbers clockwise (1 top-left → 4 top-right → 5 bottom-right → 8 bottom-left); the footprint numbers counter-clockwise. That's a chirality error — **no rotation of the part can fix it** (rotating 180° gives top row 8,7,6,5 — still wrong). As drawn, the soldered part gets SDA on VDD's pad, VSS/VDD scrambled → dead RTC, possibly stressed part.

**Fix:** in the footprint editor swap pad numbers **1↔4, 2↔3, 5↔8, 6↔7** (keep silk pin-1 dot where the part's lid dot will actually sit — top-left). Then in the PCB: *Update Footprints from Library* on U5, and **reroute the U5 area** — the four signal pads swap sides, existing tracks will connect wrong nets. Re-run DRC after.

Verified independently: Micro Crystal RV-3028-C7 datasheet + application manual (3 extractions) and my own pad-coordinate dump above agree. Symbol pin *functions* (CLKOUT/INT/SCL/SDA/VSS/VBACKUP/VDD/EVI = 1-8) are correct — only the footprint is wrong.

### F2 — RAK4630 stamp footprint orientation: verify before ordering — treat as CRITICAL until closed
Everything textual about `TwinOak.pretty/RAK4630.kicad_mod` matches RAK's official pin-definition table: **all 44 pad functions, 23×15 mm body, 1.2 mm pitch, 13/9/13/9 pads per edge, RF keep-outs adjacent to RF_BT(15)/RF_LoRa(37)** — zero mismatches. What could *not* be verified remotely is the chirality (which long edge carries pins 1–13): RAK documents that only in images, and every image/PDF/CAD source was blocked or bitmap-only (docs images, SnapEDA, EasyEDA/LCEDA API, datasheet mirrors, LCSC drawing). Since F1 proves this library can contain a mirrored footprint, close this one deliberately:

* **Best:** download RAK's official STEP (`3D_RAK4630.stp`, downloads.rakwireless.com → LoRa → RAK4630 → 3D_File — the footprint already references this filename but the file is missing from the repo), assign it to the footprint, open the 3D viewer, and check pin-1/IPEX corner against the module photos on the RAK product page.
* **Or:** 1:1 print of the layout vs. a physical module.

A mirrored castellated stamp = scrap boards, so this 2-minute check is the single most valuable remaining action.

### F3 — STAT LEDs reversed on connector B (D2 CHG, D3 DONE) — HIGH, confirmed
Netlist and PCB pads both show **anode → STAT pin, cathode → R7/R8 → VREF node**. BQ24650 STAT1/2 are open-drain *sinks* (TI datasheet, typical application: anode to pull-up rail, cathode to STAT). As wired the diodes are reverse-biased whenever STAT asserts → **CHG/DONE can never light** (no damage — 3.3 V reverse is fine). The 2026-08-09 changelog in INTERCONNECT.md line ~140 says this was "fixed during review" — the fix is **not in the current files** (never landed or regressed). **Fix:** flip D2 and D3 in the schematic; the nets swap onto the opposite (symmetric) 0805 pads — no rerouting needed.

### F4 — Two BOM part numbers no longer orderable at JLCPCB — HIGH (order blocker)
Both parts have live JLC *info pages* but are **absent from the active assembly-parts catalog**, which is exactly the "not-found" case you wanted to avoid:

| Ref(s) | Current | Problem | Replacement (drop-in) |
|---|---|---|---|
| conn-a R13/R14/R15 (100k 0805) | **C17407** | LCSC marks NRND; delisted at JLC | **C149504** — same UNI-ROYAL 0805W8F1003T5E, **Basic** library, ~1.78 M in stock |
| conn-b L1 (10 µH IHLP4040) | **C511353** (M5A) | delisted at JLC (all sibling variants present) | **C845066** — IHLP4040DZER100M**11**, same land pattern, Isat 7.1 A, ~3.4 k stock (alt: C845065 M01) |

### F5 — Solar input robustness (connector B) — decision needed
* **Reverse-panel: HIGH risk, no protection.** J2 is a screw terminal — field-reversible. A reversed panel puts −18 V on VIN/PANEL+: INA3221 IN pins (abs max −0.3 V) get damaged and the battery discharges through Q1's body diode until F1 trips (~6 A). Options: series Schottky **SS54** in the panel feed (≈0.3 V of 17.8 V ≈ 2% loss; retune R2 a notch if you care), a P-FET reverse blocker, or accept-and-label. Pick one consciously.
* **Panel TVS: MEDIUM.** Cold-morning Voc ≈ 24 V vs INA3221 26 V abs max = 2 V margin, and hot-plugging long leads into low-ESR ceramics rings above Voc. An **SMBJ24A** across PANEL+/GND is cheap insurance (BQ24650 itself is rated to 33 V).

---

## Medium / low — worth doing while the files are open

1. **conn-a spacing squeeze at the TPS63001 (would fail JLC's 0.127 mm min):** VBAT_RAW track passes the U4 L1-node track/pad at ≈ **0.11–0.126 mm** measured. It's also under your own 0.2 mm rule, so KiCad DRC will show it — nudge those two routes.
2. **rak: D+/D− pair gap 0.146 mm** — manufacturable (>0.127) and fine for USB FS, but your 0.2 mm netclass rule will flag it. Accept/exclude or give the pair its own netclass.
3. **rak: I²C pull-ups on the wrong interface row.** R1/R2 hang on U2 pins 17/18 (D-row); the bus runs on pins 7/8 (B-row). In-system they join through the platform's row-paralleling, so it works — but a **bench bring-up over USB with no platform attached has no pull-ups** (RTC scan fails). Cheap: move R1/R2 to pins 7/8.
4. **rak: USB-C board-thickness check.** The XUNPU TYPEC-303-ACP16 vertical receptacle drawing (noted in your own footprint description) assumes a **0.8 ± 0.1 mm PCB** for the through-shell tabs; boards default 1.6 mm. Verify the C720628 drawing/variant vs 1.6 mm (retention/fit) before assembly.
5. **DNP export hygiene (Fabrication Toolkit):** FT always drops parts with the *exclude-from-BOM attribute*, but DNP-marked parts are only dropped when the **"Exclude DNP components" option** is ticked — and `connector-board-a/fabrication-toolkit-options.json` currently has `"EXCLUDE DNP": false`. Affected DNP parts: rak U2 (interface), conn-a D4, conn-b C13/R11 (R11 and D4 have no LCSC number → would appear as unmatched sourcing lines). **Tick Exclude-DNP on every board when exporting** (it persists per-project) or add exclude-from-BOM/POS attributes to those four.
6. **walter: U2 (platform interface) is *not* DNP** while the identical interface on the RAK adapter *is* DNP/excluded. Same hand-fitted part — mark walter U2 DNP + excluded for consistency and BOM cleanliness.
7. **walter: MPN fallback noise in FT BOM.** FT falls back to the `MPN` field when "LCSC Part #" is absent — C1 ("Panasonic 16SEPG470M…") and C2 ("SR Passives…") will land as invalid part numbers → 2 unmatched JLC lines. Harmless (they're hand-solder TME parts), or set exclude-from-BOM on the hand-fitted set (C1, C2, J1, U1, U2).
8. **conn-b: SW1 mounting-post pad ~0.02 mm from the board edge** (both layers, no net). By design for the right-angle toggle — expect a JLC DFM remark; accept it or trim the pad.
9. **conn-b: charge-spine width.** Full path L1→RS1→(VCC)→RS3→(VBAT)→F1→SW1→J7 has a verified **0.508 mm bottleneck ≈ +20 °C at the full 2.0 A** charge current (only reached in strong sun). Works, but since you're respinning anyway, widening the spine to ~1 mm where easy is free margin. The RS3→RS4 VCC link has a 0.254 mm bottleneck — load-only (~1.2 A ms-bursts), acceptable.
10. **A/B boards: D-sub shield pads (SH) float.** Decide: tie to GND (ESD path from shell into board return, better retention) or keep isolated as a deliberate single-point-chassis-bond scheme. Make it a conscious choice — it's currently just unconnected.
11. **walter LOW:** LM66100 ST pin floats (TI says ground if unused — cosmetic); the 2×14 socket is **not keyed** — add a silk hint ("USB-C ➜ this end"); C2 (THT film) sits on the back side — assembly-awareness only.
12. **conn-a LOW:** D4 value "T03LC" isn't a real SOD-323 part number (the SOD-323 series is CDSOD323-T03/T03S; "T03LC" exists only in SOT-23). DNP, so cosmetic — orientation as drawn (cathode to rail) is correct for a unidirectional part.
13. **INFO rak RTC backup:** C7+C8 ≈ 200 µF nominal → ~90–120 µF effective at 3.3 V bias → **hold-up tens of minutes to ~1.5 h** (matches your accepted 0.5–2 h; C10/C11 are 3V3-rail bulk as documented, not backup). Firmware must-do (one-time, EEPROM): **enable trickle charge (TCE=1)** and set **BSM=LSM** — Micro Crystal factory defaults have both OFF, i.e. the backup bank does nothing until configured.
14. **INFO conn-b:** NTC unplugged → charging suspends (safe, TS reads cold-fault); TS window computes to ≈ **0.5…41 °C** — TI-recommended values, good for LiPo. JST-EH battery connector 3 A vs 2 A worst case ✓. F1's 3–6 A grey zone slightly exceeds the EH rating without tripping — largely mitigated by the TS charge suspend; noted for awareness.
15. **INFO:** Platform board is all hand-solder THT (no LCSC parts) — order **bare PCBs, 2 per repeater**. And a guardrail: the **Heltec V3 adapter PCB is stale** vs its rebuilt schematic (verified: splits/merges, missing pull-ups) — fine since it's not in this order, but don't order it on impulse.

---

## Verified clean — what I checked and found correct

**Netlist / "will this work":**
* Rebuilt full netlists of all 5 boards independently and cross-validated against the copper in each `.kicad_pcb` — **exact agreement** (this also validates schematic↔PCB sync; only conn-a carries stale net *names*, zero copper differences — run *Update PCB from Schematic* once to re-sync labels).
* **End-to-end walk RAK ⇄ Walter** across adapter→platform→D-sub→A→ribbon→B→D-sub→platform→adapter, every pin against INTERCONNECT.md: **all match**, including the SCL-on-ribbon-10 fix, KILL on LTE-6, spare 7 NC'd, 470R series resistors on 3/4/5/8/9 only (I²C correctly excluded), one-pull-up-set policy (1 k on the LoRa adapter, none on B), INA3221 A0→SDA = **0x42**, RV-3028 = 0x52.
* Kill chain: Walter IO5 → R12→Q3 (R13 pulldown, R14 pullup to VBAT) → TPS63001 EN, **default ON when floating** ✓, TPS63001 has true load-disconnect in shutdown ✓.
* Master switch topology: OFF kills panel + battery + LDO + INA — everything dead, RTC rides on backup ✓. No sneak paths (walter's LM66100 blocks USB-C 5 V from back-charging the pack ✓ — that fix is in and correct).

**Datasheet cross-checks (pin-by-pin against official docs):** Walter module 28/28 (v1.6 schematic + datasheet + DPT's own footprint — identical pads, no mirror, socket numbering correct); LM66100 (CE active-low tied right, ST float ok-ish); ESP32-S3 GPIO roles (no strap/input-only conflicts — S3 straps are 0/3/45/46 only); RAK4630 44/44 pad *functions* incl. RF pads NC'd for the -I u.FL variant ✓; nRF USB (VBUS direct + USBLC6 flow-through correct, D+↔D+ ✓, CC 5.1 k UFP ✓, SBU NC ✓); RV-3028 wiring + EVI pull-up ✓; LED pins = RAK4631 variant.h P1.03/P1.04 active-high ✓ (MeshCore will blink them); UART jumpers: **1-2 = Serial1 = P0.15/16 = exactly what MeshCore's Serial1 uses** ✓; TPS63001 10/10 pins, FB-to-VOUT correct for the fixed version, 2.2 µH per TI's own table, caps ≥ minimums ✓; BQ24650 16/16 pins, **17.83 V MPPT / 4.200 V float / 2.0 A charge** all recomputed from TI formulas ✓, TERM_EN=enabled ✓, bootstrap diode direction ✓, SRP/SRN differential filter present (C10 *is* the DS-required 100 n diff cap — checked, not a miswire) ✓; INA3221 16/16 pins, shunt ranges all within ±163.8 mV, ch2 bidirectional confirmed ✓; **MCP1700 pinout is CORRECT** (SOT-23 = GND/VOUT/VIN — my own initial suspicion, refuted against two independent datasheet mirrors).
* **1210 MLCC on the CPH3225A lands:** measured the footprint — pads are actually KiCad 1210 hand-solder geometry (1.325×2.7 mm at ±1.56 mm), so C49066 fits properly for machine assembly, and a real CPH3225A still fits later ✓.

**Mechanical mating (measured from the files):** adapter U2 grid = platform J2–J5 grid **exactly** — 2.54 mm pitch, rows 40.64 mm apart in x, 35.56 mm in y, mounting holes 40.64×50.8 mm, and the hole-to-pin-1 offset is identical (+5.08, −7.62) on both sides → adapters stack component-side-up, **no mirroring**, rows A‖C / B‖D parallel as documented. Single-pin battery/3V3 feeds are within 2.54 mm header ratings.

**Trace width vs current (IPC-2221, 1 oz outer, ΔT target 10 °C):**

| Path | Bottleneck | Current | ΔT est. | Verdict |
|---|---|---|---|---|
| conn-b charge spine (L1→…→J7, 5 hops checked) | 0.508 mm | 2.0 A peak-sun | ~20 °C | OK (widen if convenient, see #9) |
| conn-b VIN switching loop (RS2→Q1, PH→L1) | 0.508 mm | ~1.2 A RMS | ~6 °C | OK |
| conn-b VLOAD+ → D-sub / ribbon | 0.508 mm | 1.2 A bursts | ~6 °C | OK |
| walter VCC→LM66100→VIN | 0.508 mm | 1–1.5 A LTE bursts | ~10 °C | OK (470 µF bulk carries bursts) |
| conn-a VBAT_RAW in / 3V3_LORA out | 0.254/0.508 mm | ≤0.4 A | ≤4 °C | OK |
| rak +3V3 | 0.508 mm | ≤0.2 A | <1 °C | OK |
| platform VBAT + GND | 1.0–1.27 mm | any realistic | <3 °C | OK |
| Ribbon wire 2 (VBAT_RAW to A-board) | 28 AWG | ~0.4 A | — | OK (Walter current does *not* cross the ribbon) |

**Board/DRC-level:** outlines closed on all 5 (adapters inherit theirs from the shared U2 footprint — exports fine, existing gerbers prove it); min track 0.254 mm, clearance rule 0.2 mm, via 0.6/0.3 mm, annular 0.15 mm, hole-to-hole 0.25 mm — **all comfortably above JLCPCB 2-layer capabilities** (0.127/0.127, via 0.3 drill); geometric sweep found no different-net spacing below 0.15 mm anywhere except finding #1; copper-to-edge clean except the intentional SW1 post (#8); all SMD on the front side everywhere → **single-side assembly** ✓.

**DNP / NC audit (your question 6):** every deliberately-unfitted part carries the DNP flag (rak U2, conn-a D4, conn-b C13+R11 — plus values marked "DNP" in text ✓); walter U2 is the one inconsistency (#6). Every unused pin on every board carries an explicit no-connect flag — **87 NCs across the five boards, zero dangling pins, zero NC-but-connected conflicts** (walter 26, rak 29 incl. QSPI/AIN/NFC spares, conn-b 7 incl. J1.7 spare + SW1 B-poles + INA alerts; conn-a and platform legitimately have none — every pin is used). Platform J1 pad-10 (unused ribbon conductor) has no net — documented cosmetic ✓.

**JLCPCB stock & part match (your questions 7+9):** all 46 unique LCSC numbers resolved; **descriptions/packages match the schematic values in every case**, with your already-documented intentional substitutions confirmed as-listed (VBsemi SI4840DY clone, 36 k MPPSET, RUILON/TECHFUSE PTC, 50 V 1210 input caps, IHLP-M5A→see F4, 1210 MLCC backup). Everything in stock **except the two F4 delistings**. Watch-list (fine, but confirm at order time / order soon):

| Part | JLC stock | LCSC stock | Note |
|---|---|---|---|
| C32315253 RAK4630-8-SM-I | **47** | 453 | 1/board, no substitute exists — use JLC Global Sourcing from LCSC if needed |
| C4354938 LPS4018-222MRC | **12** | 331 | Global-source or sub C370436 (Sunlord SPH4018, same pattern class) |
| C558269 SI4840DY (VBsemi) | 152 | 131 | 2/board — enough for a small run |
| C55159901 CAX toggle | n/a in mirror | 190 | Confirm live in the order tool (part is new-ish); wave-solderable per JLC |
| C2903502 20 mΩ shunt | 2,167 | — | fine |

Extended-library parts are most of the actives (as expected — you said cost is fine). Hand-solder set (no LCSC, correct): TE 8-215570-0 ribbon headers, DE9s, pin headers, walter socket, TME caps, platform everything.

---

## Your checklist, answered

1. **Netlist review — will it work?** Yes, after F1/F3 fixes. The architecture (kill line, one-master I²C @0x42/0x52, UART link, power tree, backfeed protection) verified end-to-end against datasheets and INTERCONNECT.md.
2. **DRC ok?** Rules are JLC-compatible; my geometric sweep found one real spacing violation (conn-a #1) + two DRC-rule nags (#2, #8). **Caveat:** KiCad 10 wouldn't install in this cloud sandbox (v10 file format, no v10 CLI available), so run the real ERC+DRC locally before export — exact commands below. Expect it to also flag the conn-a items above.
3. **Trace width vs current:** all paths verified with margin (table above); worst case is the 2 A charge spine at ~+20 °C — acceptable, optional widening suggested.
4. **Components vs datasheets:** pin-by-pin verified for every active part on all five boards; two errors found (F1 footprint, F3 LEDs), one suspicion refuted (MCP1700). RAK4630 functions verified; chirality = F2 physical check.
5. **Connectors matched pin-by-pin, ribbon included?** Yes — D-subs (deliberately non-identical, per spec), 10-wire ribbon 1:1 incl. SCL-on-10, platform pass-through rows, both adapter maps, mating grid geometry, genders (male PCB D-subs on the back sides vs female bulkheads ✓). Assembly-time items: IDC pin-1 orientation when crimping the harness; both A/B ribbon headers are same-rotation keyed parts so a straight cable is correct.
6. **DNP + NC flags:** complete except walter U2 inconsistency (#6); all unused pins NC'd (details above).
7. **JLCPCB references:** every machine-placed part has `LCSC Part #` (FT's top-priority field ✓). Two F4 replacements needed; walter C1/C2 fall back to MPN noise (#7).
8. **Fabrication Toolkit readiness:** outlines closed, fields right, options file present (A only), existing outputs prove the pipeline. Tick **Exclude DNP** everywhere (#5), re-sync conn-a names, and after export eyeball JLC's preview for QFN/USB-C/module rotations (FT's rotations.cf catches most, the preview catches the rest).
9. **Stock:** two delisted parts (F4) = the only blockers; four watch-list parts above; everything else deep in stock.
10. **What you forgot to ask:** F2 (footprint chirality verification), F5 (reverse-panel + TVS), USB-C board-thickness spec (#4), RTC trickle-charge firmware enable (#13), bench I²C pull-up gotcha (#3), and the Heltec-adapter-is-stale guardrail (#15).

---

## Fix list (do in this order)

1. `TwinOak.pretty/RV-3028-C7.kicad_mod`: renumber pads 1↔4, 2↔3, 5↔8, 6↔7 (F1).
2. RAK PCB: *Update Footprints from Library* → reroute U5 area → check silk dot.
3. conn-b schematic: flip D2 and D3 (F3).
4. conn-a schematic: R13/R14/R15 `LCSC Part #` → **C149504**; conn-b L1 → **C845066** (F4).
5. Optional-but-recommended while in there: rak R1/R2 → U2 pins 7/8 (#3); walter U2 → DNP+exclude (#6); conn-a VBAT_RAW reroute at U4 (#1); decide SH→GND (#10); decide SS54/TVS (F5); widen charge spine (#9).
6. Every board: **Update PCB from Schematic** (conn-a re-syncs names; conn-b picks up the LED flip; rak picks up moved pull-ups).
7. **Run real ERC+DRC** (KiCad 10 on your machine — kicad-cli sits in the KiCad `bin` folder):
   ```
   kicad-cli sch erc --severity-error --severity-warning --exit-code-violations "adapter-rak4630.kicad_sch"
   kicad-cli pcb drc --severity-error --severity-warning --exit-code-violations "rak4630-adapter.kicad_pcb"
   ```
   (repeat per board — or just Inspect → ERC/DRC in the GUI; expect the known nags #2/#8)
8. **Close F2** (STEP overlay or 1:1 print) — do not skip.
9. Fabrication Toolkit per board with **Exclude DNP ✓** → upload → in the JLC tool: confirm the 4 watch-list parts live, check component rotation preview (U1/U2 on conn-b, U4 on conn-a, USB-C + RAK module on rak), expect unmatched lines only for the hand-solder THT set.
10. Order: 1.6 mm FR4 (pending #4 USB-C check), any finish, front-side SMD assembly; conn-b THT (terminal/JSTs/toggle) via JLC THT assembly or hand; **platform ×2 per repeater**, bare.

## Open items only you can close

RAK stamp chirality (F2) · USB-C vs 1.6 mm board (#4) · reverse-panel decision (F5) · SH-to-GND decision (#10) · Kinghelm switch continuity spot-check on first article · harness pin-1 discipline at build time · RTC TCE/BSM firmware config (#13).

---

*Method note: netlists were extracted independently from the schematics (geometric netlister, transform-validated) and cross-checked against the pad nets KiCad wrote into each PCB — exact agreement on all five boards, so the netlist statements above carry PCB-level confidence. Datasheet checks used the official Walter v1.5/v1.6 schematics in your repo plus TI/ST/Microchip/Micro Crystal/RAK/Espressif documents. Stock data: jlcsearch mirror of JLC's assembly DB + live LCSC (2026-08-11) — re-verify the watch-list at upload time.*
