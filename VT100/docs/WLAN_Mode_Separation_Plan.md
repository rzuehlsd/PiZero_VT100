# WLAN Log Mode / Host Mode Separation Plan

This document defines a phased plan to separate WLAN **Log Mode** and WLAN **Host Mode** cleanly while preserving operational stability and backward compatibility.

## 1) Target behavior

### 1.1 Log Mode

- Purpose: remote diagnostics and control only.
- Payload: log stream + command responses.
- Allowed commands: `help`, `status`, `echo <text>`, `exit`.
- No host bridge traffic in this mode.

### 1.2 Host Mode

- Purpose: raw VT100 host bridge only.
- Payload: keyboard TX from VT100 to host and host RX to VT100 renderer.
- No command prompt, no command parser output, no log chatter in payload channel.
- Session close is initiated by host side; VT100 returns to stable local-ready state.

## 2) Configuration model (target)

## 2.1 Existing key retained

- `wlan_host_autostart` remains supported:
  - `0` => default connect into Log Mode
  - `1` => default connect into Host Mode

## 2.2 New optional key (recommended)

- `wlan_mode_policy` (proposed):
  - `legacy-mux` (default during migration)
  - `strict-separate`

Rationale:

- Allows staged rollout without breaking existing workflows.
- Makes behavior explicit for operators and tests.

## 3) Architecture split (implementation target)

## 3.1 Session state model

Introduce explicit internal session state:

- `SessionLogMode`
- `SessionHostMode`
- `SessionClosing`

Transitions:

- Connect -> `SessionLogMode` or `SessionHostMode` according to config.
- Host escape (`Ctrl-C` / fallback) only valid in `SessionHostMode`.
- `exit` only valid in `SessionLogMode`.

## 3.2 Data-path gates

- Log mode:
  - enable logger->remote mirror.
  - disable host TX/RX forwarding.
- Host mode:
  - disable logger->remote mirror.
  - enable host TX/RX forwarding.
  - suppress command parser and prompt emission.

## 3.3 Telnet negotiation policy

- Log mode: keep telnet negotiation enabled.
- Host mode (raw client path): skip telnet negotiation to avoid control-byte artifacts.

## 4) Phased implementation plan

### Phase 0 — Baseline stabilization (done/ongoing)

- Auto-host startup is remote-silent.
- Log mirror is suppressed in host mode.
- Disconnect path recovers local VT100 responsiveness.
- Auto-host raw sessions bypass telnet negotiation.

### Phase 1 — Strict command surface in Log Mode

- Remove host-mode control commands from log-mode command parser (`host on` transitional path).
- Keep only diagnostic command set (`help/status/echo/exit`).
- Update help text and docs accordingly.

### Phase 2 — Dedicated Host Session Entry

- Rely on `wlan_host_autostart=1` (and later `wlan_mode_policy`) for host sessions.
- Keep host session semantics fully raw and deterministic.
- Ensure host exit always returns to local-ready + waiting-for-next-host lifecycle.

### Phase 3 — Config policy rollout

- Introduce `wlan_mode_policy` with default `legacy-mux`.
- Provide migration guidance and examples.
- After validation window, switch default to `strict-separate`.

### Phase 4 — Legacy deprecation

- Remove transitional behavior (manual `host on` command path).
- Simplify codepaths in `CTWlanLog` and docs/test plans.

## 5) Validation strategy per phase

1. Unit-level / targeted checks:
   - command parser behavior in Log Mode
   - host payload purity in Host Mode
2. Integration checks:
   - connect/disconnect lifecycle
   - local responsiveness after host close
3. Manual acceptance:
   - no startup control-byte artifacts
   - first host command processed immediately
   - single-action host exit (`Ctrl-C`) path

## 6) Documentation update checklist

When advancing phases, update all of:

- `README.md` (user workflow and examples)
- `docs/Configuration_Guide.md` (keys + mode semantics)
- `docs/VT100_Architecture.md` (runtime model)
- `docs/Manual_Testplan_2026-02-12_to_2026-02-14.md` (acceptance tests)
- `DAILY_CHANGELOG.md` (implementation trace)
