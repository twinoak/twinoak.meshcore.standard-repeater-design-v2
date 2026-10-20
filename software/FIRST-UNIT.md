# The first unit — from a box of parts to an update over LTE

*Written 2026-10-20, the day the Walter was first powered in the real unit. The run order across the
five repositories, with every step that is a person's marked **(Thomas)**. The end of it: a unit
registered in meshmaster.dk Walter first, its radio given its base and its first image by the
Walter, and the radio updated over LTE from meshmaster.dk.*

What each repository's piles do is in their own `tasks/`; this page is only the order and the
hand-offs. One driver per repository at a time; different repositories may run side by side,
**except the two benches** (the Walter's phase 6 and the repeater's 3.4 task 02), which drive the same
radio and share a lock.

## 0. Before anything (Thomas, today)

1. **Commit this repository** (the spec: `S-5a`, `FIRMWARE-MANIFEST.md` §4a and its fixtures,
   proposal 04 accepted; `software/firmware/walter-swd-bench/`; this page; the bring-up docs) and push.
2. **Copy the spec into both firmwares** and commit there (*spec refreshed: S-5a, the base*):

   From this repository's root (every command on this page is, unless it says `cd`):

   ```powershell
   python software\spec\tools\specsync.py copy ..\twinoak.meshcore.repeater-firmware
   python software\spec\tools\specsync.py copy ..\twinoak.meshcore.walter-firmware
   ```

   Do it after step 1, so each copy's `MANIFEST.txt` names a commit that exists (with uncommitted
   changes it says *plus uncommitted changes*, which a reviewer would rightly refuse).
3. **The release key** (`SYSTEM.md` §10.1 — nothing accepts an update until it exists):

   ```powershell
   python software\spec\tools\manifestref.py keygen     # needs: pip install cryptography
   ```

   - `private` → `gh secret set RELEASE_SIGNING_KEY --repo twinoak/twinoak.meshcore.repeater-firmware`
     and the same for `twinoak/twinoak.meshcore.walter-firmware` (`gh` prompts for the value: it never
     touches your shell history). An offline copy if you want one; nowhere in a checkout, a chat or
     a note that gets pasted. Clear the terminal afterwards.
   - `public` → `keys/release.pub` in the repeater firmware, `keys/release-keys.txt` in the Walter
     firmware (each `keys/README.md` gives the line's format), and meshmaster.dk's
     `/admin/firmware-keys` (behind a fresh second factor).
4. **The exit-node firmware** — its task 24 waited for meshmaster.dk's 131, which has landed:

   ```powershell
   cd ..\twinoak.meshmaster.exitnode-firmware
   .\tasks\run-all.ps1 -Phase '1.6' -From 24
   git tag v0.3.0; git push origin v0.3.0        # after 24 has merged, not before
   ```

5. **The Walter's phase 4, task 01** stopped for two person's things (`tasks/phase-4/logs/STOPPED-01.md`):
   accept the checked copy of `agent-core` (or make the exit-node repository public), and set
   `AGENT_CORE_READ_TOKEN` (`gh secret set AGENT_CORE_READ_TOKEN --repo
   twinoak/twinoak.meshcore.walter-firmware`, a fine-grained token with *Contents: read* on the
   exit-node repository only, short expiry); run the *upstream* workflow once as the stop file says
   and close what it opens. Its follow-ups now tell 01 to pin **v0.3.0** (step 4).

## 1. The unattended runs, no hardware needed

These four can run at the same time — they are four repositories:

| Repository | Run | What it gives the next step |
|---|---|---|
| meshmaster.dk | `.\tasks\run-all.ps1 -Phase '4.16'`, then deploy (*release* workflow, by hand) | a unit registered before its repeater, linked in one click (132); releases with a base accepted (133); the guest door (134) |
| repeater firmware | `.\tasks\run-all.ps1 -Phase '3.4' -To 01` | the RAK4630 base vendored, pinned and carried in every release |
| Walter firmware | `.\tasks\run-all.ps1 -Phase 4 -From 01`, then `-Phase 5` | the agent (phase 4); the base in the store, the door, `rak provision` (phase 5) |
| exit-node firmware | (step 0.4) | `agent-core` v0.3.0 |

## 2. The bench (Thomas, then the Walter's phase 6)

1. **A dummy load** on the LoRa port (an SMA 50 Ω terminator through the pigtail): a session puts a
   repeater image on the radio only on a dummy load — a test repeater on an antenna is a stranger on
   the Danish mesh.
2. **A local release of the repeater firmware** (its `docs/release.md`, *A rehearsal*, run locally):
   signed with the test key, it is what the Walter's bench gives the radio.
3. **The Walter's first flash** — over the Walter's own USB-C, in its socket in the LTE box with the
   box open: no need to take it out first. The Walter adapter's LM66100 keeps the USB's 5 V off the
   battery rail (`hardware/pcb/BRING-UP.md` step 6.3), so the unit can be powered as in the field
   while the cable is in. **By hand**, before any setting is typed (it erases them): `esptool …
   write-flash 0x0 out/images/twinoak-walter-factory.bin` (the Walter firmware's README). After it,
   images go on without touching the settings (`bench flash`, the Walter's own update), and
   `bench.local.json` says `"walter": "keep"` from the start. **This is the only flash that needs the
   cable for the unit's whole life**: after it, the Walter updates itself over LTE (`S-6`), and the
   radio is updated by the Walter.
4. **meshmaster.dk: New exit node** — transport `native`, **no repeater**. The token is shown once.
5. **On the Walter's console** (over the same cable, typed by you, never by a session): the SIM in,
   then `set lte.apn …`, `set lte.pin …`, `set server.url https://meshmaster.dk`,
   `set server.token mma_…` (132 makes the token page show these lines).
6. **`bench.local.json`** in the Walter firmware (its phase 6 task 01 commits `bench.example.json`):
   the Walter's USB serial number, `"walter": "keep"`, `"radio": "disposable"` (the radio has nothing
   to lose yet: it is RUI3), `"rf": "dummy-load"`, `"sim": true`, `"server": true`. Leave `rak_usb`
   out while the radio is on RUI3 (its USB serial is not the radio's ID); add it after phase 6 task 03,
   as the 16 hex digits `rak swd id` prints, in upper case.
7. **Run it:** `.\tasks\run-all.ps1 -Phase 6` in the Walter firmware. Its tasks: the tool; the first
   walk (SWD through the D-subs, `INFO.VARIANT`, the clock's ceiling); **the radio gets its base**
   (`rak provision` on the factory RUI3 radio, the first image, the link, the deliberate bad image
   rolled back); **the unit checks in** with meshmaster.dk, carrying the radio's key.
8. **Stop here if phase 6 task 03 reported the radio's port *closed* after the reset** (a revision 3
   part whose bootloader does not open it): the Walter cannot reach that radio over SWD after any
   reset, so §3's update over LTE cannot work until the bootloader is one built with Nordic's MDK
   8.40.2 or later (the repeater firmware's pile 3.4 task 01 report says which it vendored). Decide
   that first.
9. Then, in the repeater firmware: its own `bench.local.json` (its `bench.example.json`: the RAK's
   USB serial, `"rf": "dummy-load"`, `"identity": "keep"`), **the LTE box unplugged from connector B's
   J1** for the run (the Walter's supervisor must not touch the radio while the walks flash it over
   USB; the radio's power does not depend on the Walter), and `.\tasks\run-all.ps1 -Phase '3.4' -From
   02`. Plug the LTE box back afterwards.

*The first radio, before all this:* if you want it provisioned today, the Walter-side bench tool in
`software/firmware/walter-swd-bench/` does it now (its README; the RAK's hex in its `images/`). Phase
6 task 03 then provisions it again, which `"radio": "disposable"` allows.

## 3. The last part: an update over LTE (Thomas, on meshmaster.dk)

1. **The repeater**: the exit node's page shows the radio's key and **Register this unit's repeater**
   (pile 4.16, task 132). Click it, keep stage *built*, save: the two are linked at once.
2. **Real releases**: tag the repeater firmware `v0.1.0` and the Walter firmware `v0.1.0` (each
   `docs/release.md`); the workflows sign with the release key. Put the Walter's **release build** on
   the unit with `bench flash` (it keeps the settings; the factory image would erase them), because a
   test build trusts only the test key.
3. **Import** both releases on `/firmware` (the catalogue wants both products when the link's major
   is shared); mark the repeater release **canary** for this unit.
4. **State**: from the unit's firmware page, read its state (`twinoak.fw.state`) and let it check in
   once — without a state the offer has no slot to name.
5. **Offer** (reason, second factor) → at the next check-in the unit downloads the image in ranges,
   checks the signature and stores it as the candidate (`stored`).
6. **Apply** (reason, acknowledgement, second factor) → at the next check-in: flash over SWD, health
   check, `committed`. **That is the radio flashed over LTE.**

If the Walter refuses its stored golden image after the switch to the release build (it was accepted
under the test key), do not leave it so: push the real release's image **as a candidate** over the
cable (`walterctl.py push --as candidate`) and `rak update` — a committed candidate is promoted and
replaces golden (a golden image is never erased or overwritten directly). A unit should never wait in
the field with a golden image its own build does not trust.

## 4. Afterwards (the loose ends)

- Close the box only after §3 has committed once: until then the cable is the way back.
- Write `INFO.VARIANT`, the SWD ceiling and the port-after-reset line into `PROJECT-KNOWLEDGE.md`
  (the Walter's phase 6 leaves them in its reports and *What to bring back*).
- Revoke `AGENT_CORE_READ_TOKEN` when it is no longer needed, or let it expire.
- Delete the stale task branches each review lists (Walter p3-03/05/06; repeater p3.1-01, p3.2-01,
  p3.3-03; exit-node `task/p1.1-08-the-host-harness`; meshmaster.dk p4.14-125…p4.15-131).
