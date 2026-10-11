# Working in this repo

Read `PROJECT-KNOWLEDGE.md` first — it is the map of the project, its status and open items.
Then, for anything touching wiring or pins, `hardware/pcb/INTERCONNECT.md` (single source of truth;
update it first when wiring changes). Shunt/charger behaviour: `hardware/pcb/CURRENT-SENSE.md`.

**Interface freeze (2026-10-10):** the inter-board interfaces — D-sub pinouts, A↔B ribbon, platform J1
map, adapter interface rows, rails and polarities on those pins — are frozen because hardware is in
production. Never change them as part of another task. Run `python3 tools/check_interfaces.py` from
`hardware/pcb/` before any export or commit that touches a `.kicad_pcb`; it must PASS. A change is a new
interface revision and goes through `hardware/pcb/INTERFACE-FREEZE.md` first.

Conventions:
- KiCad 10. Custom symbols/footprints only in `hardware/pcb/lib/`; every project references them
  via `${KIPRJMOD}/../lib/...` in its `fp-lib-table` / `sym-lib-table` — fix those if a folder moves.
- Only `*/production/` (Fabrication Toolkit) exports are current; `*/outputs/` are stale July gerbers.
- Source files are mostly CRLF; keep each file's existing line endings when editing.
- Deprecated and removed: MeshCore.FleetManager and MeshCore.NightCrawler (management lives in the
  separate MeshMaster project). Don't recreate them here.
- The repeater will run custom MeshCore-based firmware; stock-MeshCore compatibility is not a constraint.
