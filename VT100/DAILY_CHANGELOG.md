# Daily Change Log

Reconstructed from git commit history and intended as a concise daily summary of implemented features and codebase changes.

## Update Procedure
- Append only; do not rewrite prior daily sections unless correcting factual errors.
- Use one section per day in ISO format: `## YYYY-MM-DD`.
- For each day with implemented work, add exactly these two bullets:
	- `Implemented features: ...`
	- `Codebase changes: ...`
- Keep entries concise and based on completed, merged work.

## 2026-01-12
- Implemented features: integrated `CScheduler` periodic/background tasks, added scheduler demo task, and moved keyboard/serial handling toward task-based processing.
- Codebase changes: refactored font selection and greeting rendering logic into renderer-focused code, added kernel/scheduler accessors, fixed build/link issues, and updated scope/docs.

## 2026-01-13
- Implemented features: integrated WLAN stack and telnet server with successful terminal connection testing.
- Codebase changes: removed custom scheduler usage, switched keyboard/serial flows to Circle scheduler tasks, and added WLAN console build/link dependencies.

## 2026-01-16
- Implemented features: added periodic task coverage for WLAN logging/terminal server activity.
- Codebase changes: performed broad documentation rework and partial output-device integration updates around `CLogDevice` behavior.

## 2026-01-20
- Implemented features: completed VT100 App 2.0 restructuring with renderer/font-converter integration and support for VT100 base/double-width/double-height font modes.
- Codebase changes: consolidated renderer pipeline around Circle `CTerminal`-based architecture with dynamic font handling.

## 2026-01-21
- Implemented features: added reverse video, underline/bold attribute handling, blinking cursor, and DEC/VT100 color/font escape-sequence behavior.
- Codebase changes: improved escape-sequence parsing for text attributes and continued fixes for combined attribute-command handling.

## 2026-01-22
- Implemented features: integrated `TFontConverter` and `TRenderer` into kernel runtime.
- Codebase changes: reworked kernel integration paths for renderer/font modules.

## 2026-01-23
- Implemented features: integrated keyboard handling and SD-based configuration loading into kernel startup.
- Codebase changes: mounted filesystem support, added `TConfig` kernel integration, and refined text-attribute rendering behavior limits under RGB565.

## 2026-01-29
- Implemented features: integrated `TUART` task flow for host TX/RX and renderer display path.
- Codebase changes: tuned task timing and debug behavior to improve runtime stability.

## 2026-01-30
- Implemented features: added HAL module capabilities (including buzzer/TxRx control), key-click support, and SD config-to-runtime mapping.
- Codebase changes: aligned configuration application across modules and identified/adjusted renderer color type mismatches.

## 2026-02-01
- Implemented features: progressed config integration tests (font/color selection) and documented refactoring state.
- Codebase changes: updated refactoring documentation and integration notes.

## 2026-02-02
- Implemented features: completed WLAN log integration with telnet client and restored expected keyboard auto-repeat behavior.
- Codebase changes: stabilized input/logging interaction paths.

## 2026-02-03
- Implemented features: enabled WLAN logging to file/screen/telnet and ensured startup configuration parameters are applied across modules.
- Codebase changes: finalized host-message display after telnet connection and documented known logging-screen redraw tradeoff.

## 2026-02-05
- Implemented features: improved Doxygen/header documentation workflow.
- Codebase changes: added/used documentation make targets and refreshed documentation structure.

## 2026-02-06
- Implemented features: added updated architectural documentation.
- Codebase changes: expanded architecture-level docs.

## 2026-02-07
- Implemented features: introduced initial Setup Screen A (F12), including display/context restore behavior.
- Codebase changes: migrated `TSetup` toward task implementation and documented remaining stability work for repeated activation.

## 2026-02-08
- Implemented features: completed Setup Dialog A with tab-stop configuration and ESC-based tab management commands.
- Codebase changes: persisted tab settings in `CTConfig` and connected setup UI behavior to config storage.

## 2026-02-09
- Implemented features: implemented underline and block cursor mode; added line-ending options (`LF`/`CR`/`CRLF`).
- Codebase changes: updated control-mode handling paths for cursor and EOL behavior.

## 2026-02-10
- Implemented features: added and executed ANSI/DEC escape-sequence test functions.
- Codebase changes: expanded test coverage for terminal control-sequence handling.

## 2026-02-11
- Implemented features: implemented broad ANSI/ESC coverage including insert/delete line tests, internal VT100 test updates, and graphics font-set selection groundwork.
- Codebase changes: refactored task-level synchronization with spinlock usage in kernel paths and updated architecture documentation.

## 2026-02-12
- Implemented features: implemented DEC extended graphics font-set switching, Setup B (partial), and TCP host access to VT100 terminal.
- Codebase changes: updated tests and README/documentation for terminal/network feature additions.

## 2026-02-13
- Implemented features: added additional file-based parameter configuration dialog and completed consolidation step after refactoring.
- Codebase changes: removed legacy app directory, consolidated documentation, added repository agent-instruction governance files, expanded governance policy with definition-of-done, validation escalation, commit-message draft standard, dependency/security impact reporting, added a commit-workflow exception to not request commits for DAILY_CHANGELOG-only changes, de-duplicated README section 6.2 by keeping the configuration table and merging missing option details into table notes, removed further README redundancies by keeping one authoritative WLAN host-mode workflow in section 9 and fixing documentation policy wording to a 4-document model, reduced overlap in Configuration_Guide/VT100_Architecture/Hardware by consolidating repeated command/key/status lists while preserving technical content, and added Hardware.md to README “Further Documentation” for better discoverability.

## 2026-02-14
- Implemented features: added non-blocking smooth-scroll animation with half-line intermediate frame timing for single-line scroll operations in the VT100 renderer.
- Codebase changes: extended `TRenderer` with smooth-scroll state/buffers and update-loop frame rendering, wired animation scheduling into `Scroll`/`InsertLines`/`DeleteLines` for count-1 paths, prevented immediate write-path flush during active animation, corrected RI behavior to trigger at scroll-region top, and connected Setup B group 1 bit 1 (leftmost, mask `0x8`) to persisted `smooth_scroll` config plus renderer runtime apply paths.
- Implemented features: updated the documentation set to match current smooth-scroll and Setup B behavior.
- Codebase changes: aligned README, Configuration Guide, Architecture, and Known Issues docs with current implementation status, including persisted `smooth_scroll` key coverage and SET-UP B group 1 leftmost bit mapping.
- Implemented features: expanded VTTest coverage with dedicated smooth-scroll ON/OFF demo steps and an RI-at-scroll-top-margin validation scenario.
- Codebase changes: fixed VTTest step-name dispatch mismatches so ANSI/DEC prefixed step names trigger the intended sequence handlers, and restored smooth-scroll runtime state after VTTest execution.
- Implemented features: added a structured manual verification plan for features introduced since 2026-02-12 that are not fully covered by VTTest automation.
- Codebase changes: created `docs/Manual_Testplan_2026-02-12_to_2026-02-14.md` with step-by-step test cases and expected outcomes for WLAN host mode, setup persistence, and smooth-scroll runtime behavior.

## 2026-02-17
- Implemented features: restored visible smooth-scroll animation frames on Setup B toggle (frames now pushed each tick), unblocked macOS builds, refreshed Modern Setup with a three-column view (centered title, centered footer help), clarified serial defaults/baud range and color names, enabled bitmask log output selection (multiple sinks), preserved VT100.txt comments when saving, and added periodic scroll timing stats (smooth vs normal) logged every 30s.
- Codebase changes: push composed smooth-scroll frames to the framebuffer each animation tick, updated Config.mk to use the Homebrew-linked arm-none-eabi toolchain in /usr/local/bin, expanded Modern Setup layout with centered header/footer and Description column, refined Modern descriptions, changed log output editing to toggle sink bits (screen/file/WLAN), added save logic to merge updated values into existing VT100.txt without stripping comments using Circle `CString`-compatible formatting/appends, slowed smooth-scroll pacing to ~170ms per line (~6 lps) with per-pixel steps, ensured smooth-scroll frames render live buffer content so new lines appear progressively instead of in a final batch, replaced scroll queueing with a debounce guard so isolated single-line scrolls animate while bursts fall back to instant (eliminating multi-line jumps/backtracks), and instrumented scroll paths to record/emit per-mode durations; verified a clean `make -j4` build in VT100.
- Implemented features: setup dialogs A, B, and Modern now honor the actively configured VT100 font family instead of forcing 10x20 CRT, and the Modern setup title is rendered single-line and centered correctly.
- Codebase changes: switched renderer ESC #3/#5/#6 font-mode handling to reuse the current configured font selection, changed startup config font application to use selection-based API so state restoration keeps selection metadata, and removed double-width title rendering in Modern setup while centering title text against the inner content width.
- Implemented features: Modern setup title now renders in double width and remains centered.
- Codebase changes: updated Modern title centering math to account for double-width rendered text (`ESC#6`) and restored DEC normal-width reset (`ESC#5`) after title draw.
- Implemented features: corrected Modern setup title placement so double-width title is truly centered instead of drifting to the right.
- Codebase changes: switched title placement to double-width coordinate centering by entering `ESC#6` before `Goto` and calculating the start column in double-width columns for the inner dialog area.
- Implemented features: moved Setup B dialog content block one normal-height line higher to remove the lower-edge placement glitch.
- Codebase changes: shifted Setup B info/data rows and field cursor row from the last screen line pair to the pair above (`rows-3`/`rows-2`) while preserving existing field/bit positions.
- Implemented features: ensured Setup A to Setup B page transitions start from a fully normalized text/font state.
- Codebase changes: added explicit header preamble reset in setup legacy rendering (`ESC[0m`, `ESC(B`, `ESC)B`, `SI`, `ESC#5`) before drawing titles/subtitle so lingering attributes/charset/font-width modes cannot leak between setup pages.
- Implemented features: synchronized user/developer markdown documentation with current implementation status for setup dialogs and serial flow control.
- Codebase changes: updated README and docs to mark SET-UP B Auto XON/XOFF as implemented (`flow_control`), documented persisted `flow_control` key, corrected Modern setup layout description to centered double-width title plus parameter/value/description columns, and captured legacy setup A/B header state normalization behavior.
- Implemented features: implemented legacy SET-UP B margin bell behavior (group 2 leftmost bit) to ring when the cursor reaches 8 characters before the right margin.
- Codebase changes: added persisted `margin_bell` config key, mapped SET-UP B group 2 leftmost toggle to `margin_bell`, and added renderer runtime trigger to call buzzer bell at column `right_margin - 8` for printable character output when enabled.

## 2026-02-18
- Implemented features: added an interactive VTTest step to verify margin bell behavior by starting 5 characters before the bell margin, writing past the threshold, and asking the user to confirm whether the bell sounded.
- Codebase changes: extended `VTTest` core steps with `Margin Bell Right-8`, added dedicated run-path logic to enable margin bell for the test, position the cursor at `right_margin - 13`, write a character burst across the bell point, and keep PASS/FAIL capture integrated into the existing summary output.
- Implemented features: implemented SET-UP B wrap-around control (group 3, second bit from left) with VT100-compatible semantics for both modes, plus two interactive verification tests for wrap ON and wrap OFF behavior with summary reporting.
- Codebase changes: added persisted `wrap_around` config key (default on), mapped SET-UP B group 3 bit mask `0x4` to `wrap_around`, updated renderer printable-character advance logic to hold/overwrite at right margin when wrap is off, and extended VTTest with `Wrap Around ON` and `Wrap Around OFF` steps while preserving/restoring wrap setting across test runs.
- Implemented features: added a keyboard local mode toggle on `F10` that switches input to local loopback and shows an on-screen status line when activated.
- Codebase changes: added `CKernel` local-mode state/toggle handlers, wired `F10` raw-key edge detection into the periodic dispatch path, and routed keypress output to `TRenderer::Write` instead of host/UART while local mode is enabled.
- Implemented features: stabilized runtime responsiveness after introducing local-mode key toggles by preventing periodic input-dispatch starvation under repeated function-key reports.
- Codebase changes: changed periodic `F10`/`F11`/`F12` dispatch in `kernel.cpp` from unbounded queue-drain loops to bounded single-event-per-tick handling so scheduler/yield cadence remains intact and serial/keyboard/setup processing continues.
- Implemented features: synchronized all markdown documentation (`README.md` and every file in `docs/`) with current implemented runtime behavior for local mode, margin bell, and wrap-around.
- Codebase changes: corrected SET-UP B mapping tables (`margin_bell` and `wrap_around`), added `F10` local-mode user/developer flow notes, expanded manual verification guidance, and refreshed known-issues/refactoring/hardware notes for current firmware behavior.
- Implemented features: aligned the shipped `VT100/bin/VT100.txt` template with the active firmware key set and defaults.
- Codebase changes: added missing keys (`smooth_scroll`, `wrap_around`, `flow_control`, `margin_bell`), corrected default values (`vt_test=0`, `log_output=0`), and fixed buzzer range documentation to `0..100`.
- Implemented features: restructured the manual verification plan into standardized test-case blocks with explicit pass criteria and result reporting.
- Codebase changes: updated `docs/Manual_Testplan_2026-02-12_to_2026-02-14.md` to include a TOC, per-test title/purpose sections, step tables (`Step No`, `Description`, `Expected Result`, `Verification`), a global rule that all steps must pass, and a consolidated summary results table.
- Implemented features: restored Modern Setup dialog close/save behavior on Enter across keyboard variants that emit CR/LF combinations.
- Codebase changes: updated `CTSetup::HandleModernKeyPress` to treat `\r`, `\n`, `\r\n`, and `\n\r` as valid Enter input sequences that set save+exit flags.
- Implemented features: Modern Setup now applies changed font selection immediately when closing with Enter (Save).
- Codebase changes: extended setup post-save runtime apply in `CTSetup::Run` to call `TRenderer::SetFont(config->GetFontSelection(), FontFlagsNone)` alongside existing color/cursor/VT mode updates.
- Implemented features: expanded Modern Setup save-apply behavior so additional settings now take effect immediately across runtime subsystems.
- Codebase changes: added `CKernel::ApplyRuntimeConfig()` and wired setup save through it, applying renderer visual state, HAL buzzer/RxTx swap, keyboard repeat timing refresh, UART re-initialization from config, and log target reconfiguration (screen/file/WLAN) without reboot.
- Implemented features: added explicit field-by-field verification guidance for Modern Setup runtime apply behavior.
- Codebase changes: updated `docs/Configuration_Guide.md`, `README.md`, and `docs/Manual_Testplan_2026-02-12_to_2026-02-14.md` with a runtime-apply matrix, subsystem mapping notes, and a dedicated manual test section (`C4`) covering immediate-apply expectations.
- Implemented features: restored stable repeated use of F11/F12 setup dialogs after runtime-apply integration regression.
- Codebase changes: narrowed `CKernel::ApplyRuntimeConfig()` to safe non-disruptive runtime updates (renderer/HAL) and removed keyboard/UART/logging live reconfiguration from dialog-close path to avoid callback-state corruption; documentation runtime-apply matrix was aligned accordingly.
- Implemented features: reduced visible flicker in the F11 Modern setup dialog by updating only affected content rows on selection/value changes instead of repainting the full dialog each keypress.
- Codebase changes: refactored `CTSetup` modern rendering into layout-aware row/list rendering helpers with cached dialog geometry, changed arrow-key handling to perform partial dirty-row updates with safe fallback to full redraw on geometry/window shifts, and reset cached layout state on dialog mode transitions.
- Implemented features: restored keyboard input after closing VTTest summary with Return so terminal typing resumes immediately at home position.
- Codebase changes: gated `CKernel::HandleVTTestKey` forwarding on active `vt_test` config state and hardened `CVTTest::Stop()` to clear summary/wait/pending/sequence hold flags, preventing stale test-state key capture after exit.
- Implemented features: fixed VTTest skip path (test flag on + SPACE skip) so keyboard/serial input resumes immediately after returning to cursor position 1,1.
- Codebase changes: switched VTTest key interception in `CKernel` to runtime-active gating (`CVTTest::IsActive()`), and made `CVTTest::Stop()` always clear `vt_test` state before renderer-dependent cleanup to prevent lingering interception after skip/summary exit.
- Implemented features: refined VTTest suite structure by merging DEC tests 2+3, naming DEC test 4 explicitly, removing unsupported DEC test 5, and converting tests 36/37/38 to animated character-by-character demonstrations.
- Codebase changes: updated `VTTest` step tables (DEC suite + titles), removed `DEC Screen Test`, and changed Wrap ON/OFF + Margin Bell runtime paths to staged sequence playback with visible incremental character updates from a defined start state.
- Implemented features: resolved remaining VTTest exit lockup so both skip-from-intro and exit-after-summary reliably return to normal keyboard/serial operation.
- Codebase changes: replaced callback-context VTTest stop/clear calls with deferred stop requests handled in `CVTTest::Tick()`, ensuring renderer clear/home and `Stop()` run in task context and preventing post-test input deadlock.
- Implemented features: improved VTTest cases 33/34/35 to show real line-end and bell-margin behavior on wider screens with visible markers and dynamic character injection at ~5 chars/sec.
- Codebase changes: replaced fixed-column wrap/margin test scripts with runtime-generated boundary animations based on current renderer dimensions, added explicit end-of-line/next-line and bell-margin markers, and triggered audible bell explicitly via `CHAL::Get()->BEEP()` when the animated stream crosses the bell margin.
- Implemented features: restored rendering of embedded PNG figures in Markdown-based Doxygen pages.
- Codebase changes: configured `IMAGE_PATH` in `VT100/Doxyfile` to include `docs/images` and normalized Markdown embedded image `src` paths in `README.md` and `docs/Hardware.md` to emitted filenames so Doxygen resolves and renders copied PNG/JPEG assets reliably.
- Implemented features: added a build-reference section listing all implemented Make targets and their usage purpose directly in the compile environment chapter.
- Codebase changes: inserted a `Make Targets` table in `README.md` below `Compile Environment Setup`, covering project-local targets (`all`, `kernel.img`, `docs`, `docs_clean`) plus inherited Circle `Rules.mk` targets (`clean`, `install`, `tftpboot`, `flash`, `monitor`, `cat`).
- Implemented features: added explicit toolchain maintenance Make targets for one-command Circle toolchain configuration and compiler/header verification.
- Codebase changes: extended `VT100/Makefile` with `toolchain-configure` (wraps `third_party/circle/configure -f -p <prefix>`) and `toolchain-check` defaults (`TOOLCHAIN_PREFIX`, `CHECK_TMPDIR`) to support stable checks even when system temp is constrained.
- Implemented features: restored working README image embedding and documentation links after moving project README to repository root.
- Codebase changes: updated root `README.md` relative paths for embedded images to `VT100/docs/images/...` and developer guide links to `VT100/docs/...` so GitHub rendering resolves correctly from the new location.
- Implemented features: enabled automatic publishing of generated Doxygen HTML documentation to GitHub Pages on each push to `main`.
- Codebase changes: added repository-level GitHub Actions workflow `.github/workflows/docs-pages.yml` to install Doxygen/Graphviz, run `make docs` in `VT100/`, and deploy `VT100/docs/doxygen/html` via Pages; added root `README.md` link to the published Pages documentation URL; updated `VT100/Doxyfile` input/mainpage from `README.md` to `../README.md` after moving the project README to repository root.
- Implemented features: stabilized GitHub Pages docs deployment by ensuring CI checks out required project submodules.
- Codebase changes: updated `.github/workflows/docs-pages.yml` checkout step to `submodules: recursive` so `third_party/circle/Rules.mk` is available during `make docs` in CI.
- Implemented features: restored image rendering for root-README content on the generated Doxygen main page.
- Codebase changes: extended `VT100/Makefile` `docs` target to mirror `VT100/docs/images` into `VT100/docs/doxygen/html/VT100/docs/images`, matching root-README image paths (`VT100/docs/images/...`) used by Doxygen when `../README.md` is the main page.
- Implemented features: restored Mermaid diagram rendering in Git-hosted Markdown views for architecture documentation.
- Codebase changes: added standard fenced `mermaid` code blocks to `VT100/docs/VT100_Architecture.md` for the dependency graph and sequence diagrams, while preserving existing Doxygen `\htmlonly` Mermaid blocks for generated HTML docs.
- Implemented features: removed duplicate architecture diagram output in Git Markdown views.
- Codebase changes: removed legacy Doxygen-only `\htmlonly` Mermaid blocks from `VT100/docs/VT100_Architecture.md`, leaving a single canonical set of fenced `mermaid` diagrams to avoid rendered+text duplication in Git.

## 2026-02-19
- Implemented features: restored visible images in the hardware description markdown page.
- Codebase changes: updated `VT100/docs/Hardware.md` image references from local-file names to `images/...` paths (`Schematic_V22.png`, `Layout_V22.png`, `Backplate_Front.png`, `Backplate_Back.png`) so Git Markdown and Doxygen resolve the assets from `VT100/docs/images` correctly.
- Implemented features: improved telnet host-mode recoverability by adding a firmware-side fallback escape sequence for returning to command mode when client-side `Ctrl-]` handling is unavailable.
- Codebase changes: extended `CTWlanLog` host-mode RX handling to recognize `+++` as a host-mode exit trigger, updated host-mode help/session prompts to advertise both `Ctrl-]` and `+++`, and synchronized operator/developer guidance in `docs/Configuration_Guide.md`.
- Implemented features: added on-screen telnet connect guidance to the waiting state so users can see the target endpoint directly on VT100 while waiting for a remote session.
- Codebase changes: updated `CKernel::MarkTelnetWaiting()` to render a dynamic waiting message with `telnet <ip> 2323` when IP is available, and a `<ip-address>` placeholder plus port fallback when IP is not yet assigned.
- Implemented features: introduced a consistent command-mode telnet prompt (`>: `) and updated host-mode escape controls to use `Ctrl-C` with `+++` as fallback.
- Codebase changes: added command-prompt emission in `CTWlanLog` for command mode (session start, after entered lines, and after host-mode exit), removed `host off` from command interface/help text, switched host escape handling from `Ctrl-]` to `Ctrl-C`, and synchronized `docs/Configuration_Guide.md` accordingly.
- Implemented features: extended the VT100 telnet waiting screen to also show a hostname-based `.local` connect hint in addition to the IP/port hint.
- Codebase changes: updated `CKernel::MarkTelnetWaiting()` to append `telnet <hostname>.local 2323` when hostname is available, and documented the dual waiting-screen connect hints in `docs/Configuration_Guide.md`.
- Implemented features: improved command-mode telnet usability by making log output prompt-safe and normalizing empty-line Enter handling.
- Codebase changes: updated `CTWlanLog` to track prompt visibility, force asynchronous log output onto a fresh line before restoring the `>: ` prompt in command mode, and emit `\r\n` plus prompt on empty Enter so Return no longer appears as `^M`; documentation was aligned in `docs/Configuration_Guide.md`.
- Implemented features: waiting-screen telnet connect hint now upgrades from placeholder to real IP automatically once DHCP address assignment completes.
- Codebase changes: added kernel-side waiting-message IP-state tracking and periodic waiting-screen refresh trigger while no telnet client is connected, so `MarkTelnetWaiting()` re-renders once concrete IP data becomes available instead of staying on `<ip-address>`.
- Implemented features: fixed command-mode telnet output layout so command responses no longer appear inline with Enter as `^M` artifacts.
- Codebase changes: adjusted `CTWlanLog::HandleCommandChar()` to emit a clean CRLF before processing non-empty command lines, ensuring `help`/`status` output starts on a new line while preserving existing `echo` behavior.
- Implemented features: waiting/connect guidance on VT100 is now shown only once a real IP address is available, avoiding duplicate early/late waiting messages.
- Codebase changes: updated `CKernel::MarkTelnetWaiting()` to defer rendering until DHCP yields a concrete IP and to skip placeholder rendering while still keeping telnet-wait state active.
- Implemented features: improved telnet command-mode input rendering by enabling server-side echo negotiation and firmware echo of typed command characters/backspace handling.
- Codebase changes: switched telnet negotiation to `WILL ECHO`, accepted `DO ECHO`, and added command-mode character/backspace echo sequences in `CTWlanLog::HandleCommandChar()` to reduce client-side `^M` artifacts.
- Implemented features: normalized telnet logger line endings in both command and host mode so log messages render with proper CRLF boundaries.
- Codebase changes: reworked `CTWlanLog::Write()` to normalize `\n` to `\r\n` (with split-write-safe CR tracking), preserve command-mode prompt behavior with line-safe prompt restoration, and reset CR-tracking state on connection reset.
- Implemented features: restored reliable `exit` behavior in telnet command mode by preventing close-path lock recursion.
- Codebase changes: refactored `CTWlanLog::CloseClient()` to defer disconnect logger writes until after send-lock release, removing a potential deadlock when logger output re-enters the telnet write path during disconnect.
- Implemented features: improved telnet disconnect UX by restoring stable telnet input negotiation and adding an explicit VT100 on-screen disconnect notice.
- Codebase changes: reverted command-side telnet echo negotiation/character-injection changes (`WILL ECHO` path) back to the prior negotiation mode, and added a renderer-side `Telnet client disconnected` message in `CTWlanLog::CloseClient()` before returning to waiting state.
- Implemented features: stabilized `exit` disconnect handling by deferring socket close to a safe point after receive-chunk processing.
- Codebase changes: changed `exit` command flow to set a deferred close flag processed in `HandleIncomingData()`, prevented further byte handling once close is requested, and replaced hardcoded disconnect text length with dynamically sized reason-aware VT100 disconnect messages.
- Implemented features: reduced command-mode telnet `^M` artifacts by enabling server-side ECHO negotiation.
- Codebase changes: updated telnet option negotiation in `CTWlanLog` to advertise `WILL ECHO` and accept `DO ECHO` alongside suppress-go-ahead handling, while keeping deferred-close disconnect logic unchanged.
- Implemented features: added a post-greeting telnet notice indicating WLAN mode is active and network establishment may require waiting before connection-ready messages appear.
- Codebase changes: extended `CTWlanLog::AnnounceConnection()` greeting text with an explicit wait hint for WLAN/network setup progress.
- Implemented features: fixed the `VT100_TTY` helper script so `screen` is attached directly to the PTY bridge, enabling bidirectional host-mode loop testing.
- Codebase changes: replaced one-way `tee` usage with direct `screen <pty>` attachment, changed default TCP port to `2323`, and added robust startup/cleanup handling (`set -euo pipefail`, trap-based process/file cleanup, PTY readiness wait loop).
- Implemented features: extended `VT100_PTY` with optional automatic line reply mode for host-loop testing (`--autorespond`).
- Codebase changes: added argument parsing/help output, screen logfile-based responder process with `AUTO:` loop-prevention tagging, and cleanup handling for responder/log artifacts while keeping default interactive behavior unchanged.
- Implemented features: restored `VT100_PTY --autorespond` startup compatibility on macOS by removing unsupported `screen` logfile option usage.
- Codebase changes: removed leftover `screen -Logfile` invocation, switched autorespond logging to a dedicated temp directory with `screen -L` (`screenlog.0`), and verified script startup no longer fails with `Unknown option Logfile`.
- Implemented features: improved `VT100_PTY --autorespond` runtime stability for host-mode loop testing on macOS.
- Codebase changes: removed `socat` PTY escape termination (`escape=0x03`) to avoid unintended bridge shutdown on control-byte traffic, normalized autoresponder log parsing for CR/LF streams, and fixed macOS locale-related `tr: Illegal byte sequence` failures via `LC_ALL=C` in the log-tail pipeline.
- Implemented features: simplified autorespond flow by keeping `socat` + `screen` and running a dedicated Python echo helper inside the `screen` session.
- Codebase changes: refactored `VT100_PTY --autorespond` to launch `screen` with `VT100_SCREEN_ECHO.py` (`--pty` bridge path, `AUTO:` prefix), removed logfile-tail based responder logic, and deleted obsolete standalone helper `VT100_TCP_ECHO.py`.
- Implemented features: aligned host-mode loop testing with a minimal stdin/stdout responder path and reduced multi-echo artifacts in host mode.
- Codebase changes: changed `VT100_PTY --autorespond` to execute `VT100_SCREEN_ECHO.py` as a pure stdin->stdout helper via PTY redirection (`< "$PTY" > "$PTY"`) inside `screen`, simplified `VT100_SCREEN_ECHO.py` to byte passthrough only, and updated `CTWlanLog::Write()` to suppress logger mirroring to the remote client while host mode is active so PTY loopback traffic is not amplified by log lines.
- Implemented features: stabilized `--autorespond` startup when launched under `screen` on environments with reduced shell `PATH`.
- Codebase changes: added Python interpreter resolution (`python3`/`python`) in `VT100_PTY` and switched screen command execution to use the resolved absolute interpreter path so autoresponder no longer exits with `python3: command not found`.
- Implemented features: made simulated host responses visually identifiable in host-mode loop tests.
- Codebase changes: updated `VT100_SCREEN_ECHO.py` to echo complete lines with `SIMHOST: ` prefix and added loop protection to skip lines already marked as simulated-host output.
- Implemented features: improved simulated host interaction control by preventing startup-message re-echo, forwarding Ctrl-C escape to VT100 host mode, and performing graceful shutdown signaling.
- Codebase changes: extended `VT100_SCREEN_ECHO.py` with filtered ignore prefixes for known WLAN/telnet greeting/log lines, `SIGINT` handling that forwards `0x03` (host-mode exit), and shutdown sequence forwarding (`Ctrl-C` + `exit`) on EOF/termination.
- Implemented features: enforced silent remote-session startup when WLAN host auto-start is enabled so host mode begins without sending greeting/status text to the remote endpoint.
- Codebase changes: updated `CTWlanLog::AcceptClient()` to detect auto-host mode before connection announcement, skip remote welcome/help/status `SendLine` traffic in that mode, enable host mode immediately on connect, and keep connection/auto-start notices local via logger fallback only.
- Implemented features: improved host-mode exit robustness with deterministic Ctrl-C shutdown signaling and stable local VT100 recovery after telnet disconnects.
- Codebase changes: updated `VT100_SCREEN_ECHO.py` to treat Ctrl-C as immediate graceful shutdown (`Ctrl-C` + `exit`), ignore non-printable/noisy lines before simulated-host echoing, changed `CTWlanLog::CloseClient()` to resume local-ready state for client-side close reasons (`requested by client`, `receive failed`, `send failed`), and adjusted kernel telnet-waiting transitions to avoid forcing immediate waiting-mode re-entry while local-ready mode is active.
- Implemented features: removed residual startup control-character injection and improved first-line responder handling for CR-terminated input.
- Codebase changes: changed `VT100_SCREEN_ECHO.py` shutdown sequence to `exit\r` (no injected `Ctrl-C`), stopped sending shutdown bytes on plain EOF, and replaced LF-only line splitting with CR/LF-aware delimiter parsing so early host input is processed as soon as `CR` or `LF` arrives.
- Implemented features: suppressed stray startup control-character artifacts (`^C^CA`) at host-mode entry before first real remote payload.
- Codebase changes: added host-data priming state in `CTWlanLog` and updated host-mode RX handling to drop early non-printable control bytes until first printable/escape payload arrives, while preserving normal host rendering once primed.
- Implemented features: removed telnet-option byte leakage in auto-host raw sessions by disabling telnet negotiation for auto-start host mode.
- Codebase changes: updated `CTWlanLog::AcceptClient()` to skip `SendTelnetNegotiation()` when `wlan_host_autostart` is enabled, keeping raw `socat/screen` host-loop sessions free of telnet control-sequence artifacts (`^C^CA`) while retaining telnet negotiation for non-auto-host command sessions.

## 2026-02-20
- Implemented features: enforced strict WLAN log-mode vs host-mode session separation without transitional/manual in-session host switching.
- Codebase changes: removed manual `host on` command path and host escape fallback switching from `CTWlanLog`, made host-mode input path raw per session, aligned README/configuration/architecture/test plan/separation-plan docs to strict-only semantics, and validated with `make -j4` plus `make docs`.
- Implemented features: completed documentation harmonization for the transition from multiplexed WLAN operation toward clean log-mode vs host-mode separation, including a dedicated phased separation roadmap.
- Codebase changes: added root `.gitignore` cleanup rules for local/generated artifacts, updated `README.md`, `docs/Configuration_Guide.md`, `docs/VT100_Architecture.md`, and `docs/Manual_Testplan_2026-02-12_to_2026-02-14.md` to align terminology and workflows (`log mode`, transitional `host on`, `Ctrl-C`/`+++` exit), and created `docs/WLAN_Mode_Separation_Plan.md` with phased implementation, config migration (`wlan_mode_policy` proposal), and validation strategy.
- Implemented features: added explicit execution status tracking in the WLAN separation plan to show completed vs open phases.
- Codebase changes: updated `docs/WLAN_Mode_Separation_Plan.md` with a dated current-status section and phase markers (`DONE`/`OPEN`) reflecting the implemented strict-separation state.
- Implemented features: added explicit 3-state WLAN mode policy and user-facing mode/status messaging for WLAN log, WLAN host, and WLAN disabled operation.
- Codebase changes: changed `wlan_host_autostart` handling to `0=off`, `1=log`, `2=host` across config parsing, setup mapping, and runtime activation logic; added post-greeting WLAN mode info after config load; merged connect hints into one line (`IP` + `hostname.local`); changed client connect notices to mode-specific wording; and adjusted WLAN log-mode prompt/log formatting to separate log output from prompt with spaces only (no extra CRLF before log messages).
- Implemented features: completed VT100 template coverage for persisted setup/config parameters, including the missing SET-UP B related options.
- Codebase changes: updated `VT100/templates/VT100.txt` and `VT100/bin/VT100.txt` to include `flow_control`, `smooth_scroll`, `wrap_around`, and `margin_bell`, and aligned shipped template defaults (`vt_test=0`, `log_output=0`) with firmware defaults.
- Implemented features: restored visible command entry in WLAN log-mode telnet sessions so typed characters are shown while commands are entered.
- Codebase changes: updated `CTWlanLog::HandleCommandChar()` to echo printable characters immediately to the telnet client and to render backspace/delete as `\b \b` when editing the command line, while preserving existing command execution and VT100 display routing behavior.
- Implemented features: improved host-mode disconnect UX with a dedicated local VT100 status message on session end.
- Codebase changes: updated `CTWlanLog::CloseClient()` to show `Host disconnected - resume normal operation` after host-mode disconnect while keeping non-host disconnect messaging unchanged.
- Implemented features: refined host-mode on-screen status texts for waiting and session connect states.
- Codebase changes: updated `CKernel::MarkTelnetWaiting()` to show `Waiting for host to connect via tcp ...` in host mode, and `CKernel::MarkTelnetReady()` to show `Host connected via tcp - CRTL-C in host session to close connection` on host connect.
- Implemented features: documented successful local loopback verification for WLAN logging mode and WLAN host mode in the main project README.
- Codebase changes: updated `README.md` networking/feature sections to explicitly state successful validation with `VT100_PTY --autorespond`.
