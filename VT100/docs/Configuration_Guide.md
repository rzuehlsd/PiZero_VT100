# Configuration Guide (VT100)

This document is the single configuration reference and is split into:

- Part A: User/Operator configuration
- Part B: Admin/Developer configuration and maintenance

## Table of Contents

- Part A — User / Operator
	- A1) Files you normally edit
	- A2) Setup dialogs
	- A3) `VT100.txt` keys (persisted)
	- A4) WLAN usage (operator level)
- Part B — Admin / Developer
	- B1) Source of truth and update checklist
	- B2) Paths and ownership
	- B3) Setup integration notes
	- B4) WLAN current-state integration notes
	- B5) WLAN target model (approved)
	- B6) WLAN migration and implementation plan
	- B7) Validation workflow after config-related changes

## Part A — User / Operator

### A1) Files you normally edit

- `SD:/VT100.txt` — terminal runtime settings.
- `cmdline.txt` — Circle boot options (single-line key/value arguments).
- `config.txt` / `config64.txt` — Raspberry Pi firmware display and boot options.
- `SD:/wpa_supplicant.conf` — WLAN credentials.

### A2) Setup dialogs

- Legacy setup: open with `F12`.
- Modern setup: open with `F11`.
- Local mode toggle: `F10`.

Modern setup controls:

- Up / Down: select parameter
- Left / Right: change value
- Enter: save and persist to `SD:/VT100.txt`
- Esc: cancel changes

Modern setup coverage (current implementation):

- `F11` edits 23 persisted keys: `line_ending`, `baud_rate`, `serial_bits`, `serial_parity`, `cursor_type`, `cursor_blinking`, `vt_test`, `vt52_mode`, `font_selection`, `text_color`, `background_color`, `buzzer_volume`, `key_click`, `key_auto_repeat`, `smooth_scroll`, `smooth_scroll_ms`, `repeat_delay_ms`, `repeat_rate_cps`, `switch_txrx`, `wlan_host_autostart`, `host_id`, `log_output`, and `log_filename`.
- `F12` remains the edit surface for the persisted VT100-style keys `flow_control`, `wrap_around`, and `margin_bell`, plus the runtime-only Setup A tab-stop map.

Modern setup save/apply behavior (current implementation):

- Applied immediately on `Enter`: `text_color`, `background_color`, `font_selection`, `cursor_type`, `cursor_blinking`, `vt52_mode`, `smooth_scroll`, `smooth_scroll_ms`, `buzzer_volume`, `switch_txrx`.
- Persisted and used by runtime logic without dedicated re-init: `line_ending`, `key_click`, `key_auto_repeat`.
- Persisted and applied on subsystem init/reconnect/reboot: `baud_rate`, `serial_bits`, `serial_parity`, `repeat_delay_ms`, `repeat_rate_cps`, `log_output`, `log_filename`, `wlan_host_autostart`, `host_id`.
- Persisted but edited through legacy setup (`F12`): `flow_control`, `wrap_around`, `margin_bell`.
- Display restore model: opening a setup dialog saves the renderer state including shadow-screen content; closing the dialog restores that saved state instead of copying back a separate raw framebuffer snapshot.
- Overlay handoff model: opening a setup dialog aborts any active smooth-scroll animation and forces a full projector refresh before the overlay draws, so the dialog never inherits a partial incremental frame.
- Input execution model: while a setup dialog is visible, keyboard callbacks queue cooked and raw input into a small FIFO and `CTSetup::Run()` performs the actual state changes and redraws in task context.

Local mode (`F10`) behavior:

- ON: keyboard input is looped back directly to the renderer.
- OFF: keyboard input is routed to the standard host output path (UART, or outbound shell-client uplink when active).
- Not persisted in `VT100.txt` (runtime toggle only).

### A3) `VT100.txt` keys (persisted)

Persisted by `CTConfig::SaveToFile()`:

1. `line_ending` (0=LF, 1=CRLF, 2=CR)
2. `baud_rate`
3. `serial_bits` (7/8)
4. `serial_parity` (0=None, 1=Even, 2=Odd)
5. `cursor_type` (0=underline, 1=block)
6. `cursor_blinking` (0/1)
7. `vt_test` (0/1)
8. `vt52_mode` (0/1)
9. `font_selection` (1..3)
10. `flow_control` (0/1, software XON/XOFF)
11. `text_color` (0=black, 1=white, 2=amber, 3=green)
12. `background_color` (0..3)
13. `buzzer_volume` (0..80; values above 80 are clamped)
14. `key_click` (0/1)
15. `key_auto_repeat` (0/1)
16. `smooth_scroll` (0/1)
17. `smooth_scroll_ms` (10..500)
18. `wrap_around` (0/1)
19. `repeat_delay_ms` (250..1000)
20. `repeat_rate_cps` (2..20)
21. `switch_txrx` (0/1)
22. `margin_bell` (0/1)
23. `wlan_host_autostart` (0/1/2; 0=off, 1=log mode, 2=outbound shell-client mode)
24. `host_id` (string, `IPv4[:port]`, empty means prompt locally on VT100)
25. `log_output` (0..7; 0=none, 1=screen, 2=file, 3=wlan, 4=screen+file, 5=screen+wlan, 6=file+wlan, 7=screen+file+wlan)
26. `log_filename` (string, max 63 chars)

Dialog mapping note:

- Persisted keys exposed in modern setup (`F11`): items 1-9, 11-17, 19-21, 23-26.
- Persisted keys that remain on legacy setup (`F12`): item 10 `flow_control`, item 18 `wrap_around`, item 22 `margin_bell`.

### A4) WLAN usage (operator level)

Incoming log/command endpoint: `telnet <ip-or-hostname> 2323`

Waiting-screen connect hints on VT100 include:

- `telnet <ip-address> 2323` once concrete IP has been assigned
- `telnet <hostname>.local 2323` when hostname is available
- The waiting/connect message is shown once after DHCP provides a usable IP address.

Log mode commands:

- `help`
- `status`
- `echo <text>`
- `exit`

Log mode prompt:

- `>: ` is shown at the start of each command line in log mode.
- No log prompt is inserted while shell-client payload routing is active.
- Incoming log lines in log mode are separated from the prompt by spaces only (no extra CRLF inserted before a log message).
- Pressing Enter on an empty command line emits a clean newline and re-shows the prompt.

Shell-client mode (`wlan_host_autostart=2`):

- VT100 initiates an outbound raw TCP connection instead of waiting for an incoming host-bridge client.
- `host_id` is used as default target when configured; otherwise VT100 prompts locally for `IPv4[:port]`.
- Keyboard TX is sent to the remote peer and TCP RX is rendered directly on the VT100 screen.
- UART host rendering is suspended while the shell-client session is active.

Shell-client session end:

- Shell-client mode is a dedicated raw payload session type.
- Session ends when the remote side disconnects or WLAN mode is disabled.

For host-side helper scripts and character-mode client recommendations, see `../tools/README.md`.

## Part B — Admin / Developer

### B1) Source of truth and update checklist

When adding/changing a setting, update all of:

1. `CTConfig` defaults table.
2. Parser validation in `CTConfig`.
3. Getter/setter behavior and runtime clamping.
4. Serialization in `CTConfig::SaveToFile()`.
5. Setup dialog mapping (`CTSetup`) where user-editable.
6. This document.

### B2) Paths and ownership

- Config persistence path: `SD:/VT100.txt`.
- Log output file path: `SD:/<log_filename>`.
- Telnet service port: `2323`.

### B3) Setup integration notes

- `F12` raw key (`0x45`) triggers legacy setup behavior.
- `F11` raw key (`0x44`) triggers modern setup behavior.
- `F10` raw key (`0x43`) toggles runtime local mode (keyboard loopback).
- Modern setup apply path goes through `CTConfig` setters, then persistence via `SaveToFile()`.
- Setup drawing and restore now stay on the renderer's shadow-buffer-first path; setup screens are emitted through normal renderer text operations and restored from saved renderer/shadow state.
- `CTSetup::PrepareToShow()` calls `CTRenderer::AbortSmoothScrollAndForceFullRefresh()` before drawing the overlay so setup entry always starts from a stable projector state.
- Visible setup dialogs temporarily capture keyboard input before any VTTest, local-mode, UART, or shell-client routing, and shell/UART RX is ignored until the dialog closes.
- The modern setup table (`F11`) intentionally excludes the VT100-style legacy keys `flow_control`, `wrap_around`, and `margin_bell`; those remain mapped to `F12`.
- Legacy SET-UP B maps group 1 leftmost bit (mask `0x8`, VT100 “Scroll”) to `smooth_scroll`.
- Legacy SET-UP B maps group 2 leftmost bit (mask `0x8`, VT100 “Bell”) to `margin_bell`.
- Legacy SET-UP B maps group 2 rightmost shown bit (mask `0x1`, VT100 “Auto XON/XOFF”) to `flow_control`.
- Legacy SET-UP B maps group 3 second bit from left (mask `0x4`, VT100 “Wraparound”) to `wrap_around`.
- Modern setup save path now calls kernel runtime apply for safe non-disruptive updates (renderer and HAL) to preserve stable keyboard/dialog handler routing.

### B4) WLAN current-state integration notes

- `CTWlanHost` provides the current WLAN runtime integration.
- Incoming `telnet <ip> 2323` is the log/command endpoint.
- `wlan_host_autostart=0` disables WLAN remote mode.
- `wlan_host_autostart=1` keeps the incoming endpoint in log mode.
- `wlan_host_autostart=2` activates the outbound shell-client path.
- The outbound shell-client target is taken from `host_id` or prompted locally as `IPv4[:port]`.

### B5) WLAN target model (approved)

Current runtime mode model:

- **Remote Logging/Status mode**: remote diagnostics/log sink only (`help`, `status`, `echo`, `exit`) with command prompt; no interactive shell stream.
- **Remote Shell Client mode**: VT100 acts as client and initiates a remote raw TCP session to a configured host; keyboard uplink and renderer downlink belong exclusively to this mode.

Current non-goal:

- No host-server legacy mode in final mode model.

Mode separation constraints:

- Logging stream and shell stream are strictly isolated.
- No in-session mode switching between logging and shell-client traffic paths.
- Current shell-client prompting on VT100 captures `IPv4[:port]` only; host-side shell or login behavior is provided by the remote server/helpers.

Architecture-level session model and lifecycle are documented in `docs/VT100_Architecture.md` (section 8.3).

### B6) WLAN migration and implementation plan

Planned cleanup from the current compatibility key to a clearer explicit mode policy:

1. Introduce explicit policy key `wlan_mode_policy` with values:
	- `0=off`
	- `1=remote_log`
	- `2=shell_client`
2. Keep `wlan_host_autostart` as compatibility input during transition only.
3. Compatibility mapping during transition:
	- old `0` -> new `0`
	- old `1` -> new `1`
	- old `2` -> new `2` (current shell-client behavior already uses this meaning)
4. Consider whether `host_id` should remain the single persisted shell-client target or be split into explicit host/port keys later.
5. Remove compatibility wording once `wlan_mode_policy` becomes the only public configuration key.

Implementation order (approved):

1. Config/state model split
2. Shell-client state machine cleanup/documentation refresh
3. VT100 connect UI cleanup around `host_id` / prompt reuse
4. I/O routing gates and safety guards
5. Compatibility-key cleanup and docs finalization

### B7) Validation workflow after config-related changes

1. Build `VT100`.
2. Boot with a known `VT100.txt`.
3. Change values via modern setup (F11), save with Enter.
4. Confirm rewritten file matches expected keys/order.
5. Reboot and verify values are applied.
