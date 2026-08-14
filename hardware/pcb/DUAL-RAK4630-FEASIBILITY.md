# Dual-RAK4630 Adapter — Feasibility Investigation

*TwinOak standard repeater v2 · 2026-08-11 · updated 2026-08-12 (§6: same-channel bridge addendum)*
*Scope: a new LoRa-box adapter carrying **two** RAK4630 stamp modules. Walter adapter, platform boards, connector boards A and B stay **exactly as designed**. Remote flashing and control of both modules via the Walter must work as it does today for the single module.*

**Verdict: feasible.** The D-sub is not actually the blocker — you don't need more pins. What you need is *target selection inside the LoRa box*, and there is a clean way for the Walter to signal "which module" over the existing lines, with **zero firmware on the adapter** and no dependency on either RAK being alive. Everything reduces to one new adapter PCB plus a ~20-line addition to the Walter's firmware.

---

## 1. What the adapter actually receives

Per `INTERCONNECT.md`, the dual adapter sees exactly what the single one sees (LoRa-box D-sub, via the unchanged platform board):

| Pin | Net | Direction (adapter view) |
|---|---|---|
| 1 | GND | — |
| 2 | 3V3_LORA (A-board TPS63001) | power in |
| 3 | LORA_nRESET (Walter IO1) | in |
| 4 | LORA_SWDCLK (Walter IO2) | in |
| 5 | LORA_SWDIO (Walter IO4) | bidir |
| 6 | I2C_SCL (to INA3221 on B) | out (RAK is master) |
| 7 | I2C_SDA | bidir |
| 8 | LORA_UART_RX (← WALTER_TX) | in |
| 9 | LORA_UART_TX (→ WALTER_RX) | out |

One reset, one SWD port, one UART, one I2C master role — for two modules. Those are the four sharing problems; each has a clean answer.

## 2. The four sharing problems

### 2.1 SWD — you must multiplex; you cannot parallel

The obvious hope is SWD multi-drop (several targets parallel on one SWDIO/SWDCLK, selected via `TARGETSEL`). **The nRF52840 does not support it** — multi-drop requires an ARM DPv2 debug port, and Nordic has confirmed the nRF52840's SW-DP has no multi-drop capability. Two live DPs paralleled on one wire pair would both respond to the line-reset + `DPIDR` read and fight over SWDIO.

So the SWD pair must be **switched** to one module at a time with an analog switch. This is trivial electrically: the Walter bit-bangs SWD at ≲300 kHz through the A-board's 470 Ω series resistors and two 1000 pF bulkhead filters (τ ≈ 1 µs — already the speed limiter). A 10 Ω analog switch (TS5A23157, LCSC **C11133**) in the path is invisible at those speeds. The unselected module's SWD pins float safely — nRF52 keeps an internal pull-up on SWDIO and pull-down on SWDCLK.

### 2.2 nRESET — one line, two targets

If nRESET were shared, every flash of module B would also reboot module A. So nRESET is **routed** (same SPDT approach) to the selected module only, and each module keeps its own local reset push-button (2× Kinghelm KH-6X6X5H-STM, as on the current adapter). The unselected module's reset gets a local 10 k pull-up so it idles released. Pin-reset on nRF52 is hard-wired behaviour (via UICR `PSELRESET`), so a hung module can always be reset — same caveat as today: a full chip-erase clears UICR, and your Walter flasher must restore `PSELRESET` afterwards. You already live with this on the single-RAK design, nothing new.

### 2.3 UART — stock MeshCore is point-to-point, so mux it

MeshCore's serial interface is a point-to-point protocol with no device addressing. Two stock-firmware nodes cannot share one UART as a bus (both would answer, and both would emit unsolicited traffic). Two honest options:

* **Analog mux (recommended).** Walter's TX is routed to one module's RX, and that module's TX is routed back. One TS5A23157 handles both directions. The unselected module's RX is pulled up to idle-high; its TX simply goes nowhere. Fully transparent to MeshCore — the Walter just decides *who it is talking to*, exactly like plugging the cable into the other module.
* **Bridge MCU (your "UART bridge" idea).** A small MCU on the adapter speaks to the Walter on one UART and fans out to both RAKs, adding an addressing layer. It works, but it adds a third firmware image, a flashable single point of failure between you and both radios, and it cannot carry SWD (so you still need the switching logic for flashing). Given that the mux gives you everything the bridge would except *simultaneous* streaming from both modules, the mux wins. (If you ever want concurrent streams, the ATtiny variant in §4 can grow into a concentrator later.)

Switching the mux mid-stream can corrupt at most one byte; the Walter chooses when to flip, so in practice it flips between polls.

### 2.4 I2C — segment the bus; never share the master role

Three hard constraints collide here: the LoRa module is the *single* I2C master by design; nRF52 TWIM has no multi-master arbitration; and both modules running stock MeshCore will each want an RV-3028 RTC, which has a **fixed address 0x52** — two of them cannot coexist on one bus anyway. The clean answer is segmentation, no switching required:

* **RAK-A's bus = today's bus, unchanged.** Its P0.13/P0.14 carry its own RV-3028 (with backup caps, riding through kills) *and* the D-sub SCL/SDA out to the INA3221 on the B-board, with the same 1 k pull-ups to 3V3_LORA (still needed — the bulkhead's ~1.2 nF is unchanged).
* **RAK-B gets a private bus.** Its P0.13/P0.14 see only its own RV-3028 with ordinary 4.7 k pull-ups (short traces, no filter caps). MeshCore auto-detects the RTC exactly as on module A.

Consequence: battery telemetry (INA3221) is visible to module A only. That is the right outcome anyway — with both modules running MeshCore you want exactly one of them reporting battery state, not two nodes double-polling one sensor. Module A is thereby the "primary" node. (If you ever need B to reach the INA3221, the spare half of one TS5A23157 can steer the external segment — but I'd leave that off v1.)

## 3. The clever part — selecting a target over the existing wires

The select logic must satisfy three requirements: controllable by the Walter using only pins 3/4/5/8/9; **no dependency on either RAK being alive** (a bricked module must always be reflashable — this rules out any scheme where a RAK sets the mux, e.g. an I2C GPIO expander, which dies with its master); and defined behaviour at power-on.

**Key observation:** in normal operation and in every sane flash sequence, SWDCLK never toggles *while nRESET is held asserted*. The Walter owns both lines, so you can simply make that a rule of your flasher — which turns "SWDCLK edges during reset-low" into a free, unused signalling channel. No new pins, no new protocol on any existing traffic.

**Implementation — a 2-bit shift register, no MCU:**

```
                        ┌──────────────────────────────┐
 nRESET (pin 3) ───┬───►│ 1G97: CLK_EN = ~nRESET       │
 SWDCLK (pin 4) ───┼───►│ SHIFT_CLK = SWDCLK & ~nRESET │
                   │    └──────────────┬───────────────┘
                   │                   ▼
 SWDIO (pin 5) ──── D ──►[FF1 1G175]──►[FF2 1G175]     RC on CLR pins:
                          │ Q = DEBUG_SEL │ Q = UART_SEL   power-on = 0,0 → module A
                   │      ▼               ▼
                   │   SPDT: SWDIO/SWDCLK/nRESET → A or B   SPDT: UART TX/RX → A or B
                   │
                   └──►[RC ~2–5 ms + 1G17 Schmitt]──► nRESET to *selected* module
```

* **Parts:** 2× SN74LVC1G175 D-FF with clear (LCSC C2677755 / C202238), 1× 74LVC1G97 configurable gate (computes `SWDCLK AND NOT nRESET`), 1× 74LVC1G17 Schmitt buffer, 3× TS5A23157 dual-SPDT (C11133) — six SC70/MSOP-class chips, all JLC-assemblable, **zero firmware**.
* **Programming a selection:** Walter drives nRESET low, clocks 2 bits in on SWDCLK with SWDIO as data (FF2's bit first), releases nRESET. Takes microseconds. Idempotent — the Walter never needs to *know* the current state, it just re-programs it before every operation.
* **The reset that programs the mux never reaches the radios.** nRESET toward the modules passes an RC (~2–5 ms) + Schmitt buffer, so the sub-millisecond programming window is filtered out entirely. Flipping the UART from A to B **does not reboot anything**. A real reset = Walter holds nRESET ≥ 50 ms (which also still supports the bootloader's double-tap DFU entry, per module). The pulse must exceed the ~1 µs bulkhead RC, so a 100–500 µs programming window is the sweet spot.
* **Power-on default = module A** (RC clear on the FFs), so a fresh unit behaves exactly like the single-RAK design until told otherwise.
* **Stray-clock safety:** during the programming window the 2–3 SWDCLK pulses do reach the previously-selected module's SWD pins, but a valid SWD transaction needs a full 8-bit header — a couple of stray clocks cannot form one. Conversely, if a Walter reboot ever glitches a spurious bit into the FFs, nothing breaks: the next operation re-programs the state anyway.
* **Bench override:** two 3-pin THT jumpers (repo convention) can force DEBUG_SEL / UART_SEL to A or B, bypassing the FFs during bring-up.

**Why two independent bits:** DEBUG_SEL (SWD + reset) and UART_SEL are separate, so the Walter can keep the live console on module A *while* it flashes module B over SWD. A one-bit version saves one FF and loses that.

**Alternative implementation:** one ATtiny202/402 decoding the same (or a richer) signalling scheme and driving the three select lines. One chip instead of four logic parts, and extensible (pulse-width protocols, UART concentration). Cost: a third firmware image and a factory UPDI-programming step. For an unattended hilltop box I'd take the discrete logic — it cannot be bricked, ESD-corrupted, or version-skewed. Both fit the board.

## 4. Walter firmware rules (the entire software cost)

1. Never toggle SWDCLK while nRESET is asserted, except deliberately, to program the select register (reorder the flash sequence if it currently clocks during reset — attach via the normal SWD line-reset sequence with nRESET released).
2. Program the select register explicitly before every SWD or UART operation; assume nothing about its current state.
3. Real resets: hold nRESET ≥ 50 ms. Select programming: 100–500 µs window.
4. After a full chip-erase, restore UICR `PSELRESET` (existing rule, unchanged).
5. Tristate nRESET/SWD/UART before asserting LORA_PWR_KILL (existing rule, unchanged).

## 5. Power

* **Rail capacity:** TPS63001 on the A-board delivers ~1.2 A in step-down mode (V_IN ≥ 3.6 V) and ≥ 800 mA worst-case across the LiPo range. Worst-case dual load — both nRF52840s active plus both SX1262s in TX at +22 dBm (118 mA typ each) — is ~260–300 mA. Comfortable margin; no A-board change needed.
* **Bulk:** double the adapter's rail bulk to 4× 100 µF MLCC (two per module, per the current BOM pattern), plus each module's RTC keeps its own 2× 100 µF backup pair.
* **Energy budget (the real cost):** an always-listening MeshCore node is roughly 8–15 mA at 3.3 V (nRF52 + SX1262 RX 4.6 mA, DC-DC). A second radio roughly **doubles the site's quiescent draw** — ~0.2–0.3 Ah/day extra at battery voltage. With the 10 Ah battery: no-sun autonomy drops from ~35–40 days to ~18–22 days. The 10 W panel still covers it in any realistic Danish winter, but the dual box has half the dark-weather reserve of a single. Worth stating on the site sheet.
* **Kill line:** LORA_PWR_KILL cuts 3V3_LORA — i.e. **both** modules together. You cannot power-cycle one module alone. In practice this doesn't matter: per-module pin-reset (routed) plus SWD recovery covers every soft-failure mode, and rail-kill remains the big hammer. Per-module load switches could be added, but need a third select bit — not worth it for v1.

## 6. RF — two antennas, both modules on air

You've chosen two separate antennas (second N/SMA bulkhead on the LoRa box, both RAK4630s the u.FL variant, pigtails only — still no RF on the PCB). The numbers that matter:

* **Damage:** SX1262 absolute-maximum RF input is **+10 dBm**. At +22 dBm TX, you need > 12 dB antenna-to-antenna isolation to protect the other receiver — vertically separated collinears on one mast give ~25–40 dB, so damage is a non-issue with any sane mounting. Aim for ≥ 30 dB (≥ 1–2 m vertical spacing; two verticals can't use cross-pol).
* **Desense is unavoidable:** at 30 dB isolation the victim still sees ~-8 dBm during the other's TX — total front-end blocking for that airtime. With LoRa repeater duty cycles (≲1–2 %) the throughput cost is small, but any packet arriving during the neighbour's TX is lost. This is inherent to co-located radios, not to this board.
* **Same channel:** pointless co-location (two omnis, same coverage) just doubles airtime for nothing. But the **bridge configuration is the legitimate same-channel case** — see §6.1.
* **Filtering:** the cavity filter serves module A's chain as today. Module B gets its own inline filter; in the same-channel bridge configuration it is simply a second unit of the same part (CF866.5KT30-class), which keeps the two-feedthrough plan a straight copy of the existing chain.

### 6.1 Same-channel bridge (the planned configuration)

Planned deployment: **RAK-A on the omni** (local coverage), **RAK-B on yagi(s)** toward one or more distant sites — a deliberate long-distance bridge, both radios on the DK standard channel, linked to each other through antenna-to-antenna coupling.

* **The site-internal link is guaranteed, not hoped-for.** Co-mounted antennas couple at ~25–45 dB isolation, so a +22 dBm TX is heard by the sibling at roughly −10 to −25 dBm against a ≈ −124 dBm sensitivity floor (SF8/62.5 kHz) — around **100 dB of margin**. Even a yagi back-lobe into an omni 2 m up the mast lands near −50 dBm. Any mounting that isn't actively adversarial works.
* **The real constraint is the opposite one:** stay under the SX1262's +10 dBm abs-max input, i.e. > 12 dB isolation — only violated if the yagi's main lobe fires point-blank (< ~0.5 m) into the omni. Mount the omni above/behind the yagis, out of boresight.
* **Costs, inherent to the topology:** every packet in range is repeated twice from one site (extra airtime/duty cycle), and each radio is deaf while the other transmits (same-channel "channel busy" — when they collide, it is usually with the same packet). MeshCore's dedup prevents repeat loops.
* **ERP on the yagi chain needs checking:** +22 dBm into a 12 dBi-class yagi minus ~2 dB filter+coax ≈ **+32 dBm ERP** vs the 500 mW (27 dBm) allowance of the 869.4–869.65 MHz sub-band. Because the Walter reaches each module independently (§3), run **asymmetric TX power**: omni node at the network standard, bridge node trimmed to ~16–17 dBm. RX keeps the full yagi gain either way, which is most of what the long shot needs.
* **Multi-yagi option (3-way combiner, three bridges at once):** a real 3-way divider costs 10·log 3 ≈ 4.8 dB + ~0.5–1 dB insertion. TX side that is self-solving — ~16.5 dBm per port ⇒ ≈ 26.5 dBm ERP per lobe at full +22 dBm TX, no power trim needed. The hidden price: the **same ~5.5 dB comes off RX** on all three bridges, and the receiver sums noise from all three bearings (one noisy azimuth raises the floor for all three links). Keep bearings well separated (> ~60°, no far site visible in two lobes — coherent feed means overlap ripple/nulls). Trick: cascade two 2-way splitters (−3.5/−7/−7 dB) and give the −3.5 dB port to the longest link (re-check that lobe's ERP, ≈ 28 dBm at 12 dBi). Use a proper 50 Ω Wilkinson-type divider rated through 868 MHz, not a 75 Ω CATV splitter. Combiner sits radio → filter → splitter → yagis, so one filter still serves the whole bridge side. If one link proves marginal in the field, fall back to a dedicated yagi on B and let nearer sites ride the omni.

## 7. Mechanical / layout

The platform-adapter footprint fixes the interface: 53.56 × 58.42 mm outline, four 5-pad header groups near the corners. Budget on that canvas: 2× RAK4630 (24.6 × 16.6 mm courtyard each), 2× RV-3028 + backup caps, 2× vertical USB-C (XUNPU TYPEC-303-ACP16) + ESD + CC resistors, 2× reset buttons, 6 small logic/switch chips, pull-ups, override jumpers. That is dense but plausible — roughly 2× the current adapter's population on the same area, and the current board has generous free space. Fallbacks if it won't route: hardwire Serial1 (drop the per-module J4/J5 jumpers — MeshCore default anyway), move the override jumpers to the back, or let the PCB overhang the platform outline (only the pad positions are sacred — check clearance to the box wall and the harness first). Both u.FL connectors face up with short pigtail runs to the two bulkheads.

USB note: two independent USB-C ports, one per module, same circuit as today (USBLC6-2SC6 + 5k1 CC pull-downs each). Nothing shared, nothing clever — in the field you plug the laptop into the one you mean.

## 8. What does NOT change

Walter adapter, both platform boards, connector board A, connector board B, both D-sub pinouts, the ribbon, the harness, the kill circuit, the flashing transport, the I2C architecture as seen from outside the box. The entire delta = one new PCB (`adapter-rak4630-dual`) + the Walter firmware rules in §4.

## 9. New-parts BOM sketch (all LCSC, JLC-assemblable)

| Qty | Part | Role | LCSC |
|---|---|---|---|
| 3 | TS5A23157DGSR dual SPDT, 10 Ω | SWDIO+SWDCLK / nRESET / UART TX+RX routing | C11133 |
| 2 | SN74LVC1G175 (SOT-23-6 / SC70) | select shift register | C2677755 / C202238 |
| 1 | 74LVC1G97 | clock gate `SWDCLK & ~nRESET` | check stock (any 1G00+1G04 pair substitutes) |
| 1 | 74LVC1G17 | Schmitt on filtered nRESET | check stock |
| 1 | RAK4630-8-SM-I (u.FL) | second radio | consigned/RAK as today |
| 1 | RV-3028-C7 + 2× 100 µF | second RTC + backup | as current BOM |
| 1 | XUNPU TYPEC-303-ACP16 + USBLC6-2SC6 + 2× 5k1 | second USB port | as current BOM |
| 1 | KH-6X6X5H-STM | second reset button | C2837531 |
| — | RC parts: FF clear (POR), reset filter, pull-ups (RX idle, nRESET, B-bus 4.7 k) | glue | basic parts |

## 10. Residual risks / open items

1. **Layout density** on the fixed 53.56 × 58.42 mm outline — do a placement study before committing (the one genuinely unknown item).
2. **Walter flasher sequence audit** — confirm it never clocks SWDCLK during asserted reset today; reorder if it does.
3. **Enclosure** — second N/SMA bulkhead hole + second filter + antenna spacing on the mast (site-side, not board-side).
4. **Select-logic bring-up test** — verify the RC/Schmitt reset filter passes double-tap DFU timing and blocks the programming window on the real bulkhead capacitances; values are trimmable (THT-friendly 0603s).
5. If you later want *simultaneous* UART streams from both modules, swap the discrete select logic for the ATtiny variant — footprint-compatible decision you can defer.

## Sources

- [Nordic DevZone — nRF52840 SW-DP multi-drop support (Nordic: not supported)](https://devzone.nordicsemi.com/f/nordic-q-a/51470/does-the-nrf52840-support-sw-dp-multi-drop)
- [Semtech SX1261/2 datasheet — abs-max RF input +10 dBm, RX 4.6 mA DC-DC, TX 118 mA @ +22 dBm](https://uelectronics.com/wp-content/uploads/2022/12/Datasheet-LoRa-SX1262.pdf)
- [TI TPS63001 datasheet — output-current capability](https://www.ti.com/lit/ds/symlink/tps63001.pdf)
- [LCSC — TS5A23157DGSR (C11133)](https://www.lcsc.com/product-detail/Analog-Switches-Multiplexers_Texas-Instruments_C11133.html)
- [LCSC — SN74LVC1G175 (C2677755 / C202238)](https://www.lcsc.com/product-detail/Flip-Flops_Texas-Instruments_C2677755.html)
- Repo: `hardware/pcb/INTERCONNECT.md`, `adapter-rak4630/rak4630-adapter.kicad_sch`, `connector-board-a/connector-board-a.kicad_sch`, `lib/TwinOak.pretty/platform-adapter-footprint.kicad_mod` + `RAK4630.kicad_mod`
