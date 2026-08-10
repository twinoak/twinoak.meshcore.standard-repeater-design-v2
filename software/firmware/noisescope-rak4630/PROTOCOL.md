# NoiseScope UART protocol

Line-oriented, both directions, default **115200 8N1** on the Walter management
UART (RAK4630 `Serial1`, D-sub pins 8/9). The same protocol is served on USB CDC
when a host is attached (field laptop via the adapter's USB-C).

## Framing

* **Device → Walter:** NDJSON. One JSON object per line, `\n`-terminated.
  Any line **not** starting with `{` is human-readable chatter (help text,
  prefixed `# `) and must be ignored by machine parsers.
* **Walter → device:** plain-text commands, one per line, space-separated
  arguments, `\n` or `\r` terminated. Case-insensitive command word.
* Every data line carries `seq` (monotonic per boot — detect gaps/reboots),
  `ts` (unix epoch, `0` = clock never set; re-stamp on receipt if 0) and
  `ms` (millis since boot).
* No checksum in v0.1: the link is short, push-pull, and double-filtered
  (2×1000 pF). A malformed line fails JSON parsing — drop it. Add CRC later
  if field logs ever show corruption.

## Commands

| Command | Effect |
|---|---|
| `help` | human-readable command list (`# ` lines) |
| `info` | one `info` line (fw/version/devid/reset reason/peripherals/home cfg) |
| `stat` | one `stat` line immediately (heartbeat also fires every 10 s) |
| `sweep [f0 f1 [step_khz [dwell_ms [passes]]]]` | RSSI sweep. Defaults 863 870 25 20 1. `passes 0` = repeat until `stop` |
| `hist [f0 f1 [step_khz [samples [passes]]]]` | spectral-scan histogram sweep (Semtech RAM patch). Defaults 863 870 25 2048 1 |
| `dwell <f_mhz> [secs [win_ms]]` | time-domain stats on one frequency (defaults 60 s, 1000 ms windows). Uses the home LoRa RX filter |
| `cad [f_mhz [count]]` | LoRa channel-activity detections out of `count` tries (default home freq, 100) |
| `stop` | abort any activity, return to monitor |
| `home <f_mhz> [bw_khz sf cr]` | set the monitor channel (default 869.618 62.5 8 8) |
| `boost on\|off` | SX1262 RX boosted gain (default on, matching MeshCore) |
| `auto <minutes>` | periodic unattended sweep with default params (default 10, `0` = off) |
| `time <unix_epoch>` | set clock; also written to the RV-3028 RTC when present |
| `reboot` | software reset |
| `dfu [serial]` | reboot into UF2 bootloader (or serial-only DFU with `serial`) |

Every command is answered with an `ack` line: `{"t":"ack","cmd":"sweep","ok":true,"msg":"started"}`.
Commands arriving mid-scan are executed between scan steps (nothing is lost, a
long `hist` step delays execution by tens of ms).

## Messages (device → Walter)

`boot` — once after every reset. `reset` is one of `por|pin|wdt|soft|lockup`.
```json
{"t":"boot","fw":"noisescope","ver":"0.1.0","hw":"rak4630","radio":"sx1262","reset":"por","radio_ok":true,"rtc":1,"ina":1,"ts":1754820000}
```

`stat` — heartbeat every 10 s and on request. `rssi` (present in monitor mode
only) aggregates ~1 kHz instantaneous-RSSI sampling of the **home channel
through the LoRa channel filter** — i.e. what MeshCore's receiver lives in.
`dc` = fraction of samples above `th` (threshold = previous window's min +
10 dB): a direct burst-duty-cycle number. `pwr` (when the INA3221 responds)
reports the B-board channels: panel / battery / total load.
```json
{"t":"stat","ts":1754820010,"ms":12345,"up":12,"mode":"monitor","home":869.618,
 "rssi":{"n":9900,"avg":-114.2,"max":-99.5,"min":-116.0,"dc":0.0312,"th":-106.0},
 "pwr":{"vpanel":18.10,"ipanel":120,"vbat":4.012,"ibat":-52,"vload":3.980,"iload":85}}
```

`sweep` — one line per completed pass. `avg`/`max` are dBm arrays of length
`n`; point *i* is at `f0 + i*step/1000` MHz. `rbw` is the FSK receiver filter
bandwidth used (kHz), auto-picked as the smallest ≥ 1.1×step.
```json
{"t":"sweep","seq":17,"ts":1754820100,"ms":112000,"f0":863.000,"f1":870.000,
 "step":25.0,"rbw":29.3,"dwell":20,"n":281,"unit":"dBm","avg":[-115.2,...],"max":[-108.9,...]}
```

`hist` — one line per frequency per pass. `bins[i]` counts RSSI samples in
the 4 dB slot starting at `b0 - 4*i` dBm (bin 0 = −11 dBm, bin 32 = −139 dBm);
`n` samples were taken ~8.2 µs apart (≈17 ms per line at n=2048). The
histogram preserves transients that averaging destroys: a 1% duty-cycle
burster shows up as a distinct population in a high bin.
```json
{"t":"hist","seq":44,"ts":1754820200,"ms":212000,"f":869.618,"rbw":29.3,
 "n":2048,"b0":-11,"bw_db":4,"bins":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,19,0,0,1,210,1790,25,0,0,0,0]}
```

`dwell` — one line per window. Same statistics as `stat.rssi` but at the
commanded frequency and window length.
```json
{"t":"dwell","seq":81,"ts":1754820300,"ms":312000,"f":869.618,"win":1000,
 "n":990,"avg":-113.8,"max":-100.1,"min":-115.9,"dc":0.0404,"th":-105.9}
```

`cad` — LoRa preamble detection ratio at the home modem settings. High `det`
with bursty RSSI ⇒ the interferer is another LoRa network, not broadband noise.
```json
{"t":"cad","seq":90,"ts":1754820400,"ms":412000,"f":869.618,"sf":8,"bw":62.5,"n":100,"det":17}
```

`done` — emitted when a finite activity completes: `{"t":"done","cmd":"sweep","seq":18,...}`.
`ack` / `err` — command responses and asynchronous errors (`err` carries `msg`).

## Walter → MQTT forwarding suggestion

The format is chosen so the Walter needs **no understanding of the payload**:

1. Read lines; drop anything not starting with `{`.
2. Publish verbatim to `<site>/scanner/<t>` (parse only the `t` field), QoS 0,
   e.g. `grenaa-chimney/scanner/sweep`. Retain only `boot` and the latest `stat`.
3. If `ts` is `0`, either send `time <epoch>` to the scanner (preferred, it has
   an RTC) or add a `rx_ts` field before publishing.
4. Watch `seq` for gaps (lost lines) and resets (reboot → seq restarts, plus a
   fresh `boot` line).
5. On session start: send `time`, optionally `auto 10`, done — data flows
   without further commands.

A `sweep` line at default settings is ~2.5 kB ⇒ ~0.25 s of UART time at
115200; at `auto 10` that is ~15 kB/h of LTE traffic including heartbeats.
