# First-article bring-up — power-on order for the five boards

*Written 2026-10-17 for batch 1 (ordered 2026-10-10). Pin numbers are from the as-ordered netlists and
are frozen (`INTERFACE-FREEZE.md`). Detail for the charger and the radios lives in `CURRENT-SENSE.md` §6
and `BENCH-BREADBOARD.md` §6 — this file is the order of operations.*

## The rules

1. **One board at a time, power source before loads.** Connector B makes every rail in the system, so it is
   proven on its own before anything is plugged into it. If B's charger misbehaved and put panel voltage on
   the battery rail, it would kill the Walter and the TPS63001 at once.
2. **Lab supply before real battery.** A current-limited supply makes a short cost nothing. A 10 Ah LiPo
   can push tens of amps into a short, and only the 3 A PTC F1 is in the way. The real pack goes on last.
3. **Battery side before panel side.** The battery is the system's buffer: the charger's 2 A cannot follow
   1.5 A LTE bursts on its own. Panel-only operation, with no battery, is not a supported mode. Don't use
   it as a test either.
4. **There is no reverse-battery protection.** J7 → SW1 → F1 → VBAT has no diode. A reversed pack puts
   −4 V on the MCP1700, the INA3221 and the BQ24650. **Meter every battery lead before it is plugged in.**
   Chinese LiPo pigtails are not consistent about polarity. The panel input *is* protected (D4 blocks a
   reversed panel, D5 clamps it).
5. **Antennas or 50 Ω loads on the radios before they get power:** RAK LoRa u.FL, BLE u.FL if BLE is
   enabled, Walter LTE.

## Limits, never exceed

| Where | Limit | Why |
|---|---|---|
| J7 battery input (BAT_RAW) | 0 … 4.25 V, correct polarity | 1S LiPo rail; no reverse protection |
| J2 panel input (PANEL_IN) | ≤ 22 V on the bench (panel Voc 21.6 V) | INA3221 IN± absolute max 26 V; D5 SMBJ26A stand-off 26 V |
| conn-A VBAT_RAW (ribbon pin 2) | ≤ 5.5 V | TPS63001 input range |
| RAK adapter +3V3 (row A pin 2) | 3.0 … 3.6 V | nRF52840 VDD max 3.6 V; SX1262 rails |
| Walter adapter VCC (row C pin 2) | battery range, 3.0 … 4.2 V | same rail as in the field |

## Connector pinout cheat-sheet (as ordered)

| Board / connector | Pin 1 | Pin 2 | Other |
|---|---|---|---|
| conn-B **J7** battery (JST-EH) | **GND** | **BAT_RAW (+)** | — |
| conn-B **J2** panel (MX126 screw) | **PANEL_IN (+)** | **GND** | — |
| conn-B **J6** NTC (JST-PH) | TS | GND | 10 k NTC 103AT, or a 10 k resistor on the bench |
| conn-A **J2** ribbon | GND | VBAT_RAW | 6 = LORA_PWR_KILL (HIGH = LoRa rail off) |
| conn-A **J1** D-sub (LoRa box) | GND | **3V3_LORA** (output) | 6 = SCL, 7 = SDA |
| conn-B **J1** D-sub (LTE box) | GND | **VLOAD+** (raw battery) | 6 = KILL |
| adapter interface rows | row A/C pin 1 = GND | row A/C pin 2 = rail | RAK: +3V3 on row A pin 2 · Walter: VCC on row C pin 2 |

## Equipment

- A lab supply with a current limit. Two channels make step 2 easier.
- A DMM, ideally a second one for current.
- For the charge-current test: an electronic load in CV/battery mode, **or** a 1.5 Ω 10 W resistor across
  J7 alongside the supply (see step 2c), **or** a real cell at storage charge (~3.7 V).
- A 10 k resistor or the 103AT NTC for J6. **Without it the charger never starts:** TS reads as a cold
  fault, by design.
- Pigtails made up once and metered once: a JST-EH for J7 and a ribbon/IDC for conn-A J2.
- An I2C master with pull-ups (USB-I2C adapter), for reading the INA3221 before the RAK is in the loop.
  The B-board has no pull-ups of its own.

## Step 0 — unpowered, all boards

- Visual check: rotations and polarities that the JLC preview flagged: RAK U3 (USBLC6), conn-B U1 (BQ24650),
  conn-A U4 (TPS63001), conn-A U5 BME280, diodes D1–D5 on B and the LEDs. Check that J3 USB-C is populated;
  if JLC left it off, hand-fit C720628 and clip the pegs if needed.
- **Hand-fit parts, polarity first:** Walter **C1 470 µF polymer, + to VIN side**, C2, the socket
  (USB-C THIS END silk), the RAK **J4/J5 jumpers on 1-2** (Serial1; without them the UART link is dead),
  and the conn-B connectors and switch.
- Ohmmeter on each rail to GND. Each should read high and climb as its caps charge, never a few ohms:
  conn-B VIN, VCC/VBAT, VLOAD+, the MCP1700 3.3 V output · conn-A VBAT_RAW, 3V3_LORA · RAK +3V3 ·
  Walter VCC and WALTER_VIN.
- SW1 continuity: both poles open when OFF and closed when ON (PANEL_RAW↔PANEL+, BAT_RAW↔BAT+).
- Platform boards: J1 fan-out 1,6,2,7,3,8,4,9,5 onto the harness cups; rows A‖C and B‖D paralleled;
  D-sub SH to GND on A and B.

## Step 1 — conn-B alone, battery side, lab supply on J7

1. SW1 **OFF**. Supply set to **3.70 V, limit 100 mA**, output off. Connect it to J7 (pin 2 +). Output on.
   - Expect: **~0 mA**. BAT_RAW is present at J7 and nothing past SW1 is live.
2. SW1 **ON**.
   - Expect: **under 1 mA** (INA3221 ~0.35 mA plus µA-level leakage). VCC ≈ VBAT ≈ 3.70 V. VLOAD+ at
     D-sub J1 pin 2 and ribbon J5 pin 2 ≈ 3.70 V. MCP1700 output 3.30 V.
   - **Stop if** the supply goes into current limit, or idle current is above about 5 mA.
3. Sweep the supply 3.0 → 4.2 V. VLOAD+ should follow the supply. Below about 3.4 V the 3.3 V LDO output
   follows the supply down (LDO dropout). That is expected.
4. Optional: read the INA3221 at 0x42 over your USB-I2C adapter. Ch2 bus voltage should show VBAT; the
   currents should be near zero; record the offsets.

## Step 2 — conn-B charger: lab supply on the panel input

Keep the J7 supply connected at 3.70 V, limit raised to 2.5 A. Plug the 10 k / NTC into **J6**.

> **Note from first article (2026-10-20):** a lab supply on J7 cannot sink current, so steps 2a/2d work but
> 2b (MPPT) and 2c (charge current) cannot be shown with it. You need the resistor, an e-load or a real cell.

**2a — regulation point.** Second supply on J2 (pin 1 +): **20.0 V, limit 0.6 A**. Output on.
- Expect: the CHG LED lights and VCC climbs to **4.200 V ±1 %**. The J7 supply then reads 4.2 V and
  ~0 A while still set to 3.70 V. That is normal: it can't sink current, so the charger lifts the node to
  its CV point. The charger then terminates (DONE LED). Panel-side current is a few mA once done.
- **Stop if VCC goes above 4.25 V.** That would be a charger fault, and VLOAD+ feeds the radio boxes.
- Pull the 10 k from J6 and charging should stop (TS reads as a cold fault). Put it back.

**2b — MPPT.** Lower the panel supply's current limit to **0.3 A** with the charger running. The supply
drops into CC and its voltage sags until the charger holds **VIN ≈ 17.8 V** (J2 reads ~0.4 V more,
because of D4). That is the input-voltage regulation doing its job.

**2c — charge current and thermals.** The node needs something that actually sinks current:
- An e-load in CV mode at 3.8 V on J7, **or**
- The J7 supply at 3.8 V plus a **1.5 Ω 10 W** resistor across J7. The resistor wants ~2.5 A and the
  charger supplies up to 2 A of it, **or**
- A real cell at ~3.7 V (only after 2a has passed).

Expect **2.0 A ±3 %** (40 mV across RS1) with the panel supply limit at 0.6 A or more. Run it 30 minutes;
RS1, RS3 and the spine should stay below +10 °C.

**2d — abuse (optional, limit 100 mA).** Panel reversed on J2 for a second: no current through the
charger, D5 conducts. Don't try this on the battery side, because there's no protection there.

Full charge-profile checks (termination at ~200 mA, recharge at 4.15 V, precharge below 3.10 V, the
dead-battery-dawn case) are in `CURRENT-SENSE.md` §6.

## Step 3 — conn-A alone

Feed the ribbon header J2 directly: pin 1 GND, pin 2 VBAT_RAW, from a supply at **3.7 V, limit 100 mA**.
Leave pin 6 (KILL) open.
- Expect: **3V3_LORA = 3.30 V** on D-sub J1 pin 2. Idle current well under 1 mA.
- Sweep 2.8 → 4.2 V: 3V3_LORA should hold 3.30 V throughout (buck-boost).
- Kill: put 3.3 V on ribbon pin 6 and 3V3_LORA should drop to 0 V. Remove it and the rail comes back.
- Load: 33 Ω (100 mA), then 10 Ω (330 mA), on D-sub pin 2/1. The rail should stay at 3.30 V with no
  audible or thermal drama. Look at the ripple if you have a scope.

## Step 4 — platform boards and harnesses (unpowered)

Buzz every conductor from each platform's adapter rows, through the harness and bulkhead, to the D-sub
pin on its connector board: LoRa side to conn-A J1, LTE side to conn-B J1. Expect ~1000 pF per line to the
shell on the filtered bulkheads.

## Step 5 — RAK adapter

1. Antenna on the LoRa u.FL. Bench supply **3.30 V, limit 150 mA** straight into the adapter's interface:
   row A pin 1 GND, row A pin 2 +3V3. USB alone does **not** power this board: VBUS only reaches the nRF
   USB PHY.
2. Expect ~5–15 mA idle; the LEDs blink under firmware.
3. USB-C to the laptop. **Factory RAK4630 stamps run RAK's RUI3 firmware, not the Arduino/UF2 bootloader**
   (found 2026-10-20). A COM port appears, the board idles at ~1 mA after a 3–5 mA boot burst, double-tap
   reset does nothing, and Arduino/PlatformIO/adafruit-nrfutil uploads fail. Confirm it with a terminal at
   115200, CR+LF: `AT` → `OK`, `AT+VER=?` → RUI version. RUI3's own DFU (`AT+BOOT` + Nordic nrfutil) only
   accepts RAK's RUI3 packages, so it is not a route to MeshCore or NoiseScope.
4. **One-time SWD flash of the Arduino bootloader + SoftDevice S140 6.1.1** (RAKWireless WisBlock repo,
   `bootloader/RAK4630`). Use the image that *includes* the SoftDevice: a bootloader-only hex has been
   reported to leave the board not enumerating on USB. SWD is on the adapter's interface rows: row C pin 1
   GND, pin 3 nRESET, pin 4 SWDCLK, pin 5 SWDIO. Keep the bench 3.30 V on row A pin 2 and connect the
   probe's VTref only. Probe: J-Link + nrfjprog/J-Flash, or any CMSIS-DAP (Raspberry Pi Debug Probe / Pico
   debugprobe) + pyOCD/OpenOCD. Order: read FICR **INFO.VARIANT** → if the AHB-AP refuses (APPROTECT on rev 3
   parts, `BENCH-BREADBOARD.md` caveat 6), recover/mass-erase → full erase → flash bootloader+SD → reset.
   This permanently replaces RUI3, which nothing here needs. **Every stamp needs this once.**
   - **Cheapest probe:** a Raspberry Pi Pico (any RP2040/RP2350 board) with Raspberry Pi's `debugprobe`
     firmware. Hold BOOTSEL, plug it in, copy `debugprobe_on_pico.uf2` onto the drive; it is now a CMSIS-DAP
     probe. Wiring: Pico GP2 → SWDCLK (row C pin 4), GP3 → SWDIO (row C pin 5), GND → row C pin 1. Do not
     connect the Pico's 3V3. Then `pip install pyocd`, `pyocd list`, `pyocd cmd -t nrf52840 -c "read32
     0x10000104"` (INFO.VARIANT), `pyocd erase -t nrf52840 --chip`, `pyocd flash -t nrf52840 <bootloader+S140
     hex>`. **For every unit after the first, use the D-sub fixture:** `PROVISIONING-FIXTURE.md` provisions a closed unit
     through connector B's D-sub, the Walter's own path. **No Pico? A Raspberry Pi Zero does the same job** with OpenOCD's
     GPIO bit-bang driver: wiring, config and commands are in `tools/openocd-pizero-swd.cfg` (a Pi 5 also
     works, but its GPIO sits behind the RP1 chip and needs the slower `linuxgpiod` driver).
   - **Or the Walter itself (bench tool, 2026-10-20):** `software/firmware/walter-swd-bench` runs on the
     Walter and does this whole step through the production chain on a console command (`provision yes`):
     erase, bootloader + S140, UICR, verify, one reset, about a minute. A person runs it, so S-5 holds; the production
     Walter firmware gets the same as a console command, `rak provision` (`software/spec/SYSTEM.md` S-5a, its phase 5).
     Host-tested against a simulated nRF52840, not yet on hardware: if the first run fails, a probe (above) is
     the independent reference.
5. Now double-tap reset brings up the UF2 drive. Flash NoiseScope: `adafruit-nrfutil dfu serial -pkg
   firmware.zip -p COMx -b 115200`, or convert `firmware.hex` with `uf2conv.py -f 0xADA52840` and drag it on.
   - `info`: rtc = 0 (U5 is DNP on batch 1), ina = 0 (no B-board yet).
6. Then power it from conn-A instead: platform + adapter in the LoRa box, conn-A fed as in step 3. Check
   3V3_LORA arrives at row A pin 2.

## Step 6 — Walter adapter

**Boards of 2026-10-10: rework U3 first** (`INTERCONNECT.md`, *2026-10-22 correction*). Its CE (pin 3) is on GND,
which makes the LM66100 an always-on switch: with the Walter's USB-C in, 5 V reaches the battery rail and would
charge a cell with nothing to stop it at 4.2 V. Until reworked, plug the Walter's USB-C in only with the lab supply on
the rail or with SW1 OFF — never with a cell and SW1 ON. The rework, everything off and unplugged:

- U3's column on the C3 side reads, from the C1 end: pin 1 VIN, pin 2 GND, pin 3 CE. Lift pin 3 (the one nearest
  SW1) off its pad. The pad is tied to pin 2, C3 and the pour, so it stays GND: Kapton under the lifted lead.
- 0.1–0.2 mm enamelled wire from the lifted lead to pin 6 (VOUT: the other column's pin nearest C1) or to C1's +
  lead. Insulated past pin 5 (ST, an open-drain output) and pin 4. Never power up with CE floating.
- Then steps 1–3 below; step 3 is the proof.

1. **Without the Walter in the socket:** supply **3.7 V, limit 100 mA** on row C pin 1 GND / pin 2 VCC.
   WALTER_VIN at the socket's VIN pin should read 3.7 V minus 20–90 mV (the LM66100).
2. Insert the Walter (USB-C THIS END) with the LTE antenna fitted. Raise the limit to **0.5 A** for boot.
   For LTE attach, raise it to **2 A**: the modem bursts to ~1.5 A, and a limit that is too tight shows
   up as endless re-attach.
3. With the supply live, plug the Walter's USB-C into the laptop. Supply current should fall to ~0 and
   the battery rail must **not** rise: the LM66100 blocks back-feed. (In the assembled unit the current falls only by
   the Walter's share.) WALTER_VIN reads ~5 V; with the cable out it is back at the supply minus 20–90 mV and the
   Walter runs on. *2026-10-22, unreworked board in the assembled unit: the rail rose to 5.0 V and the supply's
   current went to 0 — the fault this step exists to catch.*

## Step 7 — integration, still on the lab supply

Power the whole stack from the J7 lab supply at 3.7 V: conn-B → ribbon → conn-A → LoRa box, and conn-B →
LTE box. **Limit 300 mA** with the RAK alone; raise to **2 A** before the Walter attaches to LTE. NTC/10 k
on J6.
- NoiseScope `stat` should now show `pwr`: all three INA3221 channels, with the per-channel shunts in
  firmware since 2026-10-10. Compare against the DMM.
- Everything in `PROJECT-KNOWLEDGE.md` §9 B step 6: UART both ways, nRESET, SWD, I2C rise times, kill
  cycle, Walter power-cycle resetting the RAK. These need the Walter bring-up firmware.

## Step 8 — real battery, then real panel

1. SW1 OFF. **Meter the pack's connector** against J7 (pin 1 GND, pin 2 +). Plug it in. Measure BAT_RAW
   at J7. SW1 ON.
2. Panel last, ideally outdoors on a cloudy day first: SW1 OFF, panel on J2, SW1 ON.
3. **Field installation order is the same:** SW1 OFF → battery → panel → SW1 ON. To service: SW1 OFF
   first, and the RTC rides through (once fitted).
