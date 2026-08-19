# NightCrawler — Configuration

Everything is configurable, but **nothing needs to be configured to run safely**.
The defaults (1 msg/min, depth 5) are chosen so that the bare command, pointed at
a companion, is safe against the live production mesh.

Precedence, lowest to highest: built-in defaults → `appsettings.json` →
environment variables (`NIGHTCRAWLER_*`) → command-line flags.

---

## 1. Command-line flags

```
nightcrawler --host <ip|hostname> [options]
```

| Flag | Default | Meaning |
|---|---|---|
| `--host <h>` | *(required)* | Companion node hostname/IP (TCP/WiFi companion). |
| `--port <n>` | `5000` | Companion TCP port. |
| `--depth <n>` | `5` | Maximum crawl depth (hops from the seed). FR-19. |
| `--rate <n>` | `1` | Maximum **over-the-air messages per minute**. FR-23. |
| `--burst <n>` | `1` | Token-bucket burst size. `1` = strictly no bursting. |
| `--max-nodes <n>` | *(none)* | Optional cap on nodes queried this run. FR-21. |
| `--deadline <time>` | *(none)* | Wall-clock stop, e.g. `06:00` or an ISO timestamp. FR-22. |
| `--tier <core\|extended>` | `core` | Which per-node query plan to run. `core` = scope census + neighbours + version; `extended` also adds stats/telemetry/trace. |
| `--scopes-only` | off | Run the anonymous scope census only — skip guest login, neighbours and version. Fastest way to map scopes network-wide. |
| `--guest-passwords <list>` | `,hello` | Comma-separated candidate guest passwords to try (empty item = blank password). Overrides the config list for this run. FR-16. |
| `--output <path>` | `mesh-graph.json` | Graph output file. |
| `--continue` | off | Resume a prior `incomplete` run's frontier instead of reseeding. |
| `--refresh-older-than <dur>` | *(none)* | Only re-query nodes whose data is older than e.g. `48h`. |
| `--include-contacts` | on | Seed from the companion's known contacts as well as its neighbours. FR-7. |
| `--dry-run` | off | Plan only: print seed set + request-budget estimate, send nothing. FR-35. |
| `--verbose` | off | Frame-level logging. FR-33. |
| `--config <path>` | `appsettings.json` | Path to a config file. |

Guest passwords are configured as a **candidate list** (see §3). Because v0.1 uses
**guest access only** — and guest passwords on this mesh are low-sensitivity
(typically blank or `hello`) — the list can live in the config file. A
`--guest-passwords` flag is offered for convenience; an admin password is never
accepted, by anything.

## 2. `appsettings.json`

```jsonc
{
  "companion": {
    "host": "192.168.1.50",
    "port": 5000
  },
  "crawl": {
    "maxDepth": 5,
    "ratePerMinute": 1,
    "burst": 1,
    "maxNodes": null,
    "deadline": "06:00",
    "queryTier": "core",           // core | extended
    "includeContacts": true,
    "adaptiveBackoff": true         // FR-27
  },
  "output": {
    "path": "mesh-graph.json",
    "prettyPrint": true
  },
  "guestAuth": {
    // Guest tier only — NightCrawler never holds or sends an admin password.
    // Tried in order per node; first success wins. Empty string = blank password.
    "candidatePasswords": ["", "hello"],
    "perNode": {
      // "a1b2c3d4…": { "candidatePasswords": ["", "hello", "grenaa2025"] }
    }
  },
  "logging": { "level": "Information" }
}
```

## 3. Guest passwords

NightCrawler operates at the **guest tier only** (FR-16). For each node it tries
a list of **candidate guest passwords** in order and stops at the first that logs
in; the default list is `["", "hello"]` (blank and `hello`). These are
low-sensitivity by nature — guest access is read-only and the passwords are a
shared mesh convention, not secrets — so the list lives plainly in config.

Resolution order for a node's candidate list:

1. **Per-node list** — `guestAuth.perNode.<key>.candidatePasswords`, if present.
2. **Global list** — `guestAuth.candidatePasswords` (default `["", "hello"]`).
3. **`--guest-passwords` flag** — a comma-separated override for a one-off run.

If none of the candidates work, the node is **not** skipped: its scopes and owner
were already read anonymously, so it is recorded and marked `guest-auth-failed` /
`scope-only` (FR-17) — only its neighbours and firmware version are left unknown.
**No password is ever written to the output graph** — the record stores only
which candidate *index* matched (FR-18).

> **On admin credentials:** NightCrawler deliberately has no concept of an admin
> password. Reconfiguration and admin-gated reads are the fleet manager's job,
> kept separate so the crawler is provably read-only. If a future version ever
> needs admin reads, credentials would come from the fleet manager over a local,
> authenticated channel — see
> [INTEGRATIONS](../MeshCore.FleetManager/INTEGRATIONS.md) — never from this file.

## 4. Scheduling

The PoC is a run-to-completion process; scheduling is external:

- **Linux / Raspberry Pi:** a `cron` entry or a `systemd` timer, e.g. nightly at
  23:00 with `--deadline 06:00`.
- **Windows:** Task Scheduler, nightly trigger.
- **Later:** the fleet manager can own the schedule and invoke NightCrawler (or a
  library-ified version of it) directly.

A nightly invocation might look like:

```
nightcrawler --host 192.168.1.50 --depth 5 --rate 1 --deadline 06:00 \
             --output /var/lib/nightcrawler/mesh-graph.json
```

## 5. Exit codes

| Code | Meaning |
|---|---|
| `0` | Completed cleanly (frontier empty, or stopped at a *planned* bound: depth / max-nodes / deadline). |
| `1` | Incomplete due to an error (transport lost and not recovered, fatal config error). |
| `2` | Cancelled by the operator (Ctrl-C) before a planned stop. |
| `64` | Bad configuration / usage. |

Exit codes let a scheduler or the fleet manager tell "finished the mesh" from
"ran out of night" from "something broke."
