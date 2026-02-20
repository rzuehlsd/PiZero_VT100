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
	- B4) WLAN mode integration notes
	- B5) Planned clean mode separation
	- B6) Validation workflow after config-related changes

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

Modern setup save/apply behavior (current implementation):

- Applied immediately on `Enter`: `text_color`, `background_color`, `font_selection`, `cursor_type`, `cursor_blinking`, `vt52_mode`, `smooth_scroll`, `buzzer_volume`, `switch_txrx`.
- Persisted and used by runtime logic without dedicated re-init: `line_ending`, `key_click`, `key_auto_repeat`, `wrap_around`, `margin_bell`.
- Persisted and applied on subsystem init/reconnect/reboot: `baud_rate`, `serial_bits`, `serial_parity`, `flow_control`, `repeat_delay_ms`, `repeat_rate_cps`, `log_output`, `log_filename`, `wlan_host_autostart`.

Local mode (`F10`) behavior:

- ON: keyboard input is looped back directly to the renderer.
- OFF: keyboard input is routed to host output (UART/TCP host mode path).
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
13. `buzzer_volume` (0..100)
14. `key_click` (0/1)
15. `key_auto_repeat` (0/1)
16. `smooth_scroll` (0/1)
17. `wrap_around` (0/1)
18. `repeat_delay_ms` (250..1000)
19. `repeat_rate_cps` (2..20)
20. `switch_txrx` (0/1)
21. `margin_bell` (0/1)
22. `wlan_host_autostart` (0/1)
23. `log_output` (0..7; 0=none, 1=screen, 2=file, 3=wlan, 4=screen+file, 5=screen+wlan, 6=file+wlan, 7=screen+file+wlan)
24. `log_filename` (string, max 63 chars)

### A4) WLAN usage (operator level)

Endpoint: `telnet <ip-or-hostname> 2323`

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
- No prompt is inserted while host mode is active.
- Incoming log lines in log mode are rendered on a fresh line and the prompt is restored afterward.
- Pressing Enter on an empty command line emits a clean newline and re-shows the prompt.

When host mode is on, keyboard TX and TCP RX are used as terminal host traffic.

Host-mode session end:

- Host mode is a dedicated raw session type.
- Session ends when the remote TCP client disconnects.

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
- Legacy SET-UP B maps group 1 leftmost bit (mask `0x8`, VT100 “Scroll”) to `smooth_scroll`.
- Legacy SET-UP B maps group 2 leftmost bit (mask `0x8`, VT100 “Bell”) to `margin_bell`.
- Legacy SET-UP B maps group 2 rightmost shown bit (mask `0x1`, VT100 “Auto XON/XOFF”) to `flow_control`.
- Legacy SET-UP B maps group 3 second bit from left (mask `0x4`, VT100 “Wraparound”) to `wrap_around`.
- Modern setup save path now calls kernel runtime apply for safe non-disruptive updates (renderer and HAL) to preserve stable keyboard/dialog handler routing.

### B4) WLAN mode integration notes

- `CTWlanLog` uses one TCP endpoint with strict per-session mode separation.
- `wlan_host_autostart=0` starts a log-mode session.
- `wlan_host_autostart=1` starts a raw host-mode session.

### B5) Planned clean mode separation

Target model:

- **Log mode**: remote diagnostics only (`help`, `status`, `echo`, `exit`) with log mirroring and command prompt.
- **Host mode**: raw stdin/stdout host bridge only, without log/status/welcome chatter in host payload path.

Phased implementation details are documented in `docs/WLAN_Mode_Separation_Plan.md`.

### B6) Validation workflow after config-related changes

1. Build `VT100`.
2. Boot with a known `VT100.txt`.
3. Change values via modern setup (F11), save with Enter.
4. Confirm rewritten file matches expected keys/order.
5. Reboot and verify values are applied.
