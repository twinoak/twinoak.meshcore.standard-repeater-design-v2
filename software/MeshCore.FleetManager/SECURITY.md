# FleetManager — Security model

FleetManager stores the **private identity keys and admin passwords** of the
whole fleet, and — for the LTE tier — can **reboot hardware and push firmware**
over the network. That combination makes this the highest-stakes component in the
project. This document is the design's centre of gravity, not an appendix.

Two questions drive everything here:

1. **Confidentiality** — how are the secrets (private keys, admin/guest
   passwords, SIM identifiers) protected at rest and in use?
2. **Authorisation & integrity** — how do we ensure only the operator can trigger
   a reboot/OTA, and that every such action is attributable and auditable?

---

## 1. Threat model

Who and what we're defending against, roughly in priority order:

- **T1 — Datastore theft.** Someone gets a copy of the database / disk / backup.
  If secrets are plaintext, the *entire fleet's identities and admin access* are
  compromised at once — including the ability to impersonate nodes on the mesh.
  This is the dominant threat and the reason for encryption at rest.
- **T2 — Server compromise (at rest / cold).** The host is powered off or the
  app isn't running; attacker has the files. Same as T1.
- **T3 — Server compromise (hot / running).** Attacker gets code execution while
  the app is running with the vault unlocked. Hardest to fully defend; goal is to
  limit blast radius and ensure everything is audited.
- **T4 — Unauthorised control action.** Someone who can reach the API/UI triggers
  a reboot or a bad OTA and disrupts the fleet.
- **T5 — Malicious/mistaken OTA.** A wrong or hostile firmware image bricks nodes.
  Mitigated primarily by the *on-node* A/B + health-gated rollback, but
  FleetManager must not be the weak link that pushes an unverified image.
- **T6 — Insider/operator error.** The legitimate operator does something
  destructive; audit + confirmation + reversibility are the mitigations.
- **T7 — Third-party data over-collection.** Holding secrets we shouldn't for
  nodes we don't own. Mitigated by policy: **no secrets for third-party nodes,
  ever** (enforced in the model).

Explicitly *lower* priority for v1: multi-user privilege separation (single
operator), and network-level attacker-in-the-middle on the LTE plane (addressed,
but the LTE APN + TLS to the node broker is the first line, not FleetManager's
job to reinvent).

## 2. Secret isolation

The single most important structural decision, already reflected in the
[data model](DATA-MODEL.md): **secrets live in a separate vault, referenced by
pointer from operational records, never inlined.**

- Operational data (inventory, telemetry, sweeps, config *non-secret* fields) is
  ordinary data — most of the system never touches a secret at all.
- The **SecretBundle** vault is the only encrypted-at-rest, access-gated store.
  Config backups hold a `secretBundleRef`, not the key.
- This keeps the sensitive surface small and lets the vault be hardened,
  audited, and backed up on its own terms.

## 3. Encryption at rest (T1, T2)

- **Envelope encryption.** Each SecretBundle is encrypted with a data key;
  data keys are wrapped by a master key. Authenticated encryption
  (AES-256-GCM or equivalent) so tampering is detectable.
- **The master key is not stored next to the data.** Options, best-first for the
  deployment context:
  1. **OS/hardware keystore** — Windows DPAPI/CNG or a TPM on the host box, or a
     cloud KMS if hosted on a VPS with one. The DB copy alone is useless without
     the host's key.
  2. **Operator passphrase** — the vault is unlocked at service start (or per
     sensitive operation) with a passphrase the operator supplies; the derived
     key lives only in memory. Strong against T1/T2, at the cost of the service
     not being able to auto-start fully headless.
  3. **External secrets manager** (HashiCorp Vault / cloud secret store) if the
     deployment justifies it.
- **Recommended for the single-operator home-lab deployment:** OS/TPM-backed
  master key for auto-start convenience, **plus** an operator-passphrase gate on
  *reading* secrets and on *control actions* (defence in depth: cold theft needs
  the host key; using a secret needs the operator).
- **.NET note.** Prefer the platform Data Protection APIs / a KMS over
  hand-rolled crypto. `Microsoft.AspNetCore.DataProtection` with a properly
  protected key ring, or a KMS-backed provider, over bespoke AES code.

## 4. Secrets in use (T3)

- Secrets are decrypted **only** at the moment of use (a restore, a login to push
  config, displaying a key the operator explicitly requested) and held in memory
  as briefly as possible.
- Reads of secrets are **explicit and audited** — there is no bulk "show all
  keys" affordance; each access names a node and writes a `secret.access`
  [NodeEvent](DATA-MODEL.md#9-nodeevent-audit--lifecycle--control) (FR-39).
- The UI never renders a private key or password incidentally; revealing one is a
  deliberate, logged action behind a re-authentication/passphrase step.
- API responses never include secret fields unless the endpoint is the dedicated,
  audited secret-retrieval endpoint.

## 5. Authorisation of control actions (T4, T6)

- Every state-changing management action (reboot, power-cycle, OTA, config
  restore, key rotation) requires **authentication** and writes a `NodeEvent`
  recording actor, target, reason and result.
- **Confirmation & guarding** for destructive actions: reboot/OTA on a
  production node requires explicit confirmation; a "reason" is recorded; and
  bulk actions across many nodes are rate-limited and require a stronger
  confirmation.
- **Reversibility as a design principle.** Rely on the on-node guarantees: OTA is
  A/B with health-gated auto-rollback, reboots are recoverable (bootloader in
  protected flash, hardware watchdog), power-kill returns the radio to a known
  state. FleetManager should refuse to push an OTA image that isn't marked as
  passing whatever verification the firmware library records (T5).
- **Scheduled/conditional actions** (auto power-cycle an unresponsive node) run
  under a named system actor, are bounded (won't loop-reboot a node), and are
  audited identically.

## 6. Third-party nodes (T7)

- The model **forbids holding secrets for third-party nodes** (`ownership:
  third-party` ⇒ `secretsHeld: false`, no SecretBundle). Enforced at write time,
  not just by convention.
- Third-party records hold identity, role, why-they-matter and observed state
  only. If a third-party node later becomes owned, it's re-classified and secrets
  can then be captured.

## 7. Transport & the LTE management plane

- FleetManager reaches LTE nodes over IP, not LoRa. The channel to the Walter
  MCUs (direct, or via a broker/MQTT — see [ARCHITECTURE](ARCHITECTURE.md)) must
  be **authenticated and encrypted** (TLS / mutual auth). A rebooted-by-anyone
  endpoint is unacceptable.
- The LTE providers in use (NexCon, Lebara) and their APN don't by themselves
  provide application auth — FleetManager and the node agent authenticate each
  other at the application layer regardless of the bearer.
- Node→server telemetry and server→node commands are separate concerns: telemetry
  can be lower-trust (it's just data, and cross-checked against the crawl), but
  **commands are high-trust** and must be signed/authenticated so a node only
  acts on genuine FleetManager instructions.

## 8. FleetManager's own backups (NFR-2)

The datastore is the crown jewels — losing it loses the fleet's identities and
history; leaking a *backup* is as bad as leaking the live DB (T1). So:

- The vault stays encrypted **in backups** (back up the ciphertext + a
  separately-managed way to recover the master key; never back them up together
  in a way that defeats the separation).
- Operational data can be backed up normally.
- Test restore periodically — an un-restorable backup of the crown jewels is a
  latent catastrophe.

## 9. Audit & the NIS 2 angle (NFR-6)

The project already brushes NIS 2 (a utility company is interested in MeshCore as
a fallback comms platform). A complete, tamper-evident audit trail of node
config, secret access and control actions is exactly the kind of evidence such
scrutiny wants. Designing the audit log as append-only and attributable from day
one turns a compliance chore into a latent asset — without committing to any
formal certification now.

## 10. Security requirements checklist

- [ ] Secrets stored only in the isolated, encrypted vault; referenced by pointer
      elsewhere. (T1, T2)
- [ ] Envelope encryption with an authenticated cipher; master key not colocated
      with data. (T1, T2)
- [ ] Operator gate (passphrase/re-auth) on reading secrets and on control
      actions. (T3, T4)
- [ ] Every secret access and every control action writes an append-only audit
      event. (T3, T4, T6, NFR-6)
- [ ] Destructive actions require confirmation + a recorded reason; bulk actions
      extra-guarded. (T6)
- [ ] Refuse to push unverified OTA images; rely on on-node A/B + health rollback.
      (T5)
- [ ] No secrets held for third-party nodes, enforced in the model. (T7)
- [ ] Authenticated + encrypted, mutually-authenticated LTE command channel. (T4)
- [ ] Encrypted vault backups with master-key recovery kept separate; tested
      restores. (T1, NFR-2)
- [ ] Prefer platform crypto/KMS over bespoke implementations.
