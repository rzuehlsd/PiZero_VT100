/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "VT100 Terminal App", "index.html", [
    [ "VT100 Terminal Emulation on Raspberry Pi Zero W with Circle Bare Metal Framework", "index.html", "index" ],
    [ "Configuration Guide (VT100)", "md_docs_2_configuration___guide.html", [
      [ "Table of Contents", "md_docs_2_configuration___guide.html#table-of-contents", null ],
      [ "Part A — User / Operator", "md_docs_2_configuration___guide.html#part-a--user--operator", [
        [ "A1) Files you normally edit", "md_docs_2_configuration___guide.html#a1-files-you-normally-edit", null ],
        [ "A2) Setup dialogs", "md_docs_2_configuration___guide.html#a2-setup-dialogs", null ],
        [ "A3) <span class=\"tt\">VT100.txt</span> keys (persisted)", "md_docs_2_configuration___guide.html#a3-vt100txt-keys-persisted", null ],
        [ "A4) WLAN usage (operator level)", "md_docs_2_configuration___guide.html#a4-wlan-usage-operator-level", null ]
      ] ],
      [ "Part B — Admin / Developer", "md_docs_2_configuration___guide.html#part-b--admin--developer", [
        [ "B1) Source of truth and update checklist", "md_docs_2_configuration___guide.html#b1-source-of-truth-and-update-checklist", null ],
        [ "B2) Paths and ownership", "md_docs_2_configuration___guide.html#b2-paths-and-ownership", null ],
        [ "B3) Setup integration notes", "md_docs_2_configuration___guide.html#b3-setup-integration-notes", null ],
        [ "B4) WLAN host mode integration notes", "md_docs_2_configuration___guide.html#b4-wlan-host-mode-integration-notes", null ],
        [ "B5) Validation workflow after config-related changes", "md_docs_2_configuration___guide.html#b5-validation-workflow-after-config-related-changes", null ]
      ] ]
    ] ],
    [ "Carrier Board for Pi Zero VT100 Terminal", "md_docs_2_hardware.html", [
      [ "Backplate for VT100 case to mount board", "md_docs_2_hardware.html#backplate-for-vt100-case-to-mount-board", null ],
      [ "Revision status", "md_docs_2_hardware.html#revision-status", null ],
      [ "Firmware interaction notes (2026-02-18)", "md_docs_2_hardware.html#firmware-interaction-notes-2026-02-18", null ]
    ] ],
    [ "Manual Verification Plan (2026-02-12 to 2026-02-14)", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html", [
      [ "Table of Contents", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#table-of-contents-1", null ],
      [ "Preconditions", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#preconditions", null ],
      [ "General Pass Criterion", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#general-pass-criterion", null ],
      [ "A1) WLAN Command Mode Basics", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#a1-wlan-command-mode-basics", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test", null ]
      ] ],
      [ "A2) WLAN Manual Host Mode Switch", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#a2-wlan-manual-host-mode-switch", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-1", null ]
      ] ],
      [ "A3) WLAN Auto-Start Host Mode", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#a3-wlan-auto-start-host-mode", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-2", null ]
      ] ],
      [ "A4) UART vs Host Source Separation", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#a4-uart-vs-host-source-separation", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-3", null ]
      ] ],
      [ "B1) Setup B Smooth Scroll Bit Mapping", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#b1-setup-b-smooth-scroll-bit-mapping", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-4", null ]
      ] ],
      [ "B2) Setup B Runtime Apply Without Reboot", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#b2-setup-b-runtime-apply-without-reboot", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-5", null ]
      ] ],
      [ "B3) Setup B Additional Mapped Fields Sanity", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#b3-setup-b-additional-mapped-fields-sanity", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-6", null ]
      ] ],
      [ "B4) Setup B Wrap-Around and Margin Bell Spot Check", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#b4-setup-b-wrap-around-and-margin-bell-spot-check", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-7", null ]
      ] ],
      [ "C1) Modern Setup Navigation, Cancel, Save", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#c1-modern-setup-navigation-cancel-save", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-8", null ]
      ] ],
      [ "C2) Modern Setup Persistence Matrix", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#c2-modern-setup-persistence-matrix", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-9", null ]
      ] ],
      [ "C3) Modern Setup Reboot Roundtrip", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#c3-modern-setup-reboot-roundtrip", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-10", null ]
      ] ],
      [ "C4) Modern Setup Runtime Apply Matrix", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#c4-modern-setup-runtime-apply-matrix", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-11", null ]
      ] ],
      [ "D1) Smooth Scroll Boot-Time Apply", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#d1-smooth-scroll-boot-time-apply", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-12", null ]
      ] ],
      [ "D2) Smooth Scroll Single-Line Insert/Delete Behavior", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#d2-smooth-scroll-single-line-insertdelete-behavior", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-13", null ]
      ] ],
      [ "E1) Local Mode Toggle and Local Echo", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#e1-local-mode-toggle-and-local-echo", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-14", null ]
      ] ],
      [ "E2) Local Mode Host Routing Restore", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#e2-local-mode-host-routing-restore", [
        [ "Purpose of Test", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#purpose-of-test-15", null ]
      ] ],
      [ "Test Results Summary", "md_docs_2_manual___testplan__2026-02-12__to__2026-02-14.html#test-results-summary", null ]
    ] ],
    [ "VT100 Architecture and Technical Implementation (VT100)", "md_docs_2_v_t100___architecture.html", [
      [ "Table of Contents", "md_docs_2_v_t100___architecture.html#table-of-contents-2", null ],
      [ "1. Document role", "md_docs_2_v_t100___architecture.html#autotoc_md1-document-role", null ],
      [ "2. Runtime module map (current)", "md_docs_2_v_t100___architecture.html#autotoc_md2-runtime-module-map-current", null ],
      [ "3. Dependency graph (implementation-aligned)", "md_docs_2_v_t100___architecture.html#autotoc_md3-dependency-graph-implementation-aligned", null ],
      [ "4. Boot and initialization sequence", "md_docs_2_v_t100___architecture.html#autotoc_md4-boot-and-initialization-sequence", null ],
      [ "5. Task model and interaction pattern", "md_docs_2_v_t100___architecture.html#autotoc_md5-task-model-and-interaction-pattern", null ],
      [ "6. Runtime data flows", "md_docs_2_v_t100___architecture.html#autotoc_md6-runtime-data-flows", [
        [ "6.1 Keyboard to host flow", "md_docs_2_v_t100___architecture.html#autotoc_md61-keyboard-to-host-flow", null ],
        [ "6.2 Host to display flow", "md_docs_2_v_t100___architecture.html#autotoc_md62-host-to-display-flow", null ]
      ] ],
      [ "7. Setup subsystem details", "md_docs_2_v_t100___architecture.html#autotoc_md7-setup-subsystem-details", [
        [ "7.1 Legacy setup (F12)", "md_docs_2_v_t100___architecture.html#autotoc_md71-legacy-setup-f12", null ],
        [ "7.2 Modern setup (F11)", "md_docs_2_v_t100___architecture.html#autotoc_md72-modern-setup-f11", null ],
        [ "7.3 Setup key handling specifics", "md_docs_2_v_t100___architecture.html#autotoc_md73-setup-key-handling-specifics", null ],
        [ "7.4 Local mode (F10)", "md_docs_2_v_t100___architecture.html#autotoc_md74-local-mode-f10", null ]
      ] ],
      [ "8. Logging and network integration", "md_docs_2_v_t100___architecture.html#autotoc_md8-logging-and-network-integration", [
        [ "8.1 Sink selection model", "md_docs_2_v_t100___architecture.html#autotoc_md81-sink-selection-model", null ],
        [ "8.2 File sink", "md_docs_2_v_t100___architecture.html#autotoc_md82-file-sink", null ],
        [ "8.3 WLAN/telnet sink", "md_docs_2_v_t100___architecture.html#autotoc_md83-wlantelnet-sink", null ],
        [ "8.4 Kernel networking loop and lifecycle", "md_docs_2_v_t100___architecture.html#autotoc_md84-kernel-networking-loop-and-lifecycle", null ]
      ] ],
      [ "9. Font and rendering details", "md_docs_2_v_t100___architecture.html#autotoc_md9-font-and-rendering-details", [
        [ "9.1 DEC special graphics and charset switching", "md_docs_2_v_t100___architecture.html#autotoc_md91-dec-special-graphics-and-charset-switching", null ]
      ] ],
      [ "10. HAL and buzzer details", "md_docs_2_v_t100___architecture.html#autotoc_md10-hal-and-buzzer-details", null ],
      [ "11. Configuration persistence contract", "md_docs_2_v_t100___architecture.html#autotoc_md11-configuration-persistence-contract", null ],
      [ "12. Development notes", "md_docs_2_v_t100___architecture.html#autotoc_md12-development-notes", null ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Functions", "functions_func.html", null ],
        [ "Variables", "functions_vars.html", null ],
        [ "Typedefs", "functions_type.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"_t_color_palette_8h.html",
"index.html#dec-local-mode-f10"
];

var SYNCONMSG = 'click to disable panel synchronization';
var SYNCOFFMSG = 'click to enable panel synchronization';
var LISTOFALLMEMBERS = 'List of all members';