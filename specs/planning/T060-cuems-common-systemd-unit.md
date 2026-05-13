<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# T060 — cuems-common: Systemd Unit Update for Phase H

**Issued by**: gradient-motion-engine spec 007-osc-input-transport  
**Spec ref**: FR-013  
**Target repo**: `cuems-common`  
**File**: `etc/systemd/system/cuems-gradient-motiond.service`  
**Context version**: gradient-motion-engine v0.3.0 (branch `feat/osc-input-transport`)  
**Date**: 2026-05-13

---

## Background

Phase H of gradient-motion-engine replaces the NNG bus-client inbound transport
with a local UDP OSC listener (`OscServer`, liblo-based). The C++ daemon now
accepts commands on a configurable UDP port instead of dialling a NNG bus URL.

As a result the current systemd unit shipped by `cuems-common` is stale in three
ways:

1. It passes `--nng-url tcp://controller.local:9093` to the daemon — an argument
   that no longer exists in v0.3.0 and will cause the daemon to exit with an
   error on startup.
2. It declares `Wants=avahi-daemon.service` and an `After=avahi-daemon.service`
   line — the daemon no longer performs any mDNS / Avahi lookups. This
   dependency causes unnecessary startup delays (or failures) when Avahi is
   stopped or misconfigured on a node.
3. The `ExecStart=` line does not pass `--osc-port`, so the daemon falls back to
   the compile-time default (7100). Making it explicit is the correct deployment
   practice and allows operators to override it via the environment variable
   `CUEMS_GRADIENT_OSC_PORT` without editing the unit file.

---

## Required Changes

### 1. Remove `--nng-url` argument

```diff
- ExecStart=/usr/bin/gradient-motiond --nng-url tcp://controller.local:9093
+ ExecStart=/usr/bin/gradient-motiond --osc-port ${CUEMS_GRADIENT_OSC_PORT:-7100}
```

The shell-style default expansion `${VAR:-default}` does **not** work in
`ExecStart=` directly — systemd does not invoke a shell. Use the
`EnvironmentFile=` / `Environment=` pattern instead:

```ini
Environment=CUEMS_GRADIENT_OSC_PORT=7100
ExecStart=/usr/bin/gradient-motiond --osc-port %i
```

The recommended approach is to add an `Environment=` line with the default and
let `EnvironmentFile=-/etc/cuems/gradient-motiond.env` (note the `-` prefix,
which makes it optional) override it for site-specific deployments:

```ini
Environment=CUEMS_GRADIENT_OSC_PORT=7100
EnvironmentFile=-/etc/cuems/gradient-motiond.env
ExecStart=/usr/bin/gradient-motiond \
    --midi-port "Midi Through Port-0" \
    --osc-port $CUEMS_GRADIENT_OSC_PORT \
    --log-level info
```

### 2. Remove Avahi dependency lines

Remove all of the following lines if present:

```ini
Wants=avahi-daemon.service
After=avahi-daemon.service
```

The daemon performs no hostname resolution (it binds `0.0.0.0` and receives
from localhost). These lines are vestigial from the NNG era and actively harm
deployments where Avahi is absent.

### 3. Verify existing `After=` ordering is correct

Keep any `After=network.target` or `After=sound.target` ordering that was
already present — those are unrelated to this change.

---

## Expected Final Unit (reference)

```ini
[Unit]
Description=CUEMS Gradient Motion Engine daemon
After=network.target sound.target
PartOf=cuems-node-engine.service

[Service]
Type=simple
Environment=CUEMS_GRADIENT_OSC_PORT=7100
EnvironmentFile=-/etc/cuems/gradient-motiond.env
ExecStart=/usr/bin/gradient-motiond \
    --midi-port "Midi Through Port-0" \
    --osc-port $CUEMS_GRADIENT_OSC_PORT \
    --log-level info
Restart=on-failure
RestartSec=2s
StandardOutput=journal
StandardError=journal
SyslogIdentifier=Cuems:GradientEngine

[Install]
WantedBy=cuems-node-engine.service
```

> **Note**: The exact `PartOf=` / `WantedBy=` lines depend on how `cuems-common`
> currently integrates gradient-motiond into the service dependency graph. Preserve
> whatever is there; only modify what is listed in §Required Changes.

---

## Verification Steps

After landing the change on a node with v0.3.0 of gradient-motion-engine
installed:

```bash
sudo systemctl daemon-reload
sudo systemctl cat cuems-gradient-motiond
# Confirm: no --nng-url, no Wants=avahi-daemon, ExecStart has --osc-port

sudo systemctl restart cuems-gradient-motiond
journalctl -u cuems-gradient-motiond -n 30
# Confirm: daemon starts, logs "OSC port : 7100", no error about unknown argument

ss -ulnp | grep 7100
# Confirm: daemon is listening on 0.0.0.0:7100
# Note: liblo 0.32 binds to 0.0.0.0 rather than 127.0.0.1 (known limitation —
# nftables provides the actual loopback restriction in production)
```

With Avahi stopped:

```bash
sudo systemctl stop avahi-daemon
sudo systemctl restart cuems-gradient-motiond
journalctl -u cuems-gradient-motiond -n 30
# Confirm: daemon comes up cleanly with no avahi-related errors (spec FR-013 / SC-003)
```

---

## Development Notes

- The `--osc-port` flag resolution order in v0.3.0:
  1. `--osc-port` CLI argument  
  2. `CUEMS_GRADIENT_OSC_PORT` environment variable  
  3. Compile-time default: **7100**
- The `--node-name` flag defaults to the system hostname via `gethostname()`.
  If the CUEMS node needs a specific name for the OSC `node_name` filter (e.g.
  `node-002`), pass `--node-name node-002` in `ExecStart=`. All incoming OSC
  messages with a different `node_name` field are silently dropped.
- The `--midi-port` default is `"Midi Through Port-0"`. If the node's virtual
  MIDI port has a different name, override it here.
- The `EnvironmentFile=-/etc/cuems/gradient-motiond.env` pattern (with the
  leading `-`) is the recommended way to allow per-node overrides without
  editing the unit file directly. The file is optional — if absent, systemd
  ignores it silently.
- This change has no effect on any other CUEMS service. The daemon is a pure
  consumer: it receives commands over UDP OSC and emits OSC to downstream
  players; it does not expose any socket that other services connect to.

---

## Related Spec Artifacts (gradient-motion-engine)

| Artifact | Location |
|---|---|
| Spec FR-013 | `specs/007-osc-input-transport/spec.md` |
| Plan (daemon CLI args) | `specs/007-osc-input-transport/plan.md` — §ConfigurationManager |
| ConfigurationManager.h | `daemon/config/ConfigurationManager.h` |
| OSC port resolution order | `daemon/config/ConfigurationManager.h` §OSC port resolution order |
| On-hardware acceptance | See `specs/planning/T064-node-002-acceptance.md` |
