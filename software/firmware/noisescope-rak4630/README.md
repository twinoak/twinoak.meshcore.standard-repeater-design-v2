# NoiseScope — SX1262 spectrum/noise scanner for the RAK4630 adapter

Diagnostic firmware for the TwinOak standard-repeater LoRa module. When a site
has noise problems, flash this **in place of MeshCore**, let it stream spectrum
sweeps, RSSI histograms and burst statistics over the Walter management UART,
then flash MeshCore back. The measurement happens through the repeater's real
RX chain — antenna, coax, cavity filter, matching network, the SX1262 itself —
so unlike an external tinySA measurement it shows exactly what the radio sees.

Never transmits. RX only.

## What it measures

* **Monitor (default on boot):** continuous ~1 kHz instantaneous-RSSI sampling
  of the home channel (869.618 MHz, 62.5 kHz, SF8 by default) through the LoRa
  channel filter. Reports avg/max/min and a burst duty-cycle number every 10 s —
  this is MeshCore's "noise floor" number, but with the transients that
  MeshCore's averaging deliberately filters out.
* **Sweep:** stepped-RSSI spectrum scan (default 863–870 MHz in 25 kHz steps),
  avg + max-hold per point. The SX1262 tunes 150–960 MHz, so you can also point
  it at a suspected blocker (GSM/LTE downlink) — through the cavity filter,
  which is the honest view of what reaches the radio.
* **Hist:** Semtech's spectral-scan engine (official RAM patch, the same
  mechanism SX1261-equipped LoRaWAN gateways use). Per frequency: a histogram
  of 2048 RSSI samples taken 8.2 µs apart, 33 bins × 4 dB from −11 down to
  −139 dBm. Shows floor, burst level and duty cycle in one line — the tool for
  "idles at −115 but jumps to −100" mysteries.
* **Dwell:** park on one frequency and stream windowed statistics — burst
  periodicity and duty cycle over time.
* **CAD:** LoRa preamble-detection ratio, to tell "another LoRa network" apart
  from broadband/other interference.

See `PROTOCOL.md` for the exact NDJSON formats and the command set;
`tools/waterfall_plot.py` renders live or logged output as a waterfall.

## Relationship to MeshCore

Built on the **same pinned Adafruit nRF52 core fork and the same pinned
RadioLib commit** as MeshCore, with the same radio bring-up (DIO2 RF switch,
DIO3 TCXO 1.8 V, 140 mA current limit, RX boosted gain on), so RSSI readings
are comparable with what the repeater experiences in service.
`variants/rak4631/` and `boards/rak4631.json` are vendored from MeshCore
(Arduino LLC / Adafruit LGPL headers preserved).

MeshCore's on-flash config (identity keys, prefs in InternalFS) is **not
touched**: this firmware never writes flash, so a scanner→MeshCore round trip
preserves the repeater's identity. When flashing over SWD, use **sector/range
erase of the application area only — never chip erase** — or the bootloader,
SoftDevice and MeshCore's config filesystem go with it.

## Building

```
pio run
```

Artifacts in `.pio/build/rak4630/`:

* `firmware.hex` — for SWD flashing (Walter, J-Link, probe of choice)
* `firmware.zip` — DFU package for `adafruit-nrfutil dfu serial`
* `firmware.elf` — debugging

Offline/registry-blocked networks: every PlatformIO package can be substituted
with a local `symlink://` package (apt `gcc-arm-none-eabi` as
`toolchain-gccarmnoneeabi`, pip `scons`, apt `srecord` as `tool-sreccat`,
sparse-cloned CMSIS 5.7). The pinned framework and RadioLib come straight from
GitHub.

## Flashing workflows

**Remote (Walter, SWD):** flash `firmware.hex` over the SWD lines (D-sub pins
3/4/5), application range only. Same procedure back to MeshCore. The nRF52
watchdog (8 s) plus the bootloader living in protected flash means a bad day
ends in a reboot loop you can still flash out of — not a brick.

**Field (USB-C on the adapter):** double-tap reset or send `dfu` over any
serial console → UF2 bootloader (drag-and-drop) — or
`adafruit-nrfutil dfu serial -pkg firmware.zip -p COMx -b 115200`.

**Back to MeshCore remotely:** `dfu serial` reboots into the bootloader's
serial-only DFU if you flash over USB-CDC-less transports; over SWD just flash
the MeshCore hex.

## Using it

Connect at 115200 (Walter UART or USB). It boots into monitor mode and starts
reporting on its own — with `auto 10` (default) it also runs a full-band sweep
every 10 minutes. Typical session:

```
time 1754820000        # sync the clock (writes the RV-3028)
info
sweep                  # one full-band pass, ~7 s
hist 869.4 869.8 25    # histogram zoom on the 500 mW ERP subband
dwell 869.618 300      # 5 minutes of burst statistics on-channel
cad                    # is it LoRa?
```

Live waterfall on a laptop:

```
pip install -r tools/requirements.txt
python tools/waterfall_plot.py --port COM7 --metric max
```

Log on the Walter side and plot later:

```
python tools/waterfall_plot.py --file site.ndjson --save site.png
```

## Design notes / safety

* **Watchdog:** 8 s hardware WDT, fed from the main loop. Any hang → reset →
  boots into passive monitor mode.
* **Serial always live:** scans are chunked; command input is drained during
  blocking steps and executed between them. `stop` always works.
* **Spectral-scan patch:** ~2 kB Semtech blob (from `Lora-net/sx1302_hal`,
  shipped with RadioLib), uploaded into SX1262 RAM over SPI at the start of
  each `hist` session, purged by hardware reset at the end. Volatile by design:
  a PWR_KILL cycle, NRST pulse or reboot always returns the radio to stock.
* **Power telemetry:** reports the B-board INA3221 (panel/battery/load) in
  every heartbeat, so a scanning session doubles as a power-budget check.
  Missing peripherals (bench setup) are detected and skipped.
* **RX boosted gain** matches MeshCore's default (`boost off` to compare).
* The nRF52840's own radio is untouched (no BLE, no SoftDevice enabled).

## Interpreting results (quick reference)

* tinySA-style absolute accuracy is not the goal (±few dB); *deltas* are the
  signal: antenna vs. dummy load, filter in vs. out, quiet hour vs. busy hour.
* `stat.rssi.dc` ≫ 0 with calm `avg` = pulsed interferer (the case a single
  noise-floor number hides). Chase it with `dwell` (periodicity) then `hist`
  (level + true duty cycle), then `sweep --max` to find its frequency.
* High `cad.det` on a clean-looking channel = other LoRa traffic.
* A raised floor across the whole sweep that disappears on a dummy load =
  in-band interference or front-end compression from a strong out-of-band
  carrier; step the sweep across likely blockers (LTE800 DL 791–821,
  GSM-R/GSM900 DL 921–960) and remember you are looking through the cavity
  filter — that attenuation is part of the honest answer.
