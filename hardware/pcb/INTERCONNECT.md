# TwinOak Standard Repeater v2 — Interconnect Reference

Single source of truth for every pin and net between the boards.
Updated 2026-08-09 (post connection-audit + A/B power move).

## Topology

```
[LoRa module] ─ [LoRa adapter] ─ [platform board] ─ ribbon+IDC ─ (filtered D-sub bulkhead, LoRa box)
                                                                        │  ← connector board A (outside, D-sub male + 3V3 regulator)
                                                                   10-wire ribbon (A↔B)
                                                                        │  ← connector board B (outside, D-sub male + charger + telemetry)
[Walter]      ─ [LTE adapter]  ─ [platform board] ─ ribbon+IDC ─ (filtered D-sub bulkhead, LTE box)
```

* Both **platform boards are identical** (dumb 1:9 pass-through). Only the adapters and connector boards give pins meaning.
* Bulkheads: Amphenol **FCE17-E09SM-250** filtered D-sub, **1000 pF** per line, **solder cup** — internal harness is wired **1:1 by pin number** (ribbon conductor 10 is unused at D-sub ends).
* Power architecture: solar → BQ24650 charger (B) → 1S LiPo. Raw battery feeds the Walter box (LTE D-sub pin 2) and, over the A↔B ribbon, the **TPS63001 3V3_LORA buck-boost on the A-board**. The Walter can kill 3V3_LORA remotely (LORA_PWR_KILL, default = ON when the kill line floats).

## D-sub pinouts (both are 9-pin, but NOT identical)

| Pin | LoRa box (A-side)  | LTE box (B-side)             |
|----:|--------------------|------------------------------|
| 1   | GND                | GND                          |
| 2   | **3V3_LORA** (from A-board regulator) | **VLOAD+** (raw battery, INA ch3-measured) |
| 3   | LORA_nRESET        | LORA_nRESET (Walter IO1)     |
| 4   | LORA_SWDCLK ¹      | LORA_SWDCLK (Walter IO2)     |
| 5   | LORA_SWDIO ¹       | LORA_SWDIO (Walter IO4)      |
| 6   | I2C_SCL            | **LORA_PWR_KILL** (Walter IO5) |
| 7   | I2C_SDA            | — spare / NC                 |
| 8   | LORA_UART_RX       | WALTER_TX (Walter IO7)       |
| 9   | LORA_UART_TX       | WALTER_RX (Walter IO8)       |

¹ On the Heltec V3 adapter pin 4 is the **PRG/GPIO0 boot strap** and pin 5 is unused (ESP32 has no SWD).

Pins 3, 4, 5, 8, 9 are straight-through Walter↔LoRa links (via B-board). Pins 6, 7 differ per side; pin 2 carries a different rail per side.

## A↔B ribbon (10-wire, TE 8-215570-0 header both ends, 1:1)

| Wire | Net            | Notes                                        |
|-----:|----------------|----------------------------------------------|
| 1    | GND            |                                              |
| 2    | VLOAD+ / VBAT_RAW | raw battery from B-board (`VLOAD+`, measured by INA ch3) → A-board regulator input (`VBAT_RAW`). Currently **unfused** — F2 was dropped; consider a PTC (C438898) in this feed if you want the ribbon protected. |
| 3    | LORA_nRESET    | pass-through to both D-subs                  |
| 4    | LORA_SWDCLK    | pass-through                                 |
| 5    | LORA_SWDIO     | pass-through                                 |
| 6    | LORA_PWR_KILL  | LTE D-sub 6 → A-board kill circuit (Q3/EN)   |
| 7    | I2C_SDA        | LoRa D-sub 7 ↔ INA3221 on B-board            |
| 8    | WALTER_TX / LORA_UART_RX | pass-through                       |
| 9    | WALTER_RX / LORA_UART_TX | pass-through                       |
| 10   | I2C_SCL        | LoRa D-sub 6 ↔ INA3221 on B-board            |

## Platform board (identical everywhere)

Ribbon/D-sub pin → header rows: 1 = GND (all row pins 1), 2 = VBAT rail (rows A/C pin 2), 3–9 = IO1–IO7.
Adapter interface = 4×5-pin rows; rows A‖C and B‖D are paralleled pin-for-pin.
Note: platform J1 uses a 9-pin symbol on the 10-pad 82155700 footprint — pad 10 (unused IDC conductor) has no net. Cosmetic only.

## Adapter pin maps (platform IO → module pin)

| IO (D-sub) | Platform_LoRa name | RAK4630 adapter | Heltec V3 adapter | Platform_LTE name | Walter adapter |
|---|---|---|---|---|---|
| VBAT (2) | 3V3 | VBAT_NRF etc. (3.3 V in) | 3V3 header pins | VBAT | VIN (raw batt) |
| IO1 (3) | nRESET | P0.xx NRF_RESET (pin 17) + SW1 | RST | nRESET | Walter IO1 |
| IO2 (4) | SWDCLK | SWDCLK (pin 18) | **GPIO0 / PRG strap** | SWDCLK | Walter IO2 |
| IO3 (5) | SWDIO | SWDIO (pin 19) | — (NC) | SWDIO | Walter IO4 |
| IO4 (6) | SCL | P0.14 I2C_SCL + RV-3028 | GPIO34 (SCL) *LoRa side only — LTE side pin 6 = KILL* | PWR_KILL | Walter IO5 |
| IO5 (7) | SDA | P0.13 I2C_SDA + RV-3028 | GPIO33 (SDA) | SPARE | Walter IO6 |
| IO6 (8) | UART_RX | P0.15 Serial1 RX / P0.19 Serial2 RX (via J4) | GPIO44 U0RXD | UART_TX | Walter IO7 |
| IO7 (9) | UART_TX | P0.16 Serial1 TX / P0.20 Serial2 TX (via J5) | GPIO43 U0TXD | UART_RX | Walter IO8 |

RAK UART selection: 2.54 mm 3-pin headers **J4 (RX) / J5 (TX)** — jumper **1–2 = Serial1 (P0.15/P0.16, Arduino/MeshCore default)**, jumper 2–3 = Serial2 (P0.19/P0.20). The RAK adapter's SCL/SDA sit on the B-row platform pins (7/8) — electrically identical to the D-row (rows are paralleled on the platform board).

## I2C bus (LoRa is the single master)

* Members: RAK/V3 MCU (master), RV-3028-C7 RTC on the RAK adapter (**0x52**), INA3221 on B-board (**0x42**, A0 tied to SDA — deliberate).
* Pull-ups: **one set only — 1 k to +3V3 on the LoRa adapter** (RAK: R1/R2, V3 adapter: R2/R3). The B-board pull-ups were removed. With the rail killed the bus sits low and nothing backfeeds.
* Why 1 k: SDA/SCL cross the LoRa bulkhead filter (1000 pF/line ≈ 1.2 nF total with wiring). τ ≈ 1.3 µs → reaches V_IH in ~1.5 µs. Workable at 100 kHz (rise-time spec technically exceeded; drop the clock if flaky). 10 k would make the bus unusable (τ ≈ 12 µs).
* Sink current at 1 k ≈ 3.3 mA: within INA3221/RV-3028 limits; on nRF52 prefer high-drive (H0D1) pin config if VOL margin is tight.

## UART link (LoRa ↔ Walter)

Push-pull lines through two 1000 pF bulkhead filters (~2.2 nF) — fine at 115200. Recommended default 115200 8N1.
Heltec V3: the link lands on **UART0 (GPIO43/44)** so the Walter can run esptool-style flashing using nRESET + PRG (GPIO0). MeshCore RS232-bridge builds default to GPIO5/6 — override `WITH_RS232_BRIDGE_RX/TX` to 44/43 for this hardware (and mind the UART0 console).
RAK4630: SWD flashing via Walter (pins 3/4/5) stays the primary update path; UART is the data/management link (Serial1).

## Kill line behaviour

`LORA_PWR_KILL` (Walter IO5) → A-board R12 → Q3 gate (R13 100 k pull-down) → pulls TPS63001 EN low.
* Walter pin floating / Walter unpowered → **LoRa rail ON** (safe default).
* Drive HIGH to kill the LoRa 3V3 rail. RTC on the RAK adapter rides through on its backup caps.
* **Firmware rule: tristate nRESET/SWD/UART pins before asserting kill.** The A-board's 470 R series resistors (R1–R5) on pins 3/4/5/8/9 limit backfeed current into the unpowered radio to ~7 mA as a hardware backstop (swap to 0R if they ever bother SWD speed; bit-banged SWD is fine up to ~300 kHz).


## Walter USB / local flashing (added after USB analysis)

The Walter's USB-C VBUS and its VIN header pin are **directly tied** (datasheet: "DO NOT power Walter with both
the USB-C connection and the VIN-pin!"), and the S3's native USB (GPIO19/20) is not on the headers. The adapter
therefore now has:

* **U3 LM66100 ideal diode** (SC-70-6, LCSC C2869734) between the platform battery rail (`VCC`) and the Walter's
  VIN (`WALTER_VIN`), CE tied to GND, C3 100 nF on the input. Bulk C1 (470 µF) + C2 (10 nF) sit on the Walter side
  of the diode. Result: a PC on the onboard USB-C (5 V) simply wins the rail and **cannot back-charge the LiPo**
  through RS4 — hot-plug is safe with the system live. Drop ≈ 20–90 mV; INA ch3 still reads battery-sourced Walter
  current. (Boards built before this change: flip SW1 OFF before connecting USB — 5 V would otherwise force-charge
  the battery past the BQ24650's 4.2 V limit.)
* **J1 recovery header** (1×6, 2.54 mm): 1 = WALTER_VIN (5 V from a dongle is fine — the diode isolates the battery),
  2 = TX0 (GPIO43), 3 = RX0 (GPIO44), 4 = nRST, 5 = IO0 (boot strap, doubles as the module's 3V3_SW enable — unused
  here), 6 = GND. Any USB-UART dongle + esptool can flash the Walter if the native USB ever wedges.

## Board change log (this revision)

* **Bug fixed:** B-board had I2C SCL on ribbon wire 9 while the RAK adapter had SCL on D-sub 6 — SCL never reached the INA3221 and was instead shorted to Walter IO5 through the pin-6 pass-through. SDA happened to line up. New map above is consistent end-to-end (machine-verified netlist walk across all five boards).
* B-board: 3V3_LORA stage (TPS63001 + kill circuit) **moved to the A-board**; B-board pull-ups R9/R10 deleted; J1.7 now NC; every remaining part carries an `LCSC Part #` field. (F2 later removed in your rail rework — ribbon feed is unfused, see ribbon table.)
* A-board: was a dumb 1:1 adapter, now hosts the 3V3_LORA regulator, kill circuit and series protection resistors. Ribbon J2 is now the proper 10-pin 8-215570-0 symbol.
* RAK adapter: labels renamed to real signal names; Serial1/Serial2 selectable via J4/J5 pin-header jumpers; I2C pull-ups 10 k → 1 k.
* Walter adapter: symbol/labels renamed only — **no wire changes**. Firmware: KILL moves from IO8 → IO5; IO7 = TX to LoRa, IO8 = RX from LoRa; IO6 spare.
* Heltec V3 adapter: rebuilt for the real V3 (ESP32-S3) with a pinout-verified module symbol (was a V2 symbol with I2C/UART on input-only GPI36-38, and the module's 3V3 pins were not even connected). Physical 2×18 footprint reused (Heltec: V2→V3 pin layout unchanged) — verify mechanically against a real module before ordering.
* T096 adapter: still an unwired stub — TODO (nRF52840: same SWD+UART+I2C pattern as RAK; MeshCore T096 variant uses P0.07/P0.08 as Wire SDA/SCL, Serial1 on P0.23/P0.25).


## Shared libraries (from this revision)

All custom symbols and footprints live in **`hardware/pcb/lib/`**:
`TwinOak.kicad_sym` (Platform_interface / Platform_LoRa / Platform_LTE, RAK4630, RV-3028-C7, DPT_Walter,
WiFi_LoRa_32_V3, 8-215570-0, TPS63001) and `TwinOak.pretty/` (platform-adapter-footprint, 82155700, RAK4630,
RV-3028-C7, CPH3225A, USB-C XUNPU, Kinghelm switch, CAX toggle, walter-socket/-solder, Heltec module, TI VQFN).
Every project has its own `sym-lib-table`/`fp-lib-table` pointing at them via `${KIPRJMOD}` relative paths, so the
repo is self-contained — no global-library registration needed. Superseded per-project `.pretty` dirs and the old
`hardware/TwinOak-standard-repeater-layout.kicad_sym` are parked in `hardware/pcb/_superseded/` (delete when happy;
also remove their entries from KiCad's global library tables if you had added them there).
Conventions: power symbols are the standard `power:GND` / `power:+3V3` / `power:VCC` / `power:VIN` / `power:VBAT`
everywhere; every unused pin carries an explicit no-connect flag (T096 adapter excluded — still an unwired stub).

## B-board rail names (your rename, kept)

`PANEL_RAW`→switch→`PANEL+`→RS2→`VIN` (charger input) · `BAT_RAW`→switch→`BAT+`→F1→`VBAT`→RS3→`VCC`
(battery rail) · `VCC`→RS4→`VLOAD+` (feeds LTE D-sub pin 2 and ribbon wire 2). INA3221: ch1 = panel (RS2),
ch2 = battery (RS3), ch3 = total load (RS4). Fixed during review: IN-1 rejoined to `VIN`, IN-3 label typo
(`VLOAD`→`VLOAD+`), and both STAT LEDs were reversed (anode must face the VREF/resistor side, cathode the STAT pin).

## Adapter bulk storage (verified)

Each radio adapter buffers its rail at the box entry, per the BOM: Walter = **470 µF 16 V polymer**
(Panasonic 16SEPG470M) + 10 nF (CCT-10N/100V-S); Heltec V3 = **100 µF** (AISHI EWH1CM101E11OT) + 10 nF;
RAK = 2×100 µF MLCC on the 3V3 rail. For the Walter this is sound: LTE-M bursts (~1 A class) ride on the module's
own buck input caps + 470 µF low-ESR, and the 10 Ah battery is close by — no change recommended. If field logs ever
show brown-outs during TX bursts, double C1 to 2×470 µF; don't go polymer→tantalum here.
The Walter adapter also gained a reset micro-switch (SW1, Kinghelm KH-6X6X5H-STM, C2837531) — `WALTER_nRST` to GND,
safe against the module's onboard 10 k pull-up/RC.

## JLCPCB / BOM notes (checked 2026-08-09)

Substitutions vs. previous values (all in stock at LCSC):

| Ref (B-board) | Was | Now | LCSC |
|---|---|---|---|
| R2 (MPPSET) | 36k5 (out of stock everywhere) | **36k** → V_MPP = 17.8 V (was 17.6 V; panel Vmp 18.2 V — still fine) | C4360 |
| F1 | Bourns MF-MSMF300 (not at LCSC) | RUILON SMD1812P300TF/16, 3 A hold | C702820 |
| F2 | generic 0.35 A | PTTC SMD1206P035TF/30 PTC | C438898 |
| C1/C2 (solar input) | 10u 25 V-class | Samsung CL32B106KBJNNNE **50 V** (panel Voc ≈ 21.6 V) | C138687 |
| Q1/Q2 | Si4840BDY (LCSC OOS) | VBsemi SI4840DY-T1-E3-VB (low stock, 131) — check JLCPCB's own stock of C222446 first | C558269 |
| L1 | IHLP4040DZER100M | IHLP4040DZER100M**5A** (same land pattern, Isat 8.5 A) | C511353 |
| D2/D3 | unspecified LEDs | CHG = red C84256, DONE = green C2297 | — |

Not machine-assemblable (hand-solder, source at Mouser/TME): TE 8-215570-0 ribbon headers, Amphenol L717DFE09PT PCB D-subs, FCE17-E09SM-250 bulkheads. THT JST/screw-terminal parts have LCSC numbers in the schematic if you want JLC THT assembly.
Low-LCSC-stock flags: C13585 (10 µF 1206, JLC Basic stock is separate), C17710 (470 R, same), C2903502 (20 mΩ shunt, 2.6 k pcs), C558269 (see above).
