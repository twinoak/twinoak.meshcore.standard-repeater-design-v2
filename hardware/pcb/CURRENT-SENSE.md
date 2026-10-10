# Connector board B — current sensing and shunt design (RS1–RS4)

*Written 2026-10-10 from `connector-board-b.kicad_pcb` (2026-08-21 export), the BQ24650 and INA3221
datasheets, the LCSC part pages for the three shunt part numbers, and MeshCore's
`EnvironmentSensorManager.cpp`. Purpose: record why the four shunts have the values they have,
what each costs, what firmware must assume, and what to verify on the first article.*

**Verdict:** all four values are valid and each is sized by a different constraint. The hardware
needs no change. What was wrong is the *firmware*: neither NoiseScope nor stock MeshCore read the
three INA3221 channels with the right shunt values (§4).

**Decision 2026-10-10 (TH): keep the mixed shunts (option A in §5).** The repeater will run custom
MeshCore-based firmware anyway, so per-channel shunt values live in that firmware. NoiseScope was
fixed the same day (`INA_SHUNT_CH1/2/3_MILLIOHM` = 100 / 20 / 50 in `src/config.h`, per-channel
table in `src/telemetry.cpp`, sign convention documented in `PROTOCOL.md`).

## 1. Topology (as routed)

```
PANEL_IN ─D4─ PANEL_RAW ─SW1─ PANEL+ ──RS2──► VIN ──► BQ24650 ──► L1 ──RS1──► VCC ──RS3──► VBAT ─F1─ BAT+ ─SW1─ BAT_RAW ─J7─ pack
                                     100 mΩ   (charger in)          SRP 20 mΩ SRN    20 mΩ
                                     ch1                                               ch2
                                                                                 VCC ──RS4──► VLOAD+ ──► LTE D-sub 2 / ribbon 2
                                                                                      50 mΩ
                                                                                      ch3
```

* The charger's **voltage regulation point is VCC** (R3/R4 499k/499k → VFB 2.1 V → 4.200 V), i.e.
  the charger side of RS3. The pack sees 4.2 V minus I·(RS3 + F1 + SW1 + JST/wire).
* The **load hangs on VCC**, between RS1 and RS3. Consequences: the charger's sensed current is
  battery + load; battery current (ch2) is cleanly separated from load current (ch3).
* MPPSET senses VIN (after RS2): the panel terminal sits I·RS2 ≈ 55 mV above the 17.83 V
  setpoint at Imp — negligible (0.3 %).

INA3221 wiring (VQFN pin order verified against the datasheet):

| Ch | IN+ | IN− | Positive current means | Bus voltage read at IN− |
|---|---|---|---|---|
| 1 | PANEL+ (pin 12) | VIN (pin 11) | panel → charger | VIN (≈ panel voltage) |
| 2 | VCC (pin 15) | VBAT (pin 14) | **charging** (discharge reads negative) | VBAT (pack side of RS3, before F1) |
| 3 | VCC (pin 2) | VLOAD+ (pin 1) | load draw | VLOAD+ (rail feeding both boxes) |

VS and VPU = MCP1700 3.3 V (always on); A0 → SDA → **0x42** (stock MeshCore's default INA3221
address). Warning / Critical / PV / TC not connected.

## 2. RS1 — BQ24650 charge-current sense (not telemetry)

| Item | Value | Source |
|---|---|---|
| Part | 20 mΩ 1 %, Milliohm HoJLR1206-1W (C2903502), **1 W** current-sense | LCSC |
| Fast-charge current | 40 mV / 20 mΩ = **2.00 A ±3 %** | BQ24650 §8.3.3 (V_IREG = 40 mV) |
| Precharge current (VFB < 1.55 V ⇒ VCC < **3.10 V**) | 4 mV / 20 mΩ = **200 mA ±25 %** | §8.3.4 |
| Termination current (TERM_EN = VREF) | **200 mA ±25 %**, 100 ms deglitch + 250 ms qualify | §8.3.5 |
| Recharge | VCC < 4.20 − 0.05 = **4.15 V** | V_RECHG 50 mV |
| Dissipation at 2 A | 80 mW (8 % of rating) | — |
| SRP–SRN filter | C10 100 nF differential — as the datasheet asks | — |

Why 2 A is the right ceiling: the 10 W panel can deliver at most ≈ 10 W × 0.9 / 4.2 V ≈ 2.1 A into
a full pack (≈ 2.5 A into a 3.6 V pack), so in strong sun the charger is **current-limited, not
MPPT-limited** — expected, and it leaves only a few percent of panel power on the table. 2 A is
0.2 C for the 10 Ah pack, within the JST-EH 3 A contact rating, and below F1's 3 A hold. The
pack's own protection PCB (Meshnology) has an unstated charge-current limit — confirm it tolerates
2 A before the first full-sun charge.

Behaviour to expect because the load sits on VCC:
* Termination needs (I_bat + I_load) < 200 mA for 250 ms. With an idle load of 20–60 mA that
  happens when the pack tapers to ≈ 140–180 mA. During a period of heavy LTE activity (average
  load > 200 mA) the charger simply never terminates and floats the pack at 4.2 V — harmless.
* After termination the load pulls VCC below 4.15 V almost at once → recharge cycle. Normal; the
  DONE LED will blink in a slow rhythm rather than stay lit. Not a fault.
* Below 3.10 V at VCC the charger only supplies 200 mA. A pack that sat at its protection cut-off
  plus a Walter trying to boot (0.3–1.5 A) at dawn can out-draw the precharge; the pack covers the
  difference once its protection re-enables. Test this "dead-battery dawn" case on the bench.

## 3. RS2 / RS3 / RS4 — INA3221 telemetry shunts

INA3221: shunt range ±163.84 mV, **40 µV/LSB** (13-bit), bus 0–26 V at 8 mV/LSB, IN± abs-max 26 V.

| Ch | Ref | Value | Part / rating | Full scale | LSB | Max expected current | Drop / power at max | Utilisation |
|---|---|---|---|---|---|---|---|---|
| 1 panel | RS2 | 100 mΩ | UNI-ROYAL 1206W4F100LT5E (C25334), **0.25 W thick film, ±800 ppm/°C** | 1.64 A | 0.4 mA | 0.55 A (Imp), 0.6 A (Isc) | 55–60 mV / 36 mW | 37 % |
| 2 battery | RS3 | 20 mΩ | Milliohm HoJLR1206-1W-20mR (C2903502), **1 W** sense | ±8.19 A | 2 mA | +2.0 A charge / −1.8 A burst | 40 mV / 80 mW | 24 % |
| 3 load | RS4 | 50 mΩ | Milliohm HoJLR1206-1W-50mR (C2904224), **1 W** sense | 3.28 A | 0.8 mA | 1.8 A (Walter 1.5 A burst + LoRa 0.3 A) | 90 mV / 162 mW | 55 % |

All three stay well inside the ±163.8 mV window, dissipate a small fraction of their rating, and
drop little against their rails. **Valid as designed.** The weak points, in order of importance:

1. **Ch2 resolution.** 2 mA/LSB against a 10–40 mA idle battery current is 5–20 % per step.
   Adequate for trending and for "is it charging", too coarse for coulomb counting. The INA3221
   is a 13-bit monitor; if SoC-by-integration is ever wanted, that is an INA226/INA228 job, not a
   shunt change. Averaging (AVG = 64…256) smooths but does not add resolution.
2. **Ch1 part type.** Thick film at ±800 ppm/°C drifts ≈ ±3 % over −20…+60 °C on top of ±1 %.
   Irrelevant for panel telemetry; the Milliohm family has a 100 mΩ sibling if uniformity matters.
3. **Walter VIN drop budget at an 1.8 A burst** (series path pack → Walter): RS3 36 mV + RS4
   90 mV + F1 PTC ≈ 40–100 mV + SW1 ≈ 20 mV + JST/wire ≈ 30 mV + LM66100 20–90 mV ≈
   **0.25–0.35 V**. At a 3.5 V pack that is ≈ 3.2 V at the Walter's VIN pin (the 470 µF on the
   adapter carries the sub-ms peaks). Check against the Walter's VIN / modem minimum before relying
   on LTE at the bottom of the pack. RS4 is the only one of these you can trade (→ 20 mΩ saves
   54 mV at the cost of 2 mA/LSB on the load channel).
4. **Ch1 common mode.** Cold-dawn Voc of the CL-SM10P reaches ≈ 24–25 V at −20 °C (21.6 V ×
   ~0.35 %/K) while the charger idles (full pack) — ≈ 1 V under the INA3221's 26 V abs-max. D5
   SMBJ26A only clamps above ≈ 29 V, so it protects against hot-plug ringing, not this. Known from
   the 08-11 preflight and accepted; noted here so nobody is surprised by a dead ch1 after a cold
   winter.
5. Input offset. The INA3221's shunt offset (tens of µV) maps to a few mA on the 20 mΩ channel.
   Record the zero-current reading of each channel at first power-up and subtract in firmware.

## 4. Firmware — the real finding

Nothing reads the board correctly today:

| Firmware | Assumes | Ch1 (100 mΩ) | Ch2 (20 mΩ) | Ch3 (50 mΩ) |
|---|---|---|---|---|
| NoiseScope `config.h`: `INA_SHUNT_MILLIOHM 20` for all channels | 20 mΩ | reads **5× high** | correct | reads **2.5× high** |
| Stock MeshCore `EnvironmentSensorManager.cpp`: `TELEM_INA3221_SHUNT_VALUE 0.100`, applied to every channel via `setShuntResistance(i, …)` | 100 mΩ | correct | reads **5× low** | reads **2× low** |

Stock MeshCore (`ENV_INCLUDE_INA3221`) probes 0x42 by default, enumerates the enabled channels and
reports voltage / current / power per channel over LPP — so this board *is* plug-and-play with
stock firmware except for the shunt constant, which is one value for all three channels.

## 5. Options (decide before ordering — it is two BOM fields)

**A — keep the mixed shunts, fix firmware per channel.** NoiseScope: three constants instead of
one. MeshCore: `TELEM_INA3221_SHUNT_VALUE` has to become per-channel (`…_CH1/2/3`) — a small,
upstream-able patch, but a patch you carry until it lands. Keeps the 2 A charge path at its
lowest impedance.

**B — uniform 50 mΩ on RS2, RS3, RS4** (all C2904224, the 1 W part already on the board; C25334
and one use of C2903502 drop out). Then stock MeshCore is correct with a single build flag
`-D TELEM_INA3221_SHUNT_VALUE=0.05`, NoiseScope stays single-constant (`50`), and every channel
gets 0.8 mA/LSB.

| | Ch1 panel | Ch2 battery | Ch3 load |
|---|---|---|---|
| Full scale | 3.28 A (room for a 20 W panel) | ±3.28 A (2 A charge = 61 %) | 3.28 A (unchanged) |
| LSB | 0.8 mA (was 0.4 — irrelevant) | **0.8 mA (was 2 mA)** | 0.8 mA |
| Drop / power at max | 28 mV / 15 mW | **100 mV / 200 mW** at 2 A (20 % of 1 W) | unchanged |

Costs of B: the pack sees ≈ 100 mV less than VCC during full-current charge, so the CV phase
starts a little earlier and the top-off takes longer (pack reaches ≈ 4.18 V at the 200 mA
termination point — no safety change); the burst-drop budget in §3.3 gets 54 mV worse.

**C — uniform 100 mΩ (stock MeshCore untouched): no.** Ch2 and ch3 saturate at 1.64 A (2 A charge,
1.8 A bursts), 200 mV / 0.4 W drops.

Chosen: **A** (2026-10-10) — custom MeshCore-based firmware is planned regardless, so the
per-channel values go there; B stays documented as the fallback if stock firmware ever has to run
on this board unmodified.

## 6. First-article checks for this block

1. Zero-current offsets: switch ON, no panel, both boxes unpowered → read all three shunt
   registers, store as offsets.
2. Known-current check with a DMM in series on each path at 0.1 / 0.5 / 1.0 / 2.0 A (panel path
   from a CC-limited bench supply at 18–21 V; battery and load paths with an electronic load).
3. Sign: ch2 negative while discharging into a load, positive while charging. Ch3 positive.
4. Charge profile with a real 1S pack: CC 2.0 A ±3 % → CV at VCC = 4.200 V ±0.5 % → DONE at
   ≈ 200 mA ±25 % → recharge when VCC < 4.15 V. Precharge 200 mA with the pack below 3.1 V.
5. "Dead-battery dawn": pack at protection cut-off, Walter + LoRa connected, panel supply ramped
   from 0 — does the system come up without a brown-out loop?
6. Thermal: RS1, RS3 and the 40 mil spine after 30 min at 2 A (expect < +10 °C).
7. INA3221 configuration that the firmware should use: continuous shunt+bus, 1.1 ms conversions,
   AVG = 64–256, all three channels enabled.
