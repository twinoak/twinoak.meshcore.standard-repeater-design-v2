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
| `--tier <core\|extended>` | `core` | Which per-node query plan to run. `extended` adds stats/telemetry/trace. |
| `--output <path>` | `mesh-graph.json` | Graph output file. |
| `--continue` | off | Resume a prior `incomplete` run's frontier instead of reseeding. |
| `--refresh-older-than <dur>` | *(none)* | Only re-query nodes whose data is older than e.g. `48h`. |
| `--include-contacts` | on | Seed from the companion's known contacts as well as its neighbours. FR-7. |
| `--dry-run` | off | Plan only: print seed set + request-budget estimate, send nothing. FR-35. |
| `--verbose` | off | Frame-level logging. FR-33. |
| `--config <path>` | `appsettings.json` | Path to a config file. |

Passwords are **not** passed as flags (they'd leak into shell history / process
lists). They come from the config file or environment. See §3.

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
  "auth": {
    // see §3 — secrets belong in env/user-secrets, not here in the repo
    "defaultPasswordEnv": "NIGHTCRAWLER_DEFAULT_PW",
    "perNode": {
      // "a1b2c3d4…": { "passwordEnv": "NIGHTCRAWLER_PW_GRENAA" }
    }
  },
  "logging": { "level": "Information" }
}
```

## 3. Secrets / passwords

Login passwords for repeaters are sensitive and must not sit in a repo-committed
file or in shell history. NightCrawler resolves them, in order:

1. **Per-node override** — an env var named by `auth.perNode.<key>.passwordEnv`.
2. **Default/shared password** — the env var named by `auth.defaultPasswordEnv`
   (`NIGHTCRAWLER_DEFAULT_PW`), used for any node without an override.
3. **.NET user-secrets** (dev) — for local runs, the standard
   `dotnet user-secrets` store is honoured so nothing lands on disk in the repo.

If no password resolves for a node, login is skipped and the node is crawled
anonymously (recording whatever is readable without auth), then marked
`auth-failed`/`partial` (FR-17). **No password is ever written to the output
graph** (FR-18).

> The fleet manager is the intended long-term home for per-node credentials.
> Once it exists, NightCrawler can fetch the credentials it needs from the fleet
> manager (over a local, authenticated channel) instead of from env vars — see
> [INTEGRATIONS](../MeshCore.FleetManager/INTEGRATIONS.md). For the PoC, env
> vars / user-secrets are the mechanism.

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
