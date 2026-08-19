# MeshCore.FleetManager

> The system of record for the TwinOak fleet of MeshCore nodes — from top-shelf
> LTE-managed repeaters down to the one someone dropped in a hedge at night.

FleetManager is where the *authoritative* picture of every node lives: its
identity, its configuration backup (keys and all), its capabilities, its history,
and — for the LTE-backed nodes — the controls to actually manage it remotely.
Where [NightCrawler](../MeshCore.NightCrawler/) discovers what the mesh *looks
like from the outside*, FleetManager holds what each node *is meant to be*, and
the gap between the two is the operational dashboard.

This directory holds the **specification**. No code yet — the design comes first,
especially because this system stores secrets and can reboot hardware over LTE,
so getting the model right up front matters.

---

## The name

Working directory name is `MeshCore.FleetManager` (clear, matches the
`MeshCore.NightCrawler` namespace convention). The **product codename** is still
open — Thomas floated *MeshyFleet*. Candidates, in the evocative spirit of
*NightCrawler* and *NoiseScope*:

| Codename | Why it fits |
|---|---|
| **Rookery** *(recommended)* | A rookery is a colony of birds nesting high up on cliffs, chimneys and rooftops — exactly where these nodes live — and a "rookery" also historically meant a warren of hidden dwellings. Perfect for a fleet ranging from proud chimney-top repeaters to nodes stashed in hedgerows. |
| **Roost** | Same imagery, shorter; "where the fleet comes home to roost." |
| **Warren** | The hidden, rogue, dropped-in-the-hedge nodes as a rabbit warren. |
| **Quartermaster** | It's fundamentally a quartermaster: inventory, provisioning, keys, kit. |
| **MeshyFleet** | Thomas's original — friendly and on-the-nose. |
| **FleetForge** | If a more "tooling" flavour is wanted. |

Nothing in the spec depends on the name; rename the directory/namespace once you
pick. The docs below use **FleetManager** generically.

## What it is

The central management plane and inventory for **all** of Thomas's MeshCore nodes:

- **Every node, regardless of tier** — the LTE-backed
  [standard-repeater-design-v2](../../README.md) builds, the plain solar/battery nodes
  with no back-channel, and even **third-party nodes he doesn't own** but wants
  in inventory because they're an essential hop to reach his own.
- **A configuration backup vault** — a restorable snapshot of each node's config,
  *including its private key and admin password*, so a bricked or stolen node can
  be rebuilt or a replacement can assume its identity.
- **A remote-management console for the LTE tier** — issue reboots, push
  firmware, pull power and environment telemetry, run a
  [NoiseScope](../firmware/noisescope-rak4630/) sweep — all over the LTE
  management plane, from a desk.
- **A historical archive** — power/environment trends, firmware history, and
  crucially a library of **NoiseScope sweeps** so a site measured today can be
  compared against a sweep from last year.
- **The operational truth** against which NightCrawler's nightly observation is
  diffed to surface drift, disappearances and rogue nodes.

## What it is not

- Not a replacement for the on-node firmware/management logic. The Walter LTE MCU
  and the repeater firmware do the actual on-device work
  ([software-goals.md](../software-goals.md)); FleetManager is the **server-side**
  counterpart that stores state, orchestrates campaigns, and presents it.
- Not the mesh transport. It never sends fleet-management traffic over LoRa — LTE
  is the management plane, LoRa is for the mesh's users (a founding principle of
  the repeater project).
- Not, in its first form, a multi-user SaaS. It's a single-operator system for
  one fleet, designed so it *could* grow multi-user later.

## The node capability tiers

The whole data model hinges on the fact that nodes differ in how much they can be
managed. FleetManager models this as a **capability set** per node, not a rigid
type, but three archetypes drive the design:

| Tier | Back-channel | What FleetManager can do |
|---|---|---|
| **LTE-managed** (standard-repeater-design-v2 with Walter) | LTE-M management plane | Everything: config backup/restore, remote OTA (A/B, health-gated), force reboot/power-cycle, live power + environment telemetry, on-demand NoiseScope sweeps, log/crash capture, LTE self-monitoring. |
| **Unmanaged / mesh-only** (solar & battery nodes, no LTE) | none — reachable only over the mesh, or physically | Inventory + config backup (captured at provisioning or during a site visit), observed state from NightCrawler/adverts/MQTT observers, manual telemetry entry, planned-vs-observed drift. No remote control. |
| **Third-party / foreign** (not owned) | none / not ours | Inventory only: identity, role, why it matters (essential hop), observed state from the mesh, contact/owner notes. No secrets, no control — we don't own it. |

A node can also be **partially** managed (e.g. an LTE node whose SIM is currently
dead, or a mesh-only node we happen to have the admin password for), so
capabilities are flags, and the UI simply shows what's available for each node.

## Document map

| Document | What's in it |
|---|---|
| [REQUIREMENTS.md](REQUIREMENTS.md) | Functional + non-functional requirements across inventory, backup, remote management, telemetry, NoiseScope archive, and third-party tracking. |
| [DATA-MODEL.md](DATA-MODEL.md) | The core entities: nodes, capability sets, config backups & secrets, telemetry series, firmware images, NoiseScope sweeps, sites. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, storage, the management-plane connection to the Walter LTE MCUs, deployment. |
| [SECURITY.md](SECURITY.md) | The serious one: storing private keys + admin passwords, encryption at rest, secret access, the threat model, and remote-reboot authorisation. |
| [INTEGRATIONS.md](INTEGRATIONS.md) | How it ties into NightCrawler, NoiseScope, the Walter/LTE plane, MQTT observers (CoreScope/MeshMapper), and the wider MeshCore ecosystem. |
| [ROADMAP.md](ROADMAP.md) | Phased build order — inventory first, control last. |

## Status

`spec / pre-implementation`. The recommended build order (see
[ROADMAP](ROADMAP.md)) starts with the **inventory + config-backup vault**, which
is valuable on day one and carries the least risk, and defers **remote control**
(reboots, OTA over LTE) until the security model and the on-node side are proven.

## Relationship to the rest of the repo

FleetManager is the server-side apex of the TwinOak repeater project. It consumes
the on-node capabilities defined in [`software-goals.md`](../software-goals.md),
stores the output of the [NoiseScope](../firmware/noisescope-rak4630/) scanner,
ingests [NightCrawler](../MeshCore.NightCrawler/)'s nightly graph, and manages the
[standard-repeater-design-v2](../../README.md) hardware fleet over its LTE management
plane.
