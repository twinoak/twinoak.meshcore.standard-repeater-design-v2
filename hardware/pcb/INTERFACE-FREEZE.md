# DECISION — Inter-board interfaces are FROZEN (2026-10-10)

**Decided by Thomas Eg on 2026-10-11, the morning after the first JLCPCB batch was ordered
(2026-10-10 evening) and the hand-fit parts bought.** Physical boards now exist for the interfaces
below. They do not change — not "a little", not "while we're in there".

## What is frozen

Everything a wire, header, connector or harness carries between two boards or two boxes:

| Interface | Frozen definition | Machine-checked |
|---|---|---|
| LoRa-box D-sub (conn-A J1 ↔ LoRa platform J1 via harness) | pins 1–9 + SH, per `INTERCONNECT.md` | yes |
| LTE-box D-sub (conn-B J1 ↔ LTE platform J1 via harness) | pins 1–9 + SH, per `INTERCONNECT.md` | yes |
| A↔B ribbon (conn-A J2 ↔ conn-B J5, 10-wire 1:1, TE 8-215570-0) | wires 1–10 | yes |
| Platform board J1 ↔ D-sub harness map | conductor → cup **1,6,2,7,3,8,4,9,5**, red stripe at pin 1/6 end | J1/J2–J5 nets yes; harness is a build rule |
| Platform header rows A/B/C/D ↔ adapter interface (`platform-adapter-footprint`) | 4 × 5 pins, A‖C and B‖D paralleled, pin meanings per `INTERCONNECT.md`, pad geometry 40.64 × 35.56 mm grid | yes (nets + pad geometry on both adapters and the platform) |
| Rail definitions on those pins | LoRa side pin 2 = **3V3_LORA** (regulated on conn-A); LTE side pin 2 = **VLOAD+** (raw battery); kill line **LTE pin 6, HIGH = LoRa rail off, floating = on** | nets yes; polarity is a circuit rule |
| Bus/link conventions on those pins | I2C: LoRa module is the only master, addresses 0x42 (INA3221), 0x52 (RV-3028, DNP on batch 1), 0x76 (BME280), pull-ups 1 k on the LoRa adapter only, ≤ 100 kHz · UART 115200 8N1 · SWD bit-banged ≤ ~300 kHz, nRESET open-drain | documented rule |
| Connector part choices | Amphenol FCE17-E09SM-250 bulkheads (1000 pF/line), L717DFE09PT PCB D-subs, TE 8-215570-0 ribbon headers, 2.54 mm 1×5 header rows, Walter 2×14 socket | footprint names yes |

The machine-checked part lives in **`tools/interface-freeze.json`**, generated from the as-ordered
`.kicad_pcb` files, and is verified by **`tools/check_interfaces.py`** (pad → net for every interface
connector on all five boards, plus connector footprint name and pad geometry).

## Rules from here on

1. **Run `python3 tools/check_interfaces.py` from `hardware/pcb/` before every export and every
   commit that touches a `.kicad_pcb`.** It must print `PASS`. A `FAIL` is a design error unless rule 3
   applies.
2. Anything *inside* a board is free to change (part values, routing, the A-board regulator, the
   charger, the RTC, firmware) as long as the connector pins keep their meaning, polarity and rails.
3. Changing an interface is a **new revision of the interface, not an edit**: it needs a dated
   decision entry in this file stating what changes, which boards become incompatible, and how
   existing boards/harnesses are handled (adapter, rework, scrap). Only then is the freeze file
   regenerated (`--write`) and `INTERCONNECT.md` updated — and the freeze file's `_comment` gets the
   new date.
4. New boards that plug into these interfaces (the dual-RAK adapter, a Heltec/T096 adapter, a
   cheaper-MCU LTE adapter) **conform to the frozen side**; they do not pull the interface toward
   themselves. Add their interface connector to `BOARDS` in the checker when they are laid out.
5. Firmware treats the pin meanings in `INTERCONNECT.md` as fixed constants — D-sub/ribbon pin
   numbers, the J4/J5 Serial1 default, the kill polarity, the I2C addresses.

## Why

The interfaces are the product: platform boards, harnesses, bulkheads and adapters are only
interchangeable because every one of them agrees on these pins. One "small" change strands every
board already built, every harness already crimped and every spare on the shelf — the exact
forklift-upgrade problem this design exists to avoid (README, goal 4).

## Decision log

- **2026-10-10** — Freeze established on the as-ordered files (platform v1.1 J1 interleave map,
  conn-A 1.1 + BME280, conn-B 1.1, RAK4630 adapter, Walter adapter). `tools/interface-freeze.json`
  generated; checker passes on all five boards.
