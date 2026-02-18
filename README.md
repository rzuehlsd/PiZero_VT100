# VT100 Terminal Emulation on Raspberry Pi Zero W with Circle Bare Metal  Framework

## Motivation
My main motivation was that I wanted a terminal emulation for my VT100 replica (https://www.instructables.com/23-Scale-VT100-Terminal-Reproduction/) which I printed and assembled. I wanted something that could start within seconds, so a bare metal implementation on a Pi Zero seems to be the way to go. I looked into the PiGFX implementation by Filippo Bergamasco (https://github.com/fbergama/pigfx) and PiVT VT220 Emulator by Hans Hübner (https://github.com/hanshuebner/pivt). PiGFX for my taste was to much focused on reproducing vintage games on Z80 hardware or similar whereas PiVt was focused on VT220 with minimal configurability. Also the support for VT100 features like font stretching, double width fonts and double width double height fonts was missing in both implementations. 
So I decided to start my own VT100 Emulator journay and here is the result.

Below you see the original VT100 terminal (the color has turned to yellow over the years) and my 60% replica in a color that matches the original "oyster white" and some font examples:

<table>
  <tr>
    <td><img src="IMG_1088.jpeg" alt="VT100 original" width="80%"/></td>
    <td><img src="IMG_1095.jpeg" alt="60% replica" width="80%"/></td>
  </tr>
  <tr>
    <td><img src="DEC_Font_test.jpeg" alt="VT100 original" width="80%"/></td>
    <td><img src="DEC_Graphics_Font.jpeg" alt="60% replica" width="80%"/></td>
  </tr>
</table>


The current VT100 terminal firmware turns a Raspberry Pi Zero W into a self-contained ANSI/VT100-compatible console. It boots straight into the terminal without Linux, using the Circle bare-metal framework for deterministic hardware access. The design targets headless benches and retro workstations that need a physical keyboard and display front-end for serial hosts.

This project also includes a PCB design to support enhanced features such as a buzzer, an RS-232 adapter, a Mini-DIN-6 adapter for MBC2-Z80, and power distribution for the Pi Zero, MBC2, and display, plus a backplate to mount the board inside a 60% VT100 replica designed by megardi.

My aim was to replicate the behaivior of a true VT100 as close as possible (vttest will show the final result ;-) ) and be able to mimic a VT52, VT220 and my favorite the VT320 in amber, also I did not replicate the small deviations of the VT220 or VT320 font sets. I got the original font definitions for a VT100 font from the ROM data used by Lard Brinkhoff to simulate a VT100 terminal in software on a Pi.

If you want a nearly 100% VT100 terminal simulation, please go for the VT100 simulation from Lars Brinkhoff (https://github.com/larsbrinkhoff/terminal-simulator). He simulates the complete VT100 hardware with original ROMs on a Raspberry Pi. The Pi executes the ROM by an 8080 emulator and simulates other components like video display with character generator ROM, settings NVRAM, Intel 8251 USART, and a keyboard matrix scanner. 

## Regards

Many thanks to Rene Stange (https://github.com/rsta2) for the creation of the Circle bare metal framework for Raspberry Pis. Without his work this project would not have been possible.

I take my hat off to the engineers a DEC which did a terrific job implementing the VT100 based on the technologies available at that time. They squeezed all functionality I implemented on a Pi with 1 GHz frequency and 512MB into a 8080A with 2Mhz, 8kB of ROM (including the original VT100 7x10 font) and 3kB of RAM. Granted, a lot of the features I had to implement in software were done with analog circuits, but I really feel embarressed by their skills.

## License Statement 
Copyright (C) 2026 Ralf Zühlsdorff

This software and hardware design are released under the MIT License. You may use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of it, subject to including the copyright notice and permission notice in all copies or substantial portions.

## Documentation Policy

This project keeps documentation in a strict 4-document model:

- This `README.md` document provides the functional overview of the implemented software and user-facing operation guidance.
- [Architecture Overview](docs/VT100_Architecture.md) is the single technical architecture + implementation reference for developers.
- [Configuration Guide](docs/Configuration_Guide.md) contains all configuration guidance, split into user/operator and admin/developer parts.
- [Hardware Design](docs/Hardware.md) contains the KiCad design of the PCB and the OpenSCAD files for printing the backplate.

When implementation changes affect behavior, update these documents in lockstep and avoid introducing additional overlapping technical documents.

# Software Implementation

The application initialises USB keyboard input, the framebuffer, GPIO, UART, and WLAN within Circle and runs a cooperative task loop that keeps the terminal responsive even under heavy serial traffic. Classic aesthetics are preserved by rendering converted VT100 ROM fonts at 1024x768, while modern conveniences such as remote logging remain available.

## Scope and Goals

- Provide a responsive VT100/ANSI terminal experience with integrated keyboard, framebuffer, and UART handling.
- Preserve classic look-and-feel through ROM-derived fonts, colour themes, and optional CRT-style scan lines.
- Offer configurable serial, display, audio, and logging behaviour via both on-device UI and SD-card files.
- Deliver flexible diagnostics through screen, file, and WLAN logging with remote telnet mirroring.
- Keep deployment simple: copy firmware, configuration files, and optional fonts onto an SD card and power the Pi.


## Main Features

- [x] USB keyboard input with Circle keymap support for multiple languages
- [x] Configurable line endings (LF, CRLF, CR)
- [x] Separate `VT100.txt` configuration file on the SD boot partition
- [x] Circle boot configuration via `cmdline.txt` and `config.txt`
- [x] Serial UART interface to host via RS232 and Mini-DIN-6 connector
- [x] Support for TCP/IP host connections
- [x] Framebuffer renderer supporting multiple VT fonts and colour themes
  - [x] DEC VT100 ROM 8x20 with scan-line simulation
  - [x] VT100 10x20 CRT-style font derived from DEC VT100 ROM Font
  - [x] VT100 10x20 solid font derived from DEC VT100 ROM Font
  - [x] DEC VT100 Special Graphics Character Set support (via `ESC ( 0` and `ESC ( B`)
  - [x] Dynamic Double-Width / Double-Height glyph scaling for all fonts
  - [x] White, amber, and green on black simulate DEC monochrome terminals (VT100, VT220, VT320)
- [x] VT100 and ANSI escape sequence parser and renderer based on the VT-parse project
- [x] Configurable optional VT52 escape sequence support
- [x] 800 Hz buzzer with configurable volume for bell and key click feedback
- [x] Logging infrastructure with screen, file, and WLAN sinks
  - [x] Real-time debug output with formatted log messages
  - [x] WLAN debug output via telnet session
  - [x] WLAN host mode to integrate remote hosts via tcp/ip
- [x] GPIO16-controlled TX/RX swap to simulate Null Modem cables with straight DB9 cables
- [x] Configuration of system via VT100 Setup Screens A and B for supported parameters
- [x] Separate on-screen setup dialog covering all file-based configuration parameters (F11)



The following table gives an overview of implementation highlights:

| Area | Highlights |
| --- | --- |
| **Core Terminal** | ANSI/VT100 parser, ROM-derived fonts, framebuffer renderer with cursor control |
| **Input** | USB keyboard with F12 legacy setup, F11 modern setup, F10 local mode toggle, optional key click |
| **Serial** | Configurable UART baud rates, software flow control (XON/XOFF), GPIO16 TX/RX swap |
| **Display & Audio** | Runtime font switching, colour themes, buzzer tones, periodic status tasks |
| **Configuration** | SD-based `VT100.txt`, Circle `cmdline.txt`/`config.txt`, manual SD-card editing |
| **Logging** | Bitmask-controlled outputs (screen, file, WLAN), telnet console, timestamped files |
| **Networking** | WLAN bring-up with WPA supplicant, telnet banner showing `ip:2323` |
| **Deployment** | Makefile-driven build, SD card copy workflow, optional bootloader assets |

And here are the results of the internal tests which could be switched on in VT100.txt config file:

<img src="Test Summary.jpeg" alt="VT100 original" width="70%"/>

## Compile Environment Setup

### Make Targets

The following table lists the implemented Make targets available from `VT100/Makefile` (including targets inherited via `../Rules.mk`) and their purpose.

| Target | Purpose |
| --- | --- |
| `all` | Default target; builds firmware image and copies it to `VT100/bin/kernel.img`. |
| `kernel.img` | Creates/refreshes local `kernel.img` symlink to `VT100/bin/kernel.img`. |
| `clean` | Removes generated build artifacts (`build/`, intermediate objects, map/list/elf/img outputs). |
| `docs` | Generates Doxygen documentation from `VT100/Doxyfile` into `VT100/docs/doxygen/`. |
| `docs_clean` | Removes generated Doxygen output directories/files under `VT100/docs/doxygen/`. |

This project needs a **complete bare-metal Arm GNU toolchain** (compiler + target C library headers like `stdint.h`).



### macOS (tested)

Install required host tools and the full Arm toolchain:

```bash
brew install make
brew install --cask gcc-arm-embedded
```

Configure Circle to use the full toolchain path explicitly:

```bash
cd /Users/ralf/Projekte/VT100_Circle
./configure -f -p /Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-
```

Verify the compiler and header availability:

```bash
/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-gcc --version
printf '#include <stdint.h>\nint main(void){return 0;}\n' | \
    /Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-gcc -x c - -c -o /tmp/vt100_stdint_test.o
```

Build steps:

```bash
cd /Users/ralf/Projekte/VT100_Circle
./makeall
cd VT100
make -j4
```

### Important Note about Homebrew formulas

The Homebrew formula `arm-none-eabi-gcc` may be built without target headers (`--without-headers`), which can cause errors like `fatal error: stdint.h: No such file or directory`. The `gcc-arm-embedded` cask provides the complete toolchain required by this project.

## Configuration

The VT100 terminal reads configuration from `VT100.txt` on the SD boot partition and supplements it with Circle boot parameters in `cmdline.txt` and firmware settings in `config.txt`/`config64.txt`.

### Configuration File: VT100.txt

Create or edit `VT100.txt` on the boot partition. The firmware loads it on startup, and settings can be changed either by manual edits or via the on-screen setup dialogs (F11/F12). See [VT100/bin/VT100.txt](VT100/bin/VT100.txt) for the shipping template.

### Available Options

#### Configuration Summary

| Key | Allowed values | Default | Notes |
| --- | --------------- | ------- | ----- |
| `baud_rate` | 1200–921600 | 115200 | Host serial speed |
| `serial_bits` | 7/8 | 8 | UART data bits (SET-UP B Bits/Char) |
| `serial_parity` | 0–2 | 0 | UART parity (0=none, 1=even, 2=odd) |
| `background_color` | 0–3 | 0 | Palette: 0=black, 1=white, 2=amber, 3=green |
| `buzzer_volume` | 0–100 | 50 | Sets 800 Hz buzzer duty cycle (0 disables tone) |
| `cursor_blinking` | 0/1 | 0 | 1 enables blink animation |
| `cursor_type` | 0/1 | 0 | Cursor style (0=underline, 1=block) |
| `vt_test` | 0/1 | 0 | Run built-in VT test sequences |
| `font_selection` | 1–3 | 2 | Choose between 8x20, 10x20 CRT, 10x20 solid |
| `flow_control` | 0/1 | 0 | Software XON/XOFF UART flow control |
| `key_click` | 0/1 | 1 | Enables or disables key click feedback |
| `line_ending` | 0–2 | 0 | Enter key behaviour: 0=LF, 1=CRLF, 2=CR |
| `log_filename` | String (≤63 chars) | vt100.log | Used when file logging is active |
| `log_output` | 0–7 | 0 | 0=off, 1=screen, 2=file, 3=WLAN, 4=screen+file, 5=screen+WLAN, 6=file+WLAN, 7=all |
| `smooth_scroll` | 0/1 | 1 | Enables non-blocking smooth single-line scroll animation |
| `wrap_around` | 0/1 | 1 | Controls right-margin wrap (`1`) vs overwrite-at-last-column (`0`) |
| `repeat_delay_ms` | 250–1000 | 250 | Delay before auto-repeat starts |
| `repeat_rate_cps` | 2–20 | 10 | Characters per second once repeating |
| `margin_bell` | 0/1 | 0 | Rings bell 8 columns before right margin when enabled |
| `switch_txrx` | 0/1 | 0 | Drives GPIO16 high to swap wiring |
| `wlan_host_autostart` | 0/1 | 0 | `1` auto-enables TCP host bridge mode when a client connects |
| `text_color` | 0–3 | 1 | Foreground palette: 0=black, 1=white, 2=amber, 3=green |

On screen configuration can be done by using one of the VT100 Set Up Dialogs A and B which can be triggered by F12 key and in an additional extended configuration dialog that also covers parameter of VT100.txt configuration file. This Dialog is triggered by F11 key.

#### VT100 SETUP Screen A Parameter Mapping (current status)

The table below maps the current SET-UP A screen controls to firmware behavior.

| SET-UP A item | Corresponding config/runtime state | Implementation status |
| --- | --- | --- |
| Horizontal tab stops (per column) | Runtime tab-stop map via `CTConfig::SetTabStop()` / `CTConfig::IsTabStop()` | Implemented |
| Cursor movement in ruler (`Left`, `Right`, `Home`, `End`) | Tab edit cursor in SET-UP A ruler line | Implemented |
| Set/clear tab at current column (`T` / space) | Updates tab stop at current column | Implemented |
| Tab-stop persistence in `VT100.txt` | Not mapped to persisted file keys yet | Not implemented |



#### VT100 SETUP Screen B Parameter Mapping (current status)

The table below maps original VT100 SET-UP B terms to the current firmware configuration model.

| Tab group | Parameter name (VT100 docs) | Corresponding config param | Implementation status |
| --- | --- | --- | --- |
| 1 | Scroll | `smooth_scroll` (leftmost bit in group 1, mask `0x8`) | Implemented |
| 1 | Auto Repeat | `key_auto_repeat` (+ `repeat_delay_ms`, `repeat_rate_cps`) | Implemented |
| 1 | Screen | runtime-only (`screen_inverted`, not persisted) | Implemented |
| 1 | Cursor | `cursor_type` | Implemented |
| 2 | Bell | `margin_bell` (group 2 leftmost bit, mask `0x8`) | Implemented |
| 2 | Keyclick | `key_click` | Implemented |
| 2 | ANSI/VT52 | `vt52_mode` | Implemented |
| 2 | Auto XON/XOFF | `flow_control` (group 2 rightmost shown bit, mask `0x1`) | Implemented |
| 3 | US/UK # | N/A | Not implemented |
| 3 | Wraparound | `wrap_around` (group 3 second bit from left, mask `0x4`) | Implemented |
| 3 | New Line | `line_ending` | Implemented |
| 3 | Interlace | N/A | Not implemented |
| 4 | Parity Sense | `serial_parity` (1=even, 2=odd when parity enabled) | Implemented |
| 4 | Parity | `serial_parity` (0=none, 1/2=enabled) | Implemented |
| 4 | Bits/Char | `serial_bits` (7/8) | Implemented |
| 4 | Power | N/A | Not implemented |
| — | T SPEED (Transmit Speed) | `baud_rate` | Implemented |
| — | R SPEED (Receive Speed) | Follows `baud_rate` (TX speed) | Always follows transmit speed |

#### VT100 Extended Setup Dialog (F11)

Press `F11` to open the extende setup dialog. The dialog uses DEC special graphics box drawing, keeps the active terminal color/font theme, uses a centered double-width title, and shows a three-column `parameter` / `value` / `description` view for all persisted `VT100.txt` keys.

Controls:

- `Up` / `Down`: select parameter row
- `Left` / `Right`: change selected value
- `Return`: save to runtime + `VT100.txt` and exit
- `Esc`: cancel changes and exit

Runtime apply on `Return` (current firmware):

- Immediate runtime apply: renderer visuals (`text_color`, `background_color`, `font_selection`, cursor type/blink, VT52 mode, smooth scroll) and HAL settings (`buzzer_volume`, `switch_txrx`).
- Persisted and used by runtime logic without dedicated re-init: `line_ending`, `key_click`, `key_auto_repeat`, `wrap_around`, `margin_bell`.
- Persisted (applied on subsystem init / reconnect / reboot): serial framing (`baud_rate`, `serial_bits`, `serial_parity`, `flow_control`), keyboard repeat timing (`repeat_delay_ms`, `repeat_rate_cps`), logging targets (`log_output`, `log_filename`), and `wlan_host_autostart`.

### DEC Local Mode (F10)

The DEC feature to toggle the terminal between local (not connected to host) and line (connected to host) mode can be triggered by pressing `F10`.

- `ON`: keyboard input is looped directly to the renderer (local echo), and host output is not sent over UART/TCP.
- `OFF`: keyboard input follows the normal host path again.

The terminal prints a short status line (`VT100 local mode ON/OFF`) when toggled.

### Example VT100.txt

Most of the configurations can be defined in the file VT100.txt on the root volume of the boot SD. The following sections give a summary:

```ini
# VT100 Terminal Configuration File
# Place this file as VT100.txt on the SD card boot partition.
# Values below match firmware defaults.

# --- Serial communication ---
# Host serial speed in bits per second
baud_rate=115200

# UART framing:
# serial_bits: 7 or 8
# serial_parity: 0=None, 1=Even, 2=Odd
serial_bits=8
serial_parity=0

# --- Keyboard / line handling ---
# line_ending: 0=LF, 1=CRLF, 2=CR
line_ending=0

# key_auto_repeat: 0=off, 1=on
key_auto_repeat=1

# smooth_scroll: 0=off, 1=on
smooth_scroll=1

# wrap_around: 0=overwrite at last column, 1=wrap to next line
wrap_around=1

# flow_control: 0=off, 1=software XON/XOFF
flow_control=0

# Delay before repeat starts (milliseconds: 250..1000)
repeat_delay_ms=250

# Repeat rate (characters per second: 2..20)
repeat_rate_cps=10

# key_click: 0=off, 1=on
key_click=1

# --- Terminal mode / display ---
# vt52_mode: 0=ANSI (VT100), 1=VT52
vt52_mode=0

# cursor_type: 0=underline, 1=block
cursor_type=0

# cursor_blinking: 0=steady, 1=blinking
cursor_blinking=0

# vt_test: 0=off, 1=on
vt_test=0

# font_selection: 1=8x20, 2=10x20 CRT, 3=10x20 solid
font_selection=2

# Palette indices: 0=black, 1=white, 2=amber, 3=green
text_color=1
background_color=0

# --- Sound / wiring ---
# buzzer_volume: 0..80 (percent duty cycle)
buzzer_volume=50

# margin_bell: 0=off, 1=ring 8 columns before right margin
margin_bell=0

# switch_txrx: 0=normal wiring, 1=swapped via GPIO16
switch_txrx=0

# --- Logging ---
# log_output:
# 0=off
# 1=screen
# 2=file
# 3=wlan
# 4=screen+file
# 5=screen+wlan
# 6=file+wlan
# 7=screen+file+wlan
log_output=0

# wlan_host_autostart: 0=command/log mode after connect, 1=auto-enable host bridge mode
wlan_host_autostart=0

# Log file name (max 63 chars)
log_filename=vt100.log
```

##Circle Boot Files

- `cmdline.txt` — Parsed by Circle during boot. Keep options on one line, e.g. `logdev=tty1 loglevel=4 width=1024 height=768 keymap=DE`.
- `config.txt` / `config64.txt` — Raspberry Pi firmware settings. The template applies `dtoverlay=miniuart-bt`, `enable_uart=1`, and `display_hdmi_rotate=2` for the upside-down LCD in the replica enclosure.

Both templates live in `VT100/bin` alongside the firmware image and WLAN support files.

## Display Orientation

In the 60% VT100 replica, depending on the specific model used, the 1024x768 LCD can hide part of the first line and the left-most characters behind the bezel when mounted in the natural orientation. Mounting the panel upside down resolves the mechanical clearance issue. Rotate the output by 180° by adding the following firmware setting to the active `config.txt`/`config64.txt`:

```ini
display_hdmi_rotate=2
```

This rotates the HDMI output 180° at the firmware level so the framebuffer keeps its normal coordinate system without extra rendering adjustments.

## Debug and Logging

The terminal ships with a logging subsystem that mirrors messages to any combination of screen, SD card file, and WLAN telnet session:

- Smart fallback selects a working sink during early boot and transitions seamlessly once the framebuffer is ready.
- Messages use familiar prefixes (`[NOTE]`, `[WARN]`, `[ERROR]`) with file/line metadata to simplify troubleshooting.
- Screen logging integrates with the terminal view without breaking VT100 escape handling.
- WLAN logging announces readiness with `WLAN ready: telnet <ip>:2323`; connect via telnet to monitor remotely.
- Full telnet host-mode command workflow (`host on/off`, `socat`, `screen`, and auto-start behavior) is documented in section 9.

Refer to `VT100/docs/VT100_Architecture.md` for technical architecture and implementation details, and `VT100/docs/Configuration_Guide.md` for runtime controls and configuration.

## WLAN Telnet and Host Mode

This firmware supports two distinct WLAN session behaviors on the same telnet endpoint (`<ip>:2323`):

1. **Command/log mode** (default) for remote diagnostics and control.
2. **Host bridge mode** for transparent VT100 host traffic over TCP.
3. **Unix `screen` as host mode** to connect any Unix host to the terminal via TCP.

### Command/log mode (WLAN debugging)

Use this mode when you want to inspect runtime logs and run control commands without replacing the UART host path.

```bash
telnet <ip-or-mDNS-name> 2323
```

Typical session:

```text
help
status
echo hello from remote debug
```

What you get in this mode:

- Mirrored log output (`[NOTE]`, `[WARN]`, `[ERROR]` etc.) over telnet.
- Device/network status via `status`.
- Runtime mode switching via `host on` and `host off`.
- Session close via `exit`.

### Switch to transparent host bridge mode

In host bridge mode the terminal keyboard input is wired via tcp session to the connected client and response from client rendered on VT100 screen. So this mode effectively simulats a serial host connection over tcp. To enter this bridge mode proceed as follows:

1. From an active telnet session in command/log mode:

- Type `host on`

2. Behavior in host bridge mode:

  - Keyboard TX from the VT100 app is sent to the active TCP client.
  - TCP RX from the client is rendered directly to the VT100 screen.
  - UART host rendering is suspended while host mode is active to avoid mixed sources.
  

3. Return to command/log mode:
  - Type `host off`, or
  - Press `Ctrl-]` (escape back to command mode).

### Use Unix `screen` as remote host for VT100 app

`screen` cannot connect directly to TCP sockets, so bridge TCP to a pseudo-terminal with `socat`:

```bash
socat PTY,link=/tmp/vt100,raw,echo=0 TCP:<ip>:2323
```

In a second terminal, attach with `screen`:

```bash
screen /tmp/vt100 115200
```

Recommended workflow:

1. Start `telnet <ip> 2323`.
2. Run `host on`.
3. Start `socat` and attach `screen` to `/tmp/vt100`.
4. Interact with the VT100 app as if `screen` were the host endpoint.

### Auto-start host mode on connect

Set the config parameter in `VT100.txt`:

```ini
wlan_host_autostart=1
```

With this enabled, each new telnet client enters host bridge mode automatically. Set `wlan_host_autostart=0` to keep command/log mode as the default.

## Internal VT100 Test Integration

The internal test functions are integrated into the kernel to verify most implemented escape sequences without a host connection while still sharing the same keyboard/UART path:

- A periodic task (`CPeriodicTask`) calls `RunVTTestTick()` every 50 ms to advance timed steps, scroll tests, and summary rendering.
- `onKeyPressed()` forwards keyboard input to `HandleVTTestKey()` first; when a test is active it consumes Enter/Space for PASS/FAIL, otherwise it forwards input to the host UART unchanged.
- The VTTest lifecycle is controlled via `vt_test=0|1` in `VT100.txt` and `CTConfig::SetVTTestEnabled()` when the test ends.

These changes keep VTTest self-contained while preserving normal keyboard/UART behavior when the test is not active.

## Initial Implementation Plan

- [x] Boot application initialising framebuffer with startup banner
- [x] USB support for keyboard and serial port with loopback testing
- [x] Raw keyboard input forwarded to screen
- [x] ANSI escape sequence parsing and rendering
- [x] Configuration system with `VT100.txt` persistence
- [x] SD card filesystem access for configuration storage
- [x] Logging infrastructure with smart fallback across screen/file/WLAN
- [x] Real-time debug output with formatted log messages
- [x] Screen logging that preserves VT100 behaviour
- [x] Serial UART communication implementation
- [x] Bell, TX/RX switching, and other VT100 niceties
- [x] ROM-derived VT100 font support and converter pipeline
- [x] Multi-language keyboard layouts (US, DE, others supported by Circle)
- [x] 800 Hz buzzer with configurable key click
- [x] Configurable auto-repeat for printable, deletion, and cursor keys
- [x] On-screen configuration dialog for all parameters
- [x] WLAN debug log messages via telnet
- [x] Renderer support for bold and underline attributes
- [x] ESC sequence handling for double-width/double-height modes (dynamic scaling)
- [x] Font converter updates for double-width/double-height glyphs (simplified logic)
- [x] Internal VTTest coverage inside kernel (activate with flag `vt_test=1`)
- [x] Comprehensive Test Suite (Core, DEC, Graphics)
- [x] Dynamic Summary showing all tests based on screen geometry
- [x] Implementation of VT100 Setup A and B
- [x] Implementation of additional on-screen configuration dialog for all file-based parameters
- [x] Implementation of TCP host connection
- [x] Implement smooth scrolling (single-line, non-blocking animation)
- [ ] Test coverage on Unix hosts with tool `vttest`

## Templates

The `templates/` directory provides starter files for new modules and deployment config:

- `task_header.tmpl` and `task_implementation.tmpl` are generic task scaffolds.
- Replace all `TaskName`/`CTaskName` placeholders with your concrete module name before adding files under `include/` and `src/`.
- Keep the generated task pattern consistent with existing modules: singleton `Get()`, `Initialize()`, and cooperative `Run()` loop.
- `VT100.txt`, `cmdline.txt`, and `config.txt` are baseline SD-card templates and should stay aligned with the current `CTConfig` key set and defaults.

## Overview on Escape Sequence Coverage

The table below lists the escape and control sequences handled by this firmware, mapped to the DEC/ANSI families they belong to. “Implementation status” reflects what this codebase currently does when the sequence is received, and “Test” references the VTTest step when available.

| Sequence | Description | VT52 | VT100 | VT220 | VT320 | ANSI | Implementation status | Test |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ^H (BS) | Cursor left | ✓ | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ^I (HT) | Tab forward | ✓ | ✓ | ✓ | ✓ | ✓ | Implemented (tab stops) | [PASS] |
| ^J (LF) | New line | ✓ | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ^M (CR) | Carriage return | ✓ | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC M | Reverse index | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC 7 | Save cursor (DECSC) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC 8 | Restore cursor (DECRC) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC H | VT52 home (cursor to 1,1) | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC A | VT52 cursor up | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC B | VT52 cursor down | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC C | VT52 cursor right | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC D | VT52 cursor left | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC E | VT52 next line | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC J | VT52 clear to end of screen | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC K | VT52 clear to end of line | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC Y r c | VT52 direct cursor address | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC H | Set tab stop (HTS) | — | ✓ | ✓ | ✓ | ✓ | Implemented (ANSI mode) | [PASS] |
| ESC < | Enter ANSI Mode | ✓ | — | — | — | — | Implemented | [PASS] |
| ESC #3 | Double-height line (top) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC #4 | Double-height line (bottom) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC #5 | Normal line size | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC #6 | Double-width line | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC #8 | Screen test | — | ✓ | ✓ | ✓ | — | Parsed (ignored) | [PASS] |
| ESC [ A | Cursor up (CUU) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ B | Cursor down (CUD) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ C | Cursor right (CUF) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ D | Cursor left (CUB) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ H | Cursor home (CUP) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ r;c H | Cursor position (CUP) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ J | Erase to end of screen (ED) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 2 J | Clear entire screen (ED=2) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ K | Erase to end of line (EL) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ n X | Erase characters (ECH) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ n L | Insert lines (IL) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ n M | Delete lines (DL) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ n P | Delete characters (DCH) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 4 h / 4 l | Insert mode (IRM) | — | ✓ | ✓ | ✓ | ✓ | Parsed (not supported) | [PASS] |
| ESC [ n m | Select graphic rendition (SGR) | — | ✓ | ✓ | ✓ | ✓ | Partial (0,1,4,5,7) | [PASS] |
| ESC [ 0 m | SGR reset (attributes/colors) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 1 m | SGR bold/intense | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 2 m | SGR dim/half-bright | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 4 m | SGR underline | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 5 m | SGR blink | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 7 m | SGR reverse video | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 27 m | SGR reverse off | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 30-37 / 90-97 m | Set foreground color | — | ✓ | ✓ | ✓ | ✓ | Set in VT100.txt | [PASS] |
| ESC [ 40-47 / 100-107 m | Set background color | — | ✓ | ✓ | ✓ | ✓ | Set in VT100.txt | [PASS] |
| ESC [ ? 2 l | Enter VT52 Mode | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC [ ? 25 h / l | Cursor visible (DECTCEM) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC [ r1; r2 r | Scroll region (DECSTBM) | — | ✓ | ✓ | ✓ | — | Implemented | [PASS] |
| ESC [ g | Clear tab stop (TBC) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ 3 g | Clear all tab stops (TBC) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC [ Z | Back-tab (CBT) | — | ✓ | ✓ | ✓ | ✓ | Implemented | [PASS] |
| ESC d + / d * | Auto page mode on/off | — | — | — | — | — | Implemented (local) | [PASS] |

**VT52 note:** The parser supports a strict VT52 mode enabled via `ESC [ ? 2 l` and disabled via `ESC <`. `ESC H` acts as VT52 Home only in VT52 mode; in ANSI mode, it acts as HTS (Set Tab Stop).

**Color note:** The firmware emulates monochrome VT100/VT220/VT320 terminals. ANSI color SGR codes are parsed but not applied; choose text/background colors in `VT100.txt` instead.



## Troubleshooting

This section collects practical checks and fixes for the most common setup and runtime issues.

### Build fails with `fatal error: stdint.h: No such file or directory`

Cause: incomplete ARM toolchain (compiler installed without target C library headers).

Fix: follow section 5.1 (tool installation and configure command) and section 5.2 (Homebrew formula note) to install the complete toolchain and reconfigure Circle.

Optional cleanup to avoid accidental compiler mismatch:

```bash
brew uninstall arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb
```

### `VT100.txt` changes have no effect

Checklist:

- File name must be exactly `VT100.txt`.
- File must be on the SD boot partition expected by firmware (`SD:/VT100.txt`).
- Keep `key=value` format; unknown keys are ignored.
- Reboot after changing config.

Quick verification key for this feature set:

```ini
wlan_host_autostart=0
```

### Cannot connect to WLAN telnet console

Checklist:

- Confirm WLAN logging is enabled in config (`log_output` includes WLAN: 3, 5, 6, or 7).
- Wait until the network is up and mDNS/IP is announced in logs.
- Connect to port `2323`:

```bash
telnet <ip-or-hostname.local> 2323
```

### Connected via telnet, but only logs appear (no transparent host traffic)

You are in command/log mode. Follow section 9.2 to switch to host bridge mode and to return to command/log mode.

### Host mode enabled but VT100 still follows UART host

Expected behavior in current firmware: UART rendering is paused while WLAN host mode is active. If behavior looks mixed:

- Ensure only one active telnet client is connected.
- Repeat the section 9.2 mode-switch sequence once.
- Confirm section 9.4 configuration for `wlan_host_autostart` is set as intended (`0` or `1`).

### `screen` does not connect directly to TCP

`screen` requires a tty/pty, not a raw TCP socket. Follow the complete `socat` + `screen` bridge procedure in section 9.3.

### Auto-enter host mode on every connect

Set `wlan_host_autostart` as described in section 9.4.
