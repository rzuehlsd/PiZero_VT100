//------------------------------------------------------------------------------
// Module:        VTTest
// Description:   Simple VT100 test runner for renderer validation.
// Author:        R. Zuehlsdorff
// Created:       2026-02-09
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#include "VTTest.h"

#include <circle/logger.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <string.h>

#include "TConfig.h"
#include "TRenderer.h"
#include "hal.h"

LOGMODULE("VTTest");

namespace
{
template <size_t N>
constexpr unsigned len(const char (&)[N])
{
    return static_cast<unsigned>(N - 1);
}

static const unsigned kStepDelayMs = 5000;
static const unsigned kCursorHideMs = 5000;
static const unsigned kScrollLineDelayMs = 1000;
static const unsigned kSequencePartDelayMs = 2000;
static const unsigned kBoundaryCharDelayMs = 200;
static const unsigned kScrollLineCount = 10;
static const char *kScrollLines[kScrollLineCount] = {
    "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "L9", "L10"
};

static const CVTTest::TVTTestStep kCoreSteps[] = {
    {"ANSI Cursor Position", "\x1B[1;1H\x1B[5;10H", "Cursor should be at row 5, column 10.", 4, 9},
    {"ANSI Cursor Home", "\x1B[H", "Cursor should move to home (row 1, col 1).", 0, 0},
    {"ANSI Cursor Up", "\x1B[10;10H\x1B[A", "Cursor should move up to row 9, col 10.", 8, 9},
    {"ANSI Cursor Down", "\x1B[10;10H\x1B[B", "Cursor should move down to row 11, col 10.", 10, 9},
    {"ANSI Cursor Right", "\x1B[10;10H\x1B[C", "Cursor should move right to col 11.", 9, 10},
    {"ANSI Cursor Left", "\x1B[10;10H\x1B[D", "Cursor should move left to col 9.", 9, 8},
    {"VT52 Cursor Up", "\x1B[10;10H\x1B[?2l" "\x1B" "A" "\x1B<", "Cursor should move up to row 9, col 10 (VT52).", 8, 9},
    {"VT52 Cursor Down", "\x1B[10;10H\x1B[?2l" "\x1B" "B" "\x1B<", "Cursor should move down to row 11, col 10 (VT52).", 10, 9},
    {"VT52 Cursor Right", "\x1B[10;10H\x1B[?2l" "\x1B" "C" "\x1B<", "Cursor should move right to col 11 (VT52).", 9, 10},
    {"VT52 Cursor Left", "\x1B[10;10H\x1B[?2l" "\x1B" "D" "\x1B<", "Cursor should move left to col 9 (VT52).", 9, 8},
    {"VT52 Home", "\x1B[5;10H\x1B[?2l" "\x1B" "H" "\x1B<", "Cursor should move to home (row 1, col 1) (VT52).", 0, 0},
    {"VT52 Clear to End", "\x1B[6;10HABC\x1B[?2l" "\x1B" "J" "\x1B<", "Everything from cursor to end should be cleared (VT52).", -1, -1},
    {"VT52 Clear Line", "\x1B[2;10HHello\x1B[2;10H\x1B[?2l" "\x1B" "K" "\x1B<", "Line 2 (from col 10) should be cleared (VT52).", -1, -1},
    {"VT52 Position", "\x1B[?2l" "\x1B" "Y%*\x1B<", "Cursor should move to row 5, col 10 (VT52 ESC Y).", 4, 9},
    {"ANSI Index (IND)", "\x1B[10;10H" "\x1B" "D", "Cursor should move down to row 11, col 10 (ESC D).", 10, 9},
    {"ANSI Next Line (NEL)", "\x1B[10;10H" "\x1B" "E", "Cursor should move to row 11, col 1 (ESC E).", 10, 0},
    {"ANSI Rev Index (RI)", "\x1B[6;10HX\x1B[6;10H" "\x1B" "M\x1B[5;10HY", "Y should appear one line above X (ESC M).", -1, -1},
    {"ANSI RI at Scroll Top",
        "\x1B[6;9r"
        "\x1B[6;1HAAAA"
        "\x1B[7;1HBBBB"
        "\x1B[8;1HCCCC"
        "\x1B[9;1HDDDD"
        "\x1B[6;1H\x1BM"
        "\x1B[6;1HTOP!"
        "\x1B[r",
     "At top margin, RI should scroll region down: original line 6 shifts to line 7.", -1, -1},
    {"ANSI Save/Restore", "\x1B[10;10H\x1B" "7\x1B[1;1H\x1B" "8", "Cursor should restore to row 10, col 10 (ESC 7/8).", 9, 9},
    {"ANSI Backspace", "\x1B[12;10HAB\bC", "Text should read 'AC' at row 12, col 10.", -1, -1},
    {"ANSI Tab Forward",
     "\x1B[3g"              // clear all tab stops
     "\x1B[2;5H\x1BH"        // set tab stop at col 5
     "\x1B[2;10H\x1BH"       // set tab stop at col 10
     "\x1B[2;1HA\tB\tC",     // should land at col 5 and 10
        "Tabs set at col 5 and 10. B should appear at col 5, C at col 10.",
     -1, -1},
    {"ANSI Back Tab",
     "\x1B[3g"
     "\x1B[2;5H\x1BH"
     "\x1B[2;10H\x1BH"
        "\x1B[2;12H\x1B[ZX",    // back-tab then write X
      "Tabs at 5 and 10. Back Tab from 12 should land at 10. X at col 10.",
     -1, -1},
    {"ANSI Clear Tab Stop",
        "\x1B[3g"              // clear all tab stops
        "\x1B[2;10H\x1BH"       // set tab stop at col 10
        "\x1B[2;1HA\tB"         // B should land at col 10
        "\x1B[2;10H\x1B[g"      // clear current tab stop
        "\x1B[3;1HA\tB",        // B should now NOT land at 10.
        "Clear tab at 10. On row 3, 'A TAB B' should put B at default tab (Col 9) or end.",
     -1, -1},
    {"ANSI Clear Screen", "", "Screen should be fully blank for 5 seconds.", -1, -1},
    {"ANSI Erase to End", "\x1B[6;10HABC\x1B[J", "Everything from cursor to end should be cleared.", -1, -1},
    {"ANSI Clear Line", "\x1B[2;10HHello\x1B[2;10H\x1B[K", "Line 2 (from col 10) should be cleared.", -1, -1},
    {"ANSI Erase Chars", "", "Line 6 shows ABCDEFG, then erase runs; result should show three blanks then DEFG.", -1, -1},
    {"ANSI Delete Chars", "", "Line 6 shows ABCDEFG, then delete runs; result should be DEFG at col 10.", -1, -1},
    {"ANSI Insert Lines", "", "Rows 6-8 show AAA/BBB/CCC. After insert at row 7, row 7 is blank; row 8=BBB, row 9=CCC.", -1, -1},
    {"ANSI Delete Lines", "", "Rows 6-8 show AAA/BBB/CCC. After delete at row 7, row 7=CCC; row 8 blank.", -1, -1},
    {"ANSI Insert Mode", "\x1B[4h\x1B[4l", "No visible change expected (insert mode toggled).", -1, -1},
    {"DEC Cursor Visible", "\x1B[?25l", "Cursor should hide for 5 seconds, then show again.", -1, -1},
    {"DEC Scroll Region",
     "\x1B[6;9r"              // set scroll region rows 6-9
        "\x1B[5;1HTOP"           // marker above region (col 1)
        "\x1B[10;1HBOT"          // marker below region (col 1)
        "\x1B[6;1H\x1B[K"        // clear line 6 from col 1
        "\x1B[7;1H\x1B[K"        // clear line 7 from col 1
        "\x1B[8;1H\x1B[K"        // clear line 8 from col 1
        "\x1B[9;1H\x1B[K"        // clear line 9 from col 1
        "\x1B[6;1H",             // position cursor at start of scroll region
     "TOP and BOT must stay fixed; rows 6-9 should scroll as new lines arrive.",
     -1, -1},
     {"Smooth Scroll ON Demo",
      "\x1B[6;9r"
          "\x1B[5;1HTOP"
          "\x1B[10;1HBOT"
          "\x1B[6;1H",
      "Smooth scroll ON: rows 6-9 should animate single-line scrolling while L1..L10 stream.",
      -1, -1},
     {"Smooth Scroll OFF Demo",
      "\x1B[6;9r"
          "\x1B[5;1HTOP"
          "\x1B[10;1HBOT"
          "\x1B[6;1H",
      "Smooth scroll OFF: rows 6-9 should jump per line (no intermediate animation).",
      -1, -1},
    {"Wrap Around ON", "", "Wrap ON: write at line end should continue on next line. ENTER if wrap happened, SPACE if not.", -1, -1},
    {"Wrap Around OFF", "", "Wrap OFF: at line end extra chars overwrite last cell and cursor stays there. ENTER if correct, SPACE if not.", -1, -1},
    {"Margin Bell Right-8", "", "Starts 5 chars before bell point and writes past it. ENTER if bell sounded, SPACE if not.", -1, -1},
    {"Custom Auto Page Mode", "", "Region 5-10 filled A-F. WRAP should overwrite Line A without scrolling.", -1, -1},
};

static const CVTTest::TVTTestStep kDecSteps[] = {
    {"DEC Line/Char Attributes",
    "",
    "Line 4: double width+height. Line 8: double width. Line 12: normal. Line 16: bold/underline/reverse.",
     -1, -1},
    {"ANSI SGR Dim + Reverse",
        "\x1B[5;1H\x1B[K\x1B[2mDIM\x1B[0m \x1B[7mREV\x1B[27mNORM\x1B[0m",
     "DIM should appear dimmer; REV should be reversed; NORM should return to normal video.",
     -1, -1},
    {"DEC Special Graphics Set",
     "",
     "Line 6 should show line drawing characters (diamond, corners, lines).",
     -1, -1},
};

static const char *kGraphicsFontParts[] = {
    // 1. Normal Size
    "\x1B[2J\x1B[H\x1B#5Normal Size:\r\n"
    "Normal: `abcdefghijklmnopqrstuvwxyz{|}~\r\n"
    "Graph : \x1B(0`abcdefghijklmnopqrstuvwxyz{|}~\x1B(B",

    // 2. Double Width (Positioned below Normal)
    "\x1B[5;1H\x1B#5Double Width:\r\n"
    "\x1B#6Normal: `abcdefghijklmnopqrstuvwxyz{|}~\r\n"
    "\x1B#6Graph : \x1B(0`abcdefghijklmnopqrstuvwxyz{|}~\x1B(B",

    // 3. Double Height (Positioned below Double Width)
    "\x1B[10;1H\x1B#5Double Height:\r\n"
    "\x1B#3Normal: `abcdefghijklmnopqrstuvwxyz{|}~\r\n"
    "\x1B#4Normal: `abcdefghijklmnopqrstuvwxyz{|}~\r\n"
    "\x1B#3Graph : \x1B(0`abcdefghijklmnopqrstuvwxyz{|}~\x1B(B\r\n"
    "\x1B#4Graph : \x1B(0`abcdefghijklmnopqrstuvwxyz{|}~\x1B(B\r\n"
    "\x1B[24;1H\x1B#5\x1B(B" // Ensure Normal Single Width and ASCII at bottom
};
static const unsigned kGraphicsFontPartCount = sizeof(kGraphicsFontParts) / sizeof(kGraphicsFontParts[0]);

static const char *kDecLineAttrParts[] = {
    "\x1B[2J\x1B[H",
    "\x1B[4;1H\x1B#3DOUBLE WIDTH DOUBLE HEIGHT\r\n\x1B#5",
    "\x1B[5;1H\x1B#4DOUBLE WIDTH DOUBLE HEIGHT\r\n\x1B#5",
    "\x1B[10;1H\x1B#6DOUBLE WIDTH\r\n\x1B#5",
    "\x1B[14;1H\x1B#5NORMAL FONT\r\n",
    "\x1B[18;1H\x1B[1mBOLD\x1B[0m \x1B[4mUNDERLINE\x1B[0m \x1B[7mREVERSE\x1B[0m \r\n"
};
static const unsigned kDecLineAttrPartCount = sizeof(kDecLineAttrParts) / sizeof(kDecLineAttrParts[0]);

static const char *kClearScreenParts[] = {
    "\x1B[2J\x1B[H"
};
static const unsigned kClearScreenPartCount = sizeof(kClearScreenParts) / sizeof(kClearScreenParts[0]);

static const char *kDeleteCharParts[] = {
    "\x1B[6;1H\x1B[K\x1B[6;10HABCDEFG",
    "\x1B[6;10H\x1B[3P"
};
static const unsigned kDeleteCharPartCount = sizeof(kDeleteCharParts) / sizeof(kDeleteCharParts[0]);

static const char *kEraseCharParts[] = {
    "\x1B[6;1H\x1B[K\x1B[6;10HABCDEFG",
    "\x1B[6;10H\x1B[3X"
};
static const unsigned kEraseCharPartCount = sizeof(kEraseCharParts) / sizeof(kEraseCharParts[0]);

static const char *kInsertLineParts[] = {
    "\x1B[6;1H\x1B[K\x1B[7;1H\x1B[K\x1B[8;1H\x1B[K\x1B[9;1H\x1B[K\x1B[6;10HAAA\x1B[7;10HBBB\x1B[8;10HCCC",
    "\x1B[7;10H\x1B[1L"
};
static const unsigned kInsertLinePartCount = sizeof(kInsertLineParts) / sizeof(kInsertLineParts[0]);

static const char *kDeleteLineParts[] = {
    "\x1B[6;1H\x1B[K\x1B[7;1H\x1B[K\x1B[8;1H\x1B[K\x1B[9;1H\x1B[K\x1B[6;10HAAA\x1B[7;10HBBB\x1B[8;10HCCC",
    "\x1B[7;10H\x1B[1M"
};
static const unsigned kDeleteLinePartCount = sizeof(kDeleteLineParts) / sizeof(kDeleteLineParts[0]);

static const char *kAutoPageParts[] = {
    "\x1B[5;10r\x1B[5;1HLine A\x1B[6;1HLine B\x1B[7;1HLine C\x1B[8;1HLine D\x1B[9;1HLine E\x1B[10;1HLine F", // 1. Fill 5-10
    "\x1B" "d+",                           // 2. Enable Auto Page
    "\r\nWRAP",                            // 3. Newline (wraps to top) + Write
    "\x1B" "d*\x1B[r"                      // 4. Disable and Reset
};
static const unsigned kAutoPagePartCount = sizeof(kAutoPageParts) / sizeof(kAutoPageParts[0]);

struct TVTSuite
{
    const char *name;
    const CVTTest::TVTTestStep *steps;
    unsigned count;
};

static const TVTSuite kSuites[] = {
    {"Core VT100/ANSI", kCoreSteps, sizeof(kCoreSteps) / sizeof(kCoreSteps[0])},
    {"DEC Enhancements", kDecSteps, sizeof(kDecSteps) / sizeof(kDecSteps[0])}
};

static const unsigned kSuiteCount = sizeof(kSuites) / sizeof(kSuites[0]);
static_assert(sizeof(kCoreSteps) / sizeof(kCoreSteps[0]) <= CVTTest::kMaxSteps, "Increase kMaxSteps");
static_assert(sizeof(kDecSteps) / sizeof(kDecSteps[0]) <= CVTTest::kMaxSteps, "Increase kMaxSteps");
}

CVTTest::CVTTest(void)
    : m_pRenderer(nullptr),
      m_bActive(false),
      m_bCompleted(false),
      m_bLastEnabled(false),
            m_bStopRequested(false),
      m_nStep(0),
    m_nNextTick(0)
{
}

void CVTTest::Initialize(CTRenderer *pRenderer)
{
    m_pRenderer = pRenderer;
}

void CVTTest::Start(void)
{
    m_bActive = true;
    m_bCompleted = false;
    m_bStopRequested = false;
    m_hasSavedTabStops = false;
    m_bIntroActive = false;
    m_allCount = 0;
    for (unsigned suite = 0; suite < kSuiteCount; ++suite)
    {
        for (unsigned i = 0; i < kSuites[suite].count && m_allCount < kMaxAllResults; ++i)
        {
            m_allNames[m_allCount] = kSuites[suite].steps[i].name;
            m_allResults[m_allCount] = ResultPending;
            ++m_allCount;
        }
    }
    CTConfig *config = CTConfig::Get();
    if (config != nullptr)
    {
        m_savedSmoothScroll = config->GetSmoothScrollEnabled() ? TRUE : FALSE;
        m_hasSavedSmoothScroll = true;
        m_savedWrapAround = config->GetWrapAroundEnabled() ? TRUE : FALSE;
        m_hasSavedWrapAround = true;
        for (unsigned i = 0; i < CTConfig::TabStopsMax; ++i)
        {
            m_savedTabStops[i] = config->IsTabStop(i);
        }
        m_hasSavedTabStops = true;
    }
    StartSuite(0);
    if (m_pRenderer != nullptr)
    {
        m_pRenderer->SetBlinkingCursor(FALSE, 500);
        m_pRenderer->SetCursorMode(TRUE);
    }
}

void CVTTest::StartSuite(unsigned index)
{
    m_suiteIndex = index;
    m_suiteName = kSuites[index].name;
    m_steps = kSuites[index].steps;
    m_stepCount = kSuites[index].count;
    m_bAwaitNextSuite = false;
    m_bShowRulers = (index == 0);
    m_bTabLayout = false;
    m_bHoldClearScreen = false;
    m_bSummaryActive = false;
    m_pHoldStep = nullptr;
    m_bHoldCursorToggle = false;
    m_bScrollTestActive = false;
    m_scrollLineIndex = 0;
    m_scrollNextTick = 0;
    m_BoundaryTestMode = BoundaryTestNone;
    m_BoundaryCharIndex = 0;
    m_BoundaryNextTick = 0;
    m_BoundaryBellTriggered = false;
    m_BoundaryBellCol = 0;
    m_BoundaryRow = 0;
    m_BoundaryStartCol = 0;
    m_BoundaryChars[0] = '\0';
    m_bSequencePartsActive = false;
    m_sequenceParts = nullptr;
    m_sequencePartCount = 0;
    m_sequencePartIndex = 0;
    m_sequenceNextTick = 0;
    m_bShowPromptAfterSequence = false;
    m_bPendingResult = false;
    m_pendingResult = ResultPending;
    m_nStep = 0;
    m_nNextTick = 0;
    m_bWaitForKey = false;
    m_bKeyPressed = false;

    for (unsigned i = 0; i < m_stepCount; ++i)
    {
        m_TestResults[i] = ResultPending;
    }

    if (m_pRenderer != nullptr && index == 1)
    {
        m_pRenderer->ResetParserState();
        const unsigned rows = m_pRenderer->GetRows();
        CString clearSeq;
        clearSeq.Format("\x1B#5\x1B[0m\x1B[1;%ur\x1B[2J\x1B[H", rows > 0 ? rows : 1);
        m_pRenderer->Write(clearSeq.c_str(), clearSeq.GetLength());
    }
}

bool CVTTest::IsActive() const
{
    return m_bActive;
}

void CVTTest::Stop(void)
{
    m_bActive = false;
    m_bCompleted = true;
    m_bStopRequested = false;
    m_bSummaryActive = false;
    m_bAwaitNextSuite = false;
    m_bWaitForKey = false;
    m_bKeyPressed = false;
    m_bPendingResult = false;
    m_pendingResult = ResultPending;
    m_bIntroActive = false;
    m_nNextTick = 0;
    m_scrollNextTick = 0;
    m_sequenceNextTick = 0;
    m_bScrollTestActive = false;
    m_bSequencePartsActive = false;
    m_BoundaryTestMode = BoundaryTestNone;
    m_sequenceParts = nullptr;
    m_sequencePartCount = 0;
    m_sequencePartIndex = 0;
    m_bShowPromptAfterSequence = false;
    m_bHoldClearScreen = false;
    m_bHoldCursorToggle = false;
    m_pHoldStep = nullptr;

    CTConfig *config = CTConfig::Get();
    if (config != nullptr)
    {
        config->SetVTTestEnabled(FALSE);
    }

    if (m_pRenderer != nullptr)
    {
        // Reset permanent settings via escape sequences
        const unsigned rows = m_pRenderer->GetRows();
        if (rows > 0)
        {
            CString resetSeq;
            resetSeq.Format("\x1B[1;%ur\x1B[4l", rows);
            m_pRenderer->Write(resetSeq.c_str(), resetSeq.GetLength());
        }
        m_pRenderer->ResetParserState();
        if (config != nullptr)
        {
            if (m_hasSavedSmoothScroll)
            {
                config->SetSmoothScrollEnabled(m_savedSmoothScroll);
                m_pRenderer->SetSmoothScrollEnabled(m_savedSmoothScroll);
                m_hasSavedSmoothScroll = false;
            }
            if (m_hasSavedWrapAround)
            {
                config->SetWrapAroundEnabled(m_savedWrapAround);
                m_hasSavedWrapAround = false;
            }
            if (m_hasSavedTabStops)
            {
                for (unsigned i = 0; i < CTConfig::TabStopsMax; ++i)
                {
                    config->SetTabStop(i, m_savedTabStops[i]);
                }
                m_hasSavedTabStops = false;
            }
            m_pRenderer->SetCursorBlock(config->GetCursorBlock());
            m_pRenderer->SetBlinkingCursor(config->GetCursorBlinking(), 500);
        }
        m_pRenderer->SetCursorMode(TRUE);
    }
}

void CVTTest::Tick(void)
{
    CTConfig *config = CTConfig::Get();
    const bool enabled = (config != nullptr) ? config->GetVTTestEnabled() : false;

    if (!enabled)
    {
        if (m_bActive)
        {
            Stop();
        }
        m_bActive = false;
        m_bCompleted = false;
        m_bLastEnabled = false;
        return;
    }

    if (!m_bLastEnabled && enabled)
    {
        Start();
        ShowIntro();
        m_bIntroActive = true;
    }
    m_bLastEnabled = enabled;

    if (m_bStopRequested)
    {
        m_bStopRequested = false;
        if (m_pRenderer != nullptr)
        {
            m_pRenderer->ClearDisplay();
            m_pRenderer->Goto(0, 0);
        }
        Stop();
        return;
    }

    if (!m_bActive || m_pRenderer == nullptr)
    {
        return;
    }

    if (m_bIntroActive)
    {
        return;
    }

    if (m_nStep >= m_stepCount && !m_bSummaryActive)
    {
        ShowSummary();
        return;
    }

    unsigned now = CTimer::Get()->GetTicks();
    if (m_nNextTick != 0 && (int)(now - m_nNextTick) < 0)
    {
        return;
    }

    if (m_bScrollTestActive)
    {
        if (m_scrollNextTick != 0 && (int)(now - m_scrollNextTick) < 0)
        {
            return;
        }

        if (m_scrollLineIndex < kScrollLineCount)
        {
            m_pRenderer->Write(kScrollLines[m_scrollLineIndex], strlen(kScrollLines[m_scrollLineIndex]));
            m_pRenderer->Write("\n", len("\n"));
            m_scrollLineIndex++;
            m_scrollNextTick = now + MSEC2HZ(kScrollLineDelayMs);
            return;
        }

        m_bScrollTestActive = false;
        m_bWaitForKey = true;
        return;
    }

    if (m_BoundaryTestMode != BoundaryTestNone)
    {
        ServiceBoundaryAnimation(now);
        return;
    }

    if (m_bSequencePartsActive)
    {
        if (m_sequenceNextTick != 0 && (int)(now - m_sequenceNextTick) < 0)
        {
            return;
        }

        if (m_sequenceParts != nullptr && m_sequencePartIndex < m_sequencePartCount)
        {
            const char *part = m_sequenceParts[m_sequencePartIndex];
            m_pRenderer->Write(part, strlen(part));
            m_sequencePartIndex++;
            m_sequenceNextTick = now + MSEC2HZ(kSequencePartDelayMs);
            return;
        }

        const char **finishedParts = m_sequenceParts;
        m_bSequencePartsActive = false;
        m_sequenceParts = nullptr;
        m_sequencePartCount = 0;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = 0;

        if (finishedParts == kClearScreenParts)
        {
            m_bHoldClearScreen = true;
            // Add 3s to the 2s sequence delay for total 5s
            m_nNextTick = now + MSEC2HZ(3000);
            m_bWaitForKey = false;
            return;
        }

        if (m_bShowPromptAfterSequence)
        {
            ShowPrompt();
            m_bShowPromptAfterSequence = false;
        }
        m_bWaitForKey = true;
        return;
    }

    if (m_bHoldClearScreen && m_pHoldStep != nullptr)
    {
        // After the clear-screen hold, redraw the test frame and wait for confirmation
        DrawTestFrame(*m_pHoldStep);
        ShowPrompt();
        m_bHoldClearScreen = false;
        m_bWaitForKey = true;
        m_pHoldStep = nullptr;
        return;
    }

    if (m_bHoldCursorToggle)
    {
        // Re-enable cursor after hide interval
        m_pRenderer->ResetParserState();
        m_pRenderer->Write("\x1B[?25h", len("\x1B[?25h"));
        m_pRenderer->SetCursorMode(TRUE);
        m_pRenderer->SetBlinkingCursor(FALSE, 500);
        m_bHoldCursorToggle = false;
        m_bWaitForKey = true;
        return;
    }

    if (m_bWaitForKey && m_bPendingResult)
    {
        if (m_nStep < m_stepCount)
        {
            m_TestResults[m_nStep] = m_pendingResult;
            unsigned index = 0;
            for (unsigned i = 0; i < m_suiteIndex; ++i)
            {
                index += kSuites[i].count;
            }
            index += m_nStep;
            if (index < m_allCount)
            {
                m_allResults[index] = m_pendingResult;
            }
        }
        m_bPendingResult = false;
        m_pendingResult = ResultPending;
        m_bKeyPressed = true;
    }

    // Wait for key press before advancing to next test
    if (m_bWaitForKey) {
        if (m_bKeyPressed) {
            m_bKeyPressed = false;
            m_bWaitForKey = false;
            m_nStep++;
            if (m_nStep >= m_stepCount)
            {
                if (m_suiteIndex + 1 < kSuiteCount)
                {
                    StartSuite(m_suiteIndex + 1);
                    return;
                }
                ShowSummary();
                return;
            }
            m_nNextTick = now + MSEC2HZ(100); // short debounce
        }
        return;
    }

    if (m_nStep >= m_stepCount)
    {
        return;
    }

    const TVTTestStep &step = m_steps[m_nStep];
    // Otherwise, run normal test step, then wait for key
    RunStep(step);
    m_bWaitForKey = true;
    return;
}

bool CVTTest::OnKeyPress(const char *pString)
{
    if (m_bIntroActive && pString != nullptr)
    {
        for (const char *p = pString; *p != '\0'; ++p)
        {
            if (*p == '\r' || *p == '\n')
            {
                m_bIntroActive = false;
                m_bWaitForKey = false;
                m_bKeyPressed = false;
                return true;
            }
            if (*p == ' ')
            {
                m_bIntroActive = false;
                m_bStopRequested = true;
                return true;
            }
        }
    }

    if (m_bSummaryActive && pString != nullptr)
    {
        for (const char *p = pString; *p != '\0'; ++p)
        {
            if (*p == '\r' || *p == '\n')
            {
                m_bSummaryActive = false;
                if (m_bAwaitNextSuite)
                {
                    StartSuite(m_suiteIndex + 1);
                    return true;
                }
                m_bStopRequested = true;
                return true;
            }
        }
    }

    if (!m_bActive || !m_bWaitForKey || pString == nullptr)
    {
        if (m_bActive && !m_bWaitForKey && pString != nullptr)
        {
            for (const char *p = pString; *p != '\0'; ++p)
            {
                if (*p == '\r' || *p == '\n')
                {
                    m_pendingResult = ResultPass;
                    m_bPendingResult = true;
                    return true;
                }
                if (*p == ' ')
                {
                    m_pendingResult = ResultFail;
                    m_bPendingResult = true;
                    return true;
                }
            }
        }
        return false;
    }

    bool accepted = false;
    for (const char *p = pString; *p != '\0'; ++p)
    {
        if (*p == '\r' || *p == '\n')
        {
            if (m_nStep < m_stepCount)
            {
                m_TestResults[m_nStep] = ResultPass;
            }
            accepted = true;
            break;
        }
        if (*p == ' ')
        {
            if (m_nStep < m_stepCount)
            {
                m_TestResults[m_nStep] = ResultFail;
            }
            accepted = true;
            break;
        }
    }

    if (accepted)
    {
        if (m_nStep < m_stepCount)
        {
            unsigned index = 0;
            for (unsigned i = 0; i < m_suiteIndex; ++i)
            {
                index += kSuites[i].count;
            }
            index += m_nStep;
            if (index < m_allCount)
            {
                m_allResults[index] = m_TestResults[m_nStep];
            }
        }
        m_bKeyPressed = true;
    }

    return accepted;
}

void CVTTest::ShowIntro(void)
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    const unsigned rows = m_pRenderer->GetRows();
    CString clearSeq;
    clearSeq.Format("\x1B#5\x1B[0m\x1B[1;%ur\x1B[2J\x1B[H", rows > 0 ? rows : 1);
    m_pRenderer->ResetParserState();
    m_pRenderer->Write(clearSeq.c_str(), clearSeq.GetLength());

    const char *titleSeq = "\x1B[1;1H\x1B#3VT100 Internal Test\r\n\x1B[2;1H\x1B#4VT100 Internal Test\r\n\x1B#5";
    m_pRenderer->Write(titleSeq, strlen(titleSeq));
    m_pRenderer->ResetParserState();

    m_pRenderer->Goto(5, 0);
    m_pRenderer->Write("Press RETURN to start tests.", len("Press RETURN to start tests."));
    m_pRenderer->Goto(6, 0);
    m_pRenderer->Write("Press SPACE to skip tests.", len("Press SPACE to skip tests."));

    LOGNOTE("VT100 Internal Test: waiting for start/skip");
}

void CVTTest::RunStep(const TVTTestStep &step)
{
    bool savedRulers = m_bShowRulers;
    bool savedTabLayout = m_bTabLayout;
    const bool isTabTest = (step.name != nullptr && strstr(step.name, "Tab") != nullptr);
    const bool isWrapOnTest = (step.name != nullptr && strcmp(step.name, "Wrap Around ON") == 0);
    const bool isWrapOffTest = (step.name != nullptr && strcmp(step.name, "Wrap Around OFF") == 0);
    const bool isMarginBellTest = (step.name != nullptr && strcmp(step.name, "Margin Bell Right-8") == 0);
    const bool isDecLineAttr = (step.name != nullptr && strcmp(step.name, "DEC Line/Char Attributes") == 0);
    const bool isDecGraphics = (step.name != nullptr && strcmp(step.name, "DEC Special Graphics Set") == 0);
    if (isTabTest)
    {
        m_bShowRulers = true;
    }
    if (isMarginBellTest)
    {
        m_bShowRulers = false;
    }
    if (isWrapOnTest || isWrapOffTest)
    {
        m_bShowRulers = false;
    }
    m_bTabLayout = isTabTest;

    DrawTestFrame(step);
    const bool isClearScreen = (step.name != nullptr && strcmp(step.name, "ANSI Clear Screen") == 0);
    if (!isClearScreen && !isDecLineAttr && !isDecGraphics && !isWrapOnTest && !isWrapOffTest && !isMarginBellTest)
    {
        ShowPrompt();
    }
    m_bShowRulers = savedRulers;
    m_bTabLayout = savedTabLayout;

    if (isDecLineAttr)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kDecLineAttrParts;
        m_sequencePartCount = kDecLineAttrPartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks();
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (isDecGraphics)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kGraphicsFontParts;
        m_sequencePartCount = kGraphicsFontPartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks();
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (step.name != nullptr && strcmp(step.name, "ANSI Delete Chars") == 0)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kDeleteCharParts;
        m_sequencePartCount = kDeleteCharPartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kSequencePartDelayMs);
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (step.name != nullptr && strcmp(step.name, "ANSI Erase Chars") == 0)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kEraseCharParts;
        m_sequencePartCount = kEraseCharPartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kSequencePartDelayMs);
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (step.name != nullptr && strcmp(step.name, "ANSI Insert Lines") == 0)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kInsertLineParts;
        m_sequencePartCount = kInsertLinePartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kSequencePartDelayMs);
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (step.name != nullptr && strcmp(step.name, "ANSI Delete Lines") == 0)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kDeleteLineParts;
        m_sequencePartCount = kDeleteLinePartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kSequencePartDelayMs);
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        return;
    }

    if (step.name != nullptr && strcmp(step.name, "Custom Auto Page Mode") == 0)
    {
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kAutoPageParts;
        m_sequencePartCount = kAutoPagePartCount;
        m_sequencePartIndex = 0;
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kSequencePartDelayMs);
        m_bShowPromptAfterSequence = true;
        m_bWaitForKey = false;
        // Hint implies waiting for completion
        return;
    }
    else if (isMarginBellTest)
    {
        CTConfig *config = CTConfig::Get();
        if (config != nullptr)
        {
            config->SetMarginBellEnabled(TRUE);
            if (config->GetBuzzerVolume() == 0U)
            {
                config->SetBuzzerVolume(50U);
            }
        }

        StartBoundaryAnimation(false, true);
        return;
    }
    else if (isWrapOnTest || isWrapOffTest)
    {
        CTConfig *config = CTConfig::Get();
        if (config != nullptr)
        {
            config->SetWrapAroundEnabled(isWrapOnTest ? TRUE : FALSE);
        }

        StartBoundaryAnimation(isWrapOnTest, false);
        return;
    }
    else if (step.sequence != nullptr && step.sequence[0] != '\0') {
        m_pRenderer->ResetParserState();
        // Use escape sequences only for positioning in tests
        m_pRenderer->Write(step.sequence, strlen(step.sequence));

        if (strcmp(step.name, "DEC Cursor Visible") == 0)
        {
            m_bHoldCursorToggle = true;
            m_nNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kCursorHideMs);
            m_bWaitForKey = false;
        }

        if (strcmp(step.name, "DEC Scroll Region") == 0
            || strcmp(step.name, "Smooth Scroll ON Demo") == 0
            || strcmp(step.name, "Smooth Scroll OFF Demo") == 0)
        {
            CTConfig *config = CTConfig::Get();
            if (strcmp(step.name, "Smooth Scroll ON Demo") == 0)
            {
                if (config != nullptr)
                {
                    config->SetSmoothScrollEnabled(TRUE);
                }
                m_pRenderer->SetSmoothScrollEnabled(TRUE);
            }
            else if (strcmp(step.name, "Smooth Scroll OFF Demo") == 0)
            {
                if (config != nullptr)
                {
                    config->SetSmoothScrollEnabled(FALSE);
                }
                m_pRenderer->SetSmoothScrollEnabled(FALSE);
            }

            m_bScrollTestActive = true;
            m_scrollLineIndex = 0;
            m_scrollNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kScrollLineDelayMs);
            m_bWaitForKey = false;
        }
    }
    else if (isClearScreen)
    {
        // Hold full clear screen for 5 seconds, then redraw test frame
        // Use sequence logic to impose pre-delay so user can read instructions
        m_pRenderer->ResetParserState();
        m_bSequencePartsActive = true;
        m_sequenceParts = kClearScreenParts;
        m_sequencePartCount = kClearScreenPartCount;
        m_sequencePartIndex = 0;
        // Wait 3 seconds before clearing so user can read the test frame
        m_sequenceNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(3000);
        
        m_pHoldStep = &step;
        m_bShowPromptAfterSequence = false;
        m_bWaitForKey = false;
    }
}

void CVTTest::StartBoundaryAnimation(bool wrapAroundEnabled, bool marginBellMode)
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    const unsigned rows = m_pRenderer->GetRows();
    const unsigned cols = m_pRenderer->GetColumns();
    if (rows == 0 || cols == 0)
    {
        return;
    }

    unsigned markerRow = 6;
    unsigned testRow = 7;
    unsigned nextRow = 8;
    if (rows <= 9)
    {
        markerRow = rows > 3 ? rows - 3 : 0;
        testRow = rows > 2 ? rows - 2 : 0;
        nextRow = rows > 1 ? rows - 1 : 0;
    }

    const unsigned eolCol = cols - 1U;
    const unsigned bellCol = (cols > 8U) ? (cols - 8U) : 0U;
    const char *baseText = marginBellMode ? "BELL>>" : "WRAP>>";
    const unsigned baseLen = strlen(baseText);
    const unsigned gapToBoundary = marginBellMode ? 4U : 2U;
    const unsigned boundaryCol = marginBellMode ? bellCol : eolCol;
    const unsigned totalOffset = baseLen + gapToBoundary;
    const unsigned startCol = (boundaryCol > totalOffset) ? (boundaryCol - totalOffset) : 0U;

    m_pRenderer->ResetParserState();

    m_pRenderer->Goto(markerRow, 0);
    m_pRenderer->Write("\x1B[K", len("\x1B[K"));
    m_pRenderer->Goto(testRow, 0);
    m_pRenderer->Write("\x1B[K", len("\x1B[K"));
    if (nextRow != testRow)
    {
        m_pRenderer->Goto(nextRow, 0);
        m_pRenderer->Write("\x1B[K", len("\x1B[K"));
    }

    if (marginBellMode)
    {
        unsigned markerTextCol = (bellCol > 12U) ? (bellCol - 12U) : 0U;
        m_pRenderer->Goto(markerRow, markerTextCol);
        m_pRenderer->Write("BELL-MARGIN", len("BELL-MARGIN"));
        m_pRenderer->Goto(markerRow, bellCol);
        m_pRenderer->Write("!", 1);
    }
    else
    {
        unsigned markerTextCol = (eolCol > 9U) ? (eolCol - 9U) : 0U;
        m_pRenderer->Goto(markerRow, markerTextCol);
        m_pRenderer->Write("LINE-END", len("LINE-END"));
        m_pRenderer->Goto(markerRow, eolCol);
        m_pRenderer->Write("|", 1);

        if (nextRow != testRow)
        {
            unsigned nextMarkerCol = (eolCol > 10U) ? (eolCol - 10U) : 0U;
            m_pRenderer->Goto(nextRow, nextMarkerCol);
            m_pRenderer->Write("NEXT-LINE", len("NEXT-LINE"));
        }
    }

    m_pRenderer->Goto(testRow, startCol);
    m_pRenderer->Write(baseText, baseLen);

    strncpy(m_BoundaryChars, "1234567890ABC", sizeof(m_BoundaryChars) - 1);
    m_BoundaryChars[sizeof(m_BoundaryChars) - 1] = '\0';

    m_BoundaryTestMode = marginBellMode
        ? BoundaryTestMarginBell
        : (wrapAroundEnabled ? BoundaryTestWrapOn : BoundaryTestWrapOff);
    m_BoundaryRow = testRow;
    m_BoundaryStartCol = startCol + baseLen;
    m_BoundaryCharIndex = 0;
    m_BoundaryNextTick = CTimer::Get()->GetTicks() + MSEC2HZ(kBoundaryCharDelayMs);
    m_BoundaryBellCol = bellCol;
    m_BoundaryBellTriggered = false;
    m_bWaitForKey = false;
}

void CVTTest::ServiceBoundaryAnimation(unsigned nowTicks)
{
    if (m_pRenderer == nullptr || m_BoundaryTestMode == BoundaryTestNone)
    {
        return;
    }

    if (m_BoundaryNextTick != 0 && (int)(nowTicks - m_BoundaryNextTick) < 0)
    {
        return;
    }

    const unsigned totalChars = strlen(m_BoundaryChars);
    if (m_BoundaryCharIndex < totalChars)
    {
        const unsigned currentCol = m_BoundaryStartCol + m_BoundaryCharIndex;
        if (m_BoundaryTestMode == BoundaryTestMarginBell && !m_BoundaryBellTriggered && currentCol >= m_BoundaryBellCol)
        {
            CHAL *hal = CHAL::Get();
            if (hal != nullptr)
            {
                hal->BEEP();
            }
            m_BoundaryBellTriggered = true;
        }

        const char ch = m_BoundaryChars[m_BoundaryCharIndex];
        m_pRenderer->Write(&ch, 1);
        ++m_BoundaryCharIndex;
        m_BoundaryNextTick = nowTicks + MSEC2HZ(kBoundaryCharDelayMs);
        return;
    }

    m_BoundaryTestMode = BoundaryTestNone;
    m_BoundaryNextTick = 0;
    ShowPrompt();
    m_bWaitForKey = true;
}

void CVTTest::ShowPrompt(void)
{
    unsigned rows = m_pRenderer->GetRows();
    unsigned promptLine = rows > 0 ? rows : 24;
    m_pRenderer->Goto(promptLine, m_bShowRulers ? 10 : 0);
    m_pRenderer->Write("Confirm: ENTER=PASS  SPACE=FAIL", len("Confirm: ENTER=PASS  SPACE=FAIL"));
}

void CVTTest::ShowSummary(void)
{
    // Reset parser and force Normal font (10x20) so GetRows returns max capacity
    m_pRenderer->ResetParserState();
    m_pRenderer->Write("\x1B#5", 3);

    // Reset line/char attributes and clear screen before summary
    const unsigned rows = m_pRenderer->GetRows();
    CString clearSeq;
    clearSeq.Format("\x1B#5\x1B[0m\x1B[1;%ur\x1B[2J\x1B[H", rows > 0 ? rows : 1);
    m_pRenderer->Write(clearSeq.c_str(), clearSeq.GetLength());

    // Simple single-height title to save vertical space
    const unsigned dRows = m_pRenderer->GetRows();
    const unsigned dCols = m_pRenderer->GetColumns();
    const unsigned dHeight = m_pRenderer->GetHeight();
    
    // Explicitly title with debug info
    CString titleMsg;
    titleMsg.Format("VT100 Internal Test Summary (R:%u C:%u H:%u Items:%u)", dRows, dCols, dHeight, m_allCount);
    
    // Draw Title in Double Width Double Height
    // #3 Draws the characters 2x size (height covers 2 rows).
    // #4 Is a dummy line to skip the space covered by the bottom of the letters.
    
    // Line 1: Top Half (Visual)
    m_pRenderer->Write("\x1B[1;1H\x1B#3", 7);
    m_pRenderer->Write(titleMsg.c_str(), titleMsg.GetLength());
    m_pRenderer->Write("\r\n", 2);
    
    // Line 2: Bottom Half (Spacer)
    m_pRenderer->Write("\x1B#4", 3);
    m_pRenderer->Write(titleMsg.c_str(), titleMsg.GetLength());
    m_pRenderer->Write("\r\n", 2);

    // Reset to Normal for content
    m_pRenderer->Write("\x1B#5", 3);
    m_pRenderer->ResetParserState();

    LOGNOTE("VT100 Internal Test Summary - R:%u C:%u H:%u", dRows, dCols, dHeight);

    unsigned passCount = 0;
    unsigned failCount = 0;
    for (unsigned i = 0; i < m_allCount; ++i)
    {
        if (m_allResults[i] == ResultPass)
            ++passCount;
        else if (m_allResults[i] == ResultFail)
            ++failCount;
    }

    unsigned maxNameLen = 0;
    for (unsigned i = 0; i < m_allCount; ++i)
    {
        unsigned nameLen = m_allNames[i] ? static_cast<unsigned>(strlen(m_allNames[i])) : 0;
        if (nameLen > maxNameLen)
            maxNameLen = nameLen;
    }

    // Start listing at line 4 to accommodate DWDH title
    unsigned line = 4;

    const unsigned columns = m_pRenderer->GetColumns();
    const unsigned colWidth = columns > 1 ? columns / 2 : columns;
    
    // Calculate split across two columns
    // Force display of all items even if they might overflow (renderer clamps)
    const unsigned displayCount = m_allCount;
    
    // Split: put more on the left if odd
    // But importantly, ensure we use all available rows
    const unsigned leftCount = (displayCount + 1) / 2;
    const unsigned rightCount = displayCount - leftCount;

    for (unsigned i = 0; i < leftCount; ++i)
    {
        const unsigned indexValue = i + 1;
        const char *status = "PENDING";
        if (m_allResults[i] == ResultPass)
            status = "PASS";
        else if (m_allResults[i] == ResultFail)
            status = "FAIL";

        CString entry;
        const char *name = m_allNames[i] ? m_allNames[i] : "";
        unsigned nameLen = static_cast<unsigned>(strlen(name));
        if (indexValue < 10)
        {
            entry.Append(" ");
        }
        CString index;
        index.Format("%u) ", indexValue);
        entry.Append(index);
        entry.Append(name);
        if (maxNameLen > nameLen)
        {
            unsigned pad = maxNameLen - nameLen;
            for (unsigned p = 0; p < pad; ++p)
            {
                entry.Append(" ");
            }
        }
        entry.Append(" [");
        entry.Append(status);
        entry.Append("]");
        m_pRenderer->Goto(line + i, 0);
        m_pRenderer->Write(entry.c_str(), entry.GetLength());
        LOGNOTE("%s", entry.c_str());
    }

    for (unsigned i = 0; i < rightCount; ++i)
    {
        const unsigned indexValue = leftCount + i + 1;
        const unsigned resultIndex = leftCount + i;
        const char *status = "PENDING";
        if (m_allResults[resultIndex] == ResultPass)
            status = "PASS";
        else if (m_allResults[resultIndex] == ResultFail)
            status = "FAIL";

        CString entry;
        const char *name = m_allNames[resultIndex] ? m_allNames[resultIndex] : "";
        unsigned nameLen = static_cast<unsigned>(strlen(name));
        if (indexValue < 10)
        {
            entry.Append(" ");
        }
        CString index;
        index.Format("%u) ", indexValue);
        entry.Append(index);
        entry.Append(name);
        if (maxNameLen > nameLen)
        {
            unsigned pad = maxNameLen - nameLen;
            for (unsigned p = 0; p < pad; ++p)
            {
                entry.Append(" ");
            }
        }
        entry.Append(" [");
        entry.Append(status);
        entry.Append("]");
        m_pRenderer->Goto(line + i, colWidth);
        m_pRenderer->Write(entry.c_str(), entry.GetLength());
        LOGNOTE("%s", entry.c_str());
    }

    // Position summary at the bottom
    unsigned summaryLine = rows > 1 ? rows - 1 : line + leftCount;
    if (summaryLine < line + leftCount && summaryLine >= rows) summaryLine = rows - 1; // Adjust if overlapping
    if (summaryLine < line + leftCount) summaryLine = line + leftCount; // Ensure it's below list if space allows or scroll...
    // Actually just force bottom of screen
    summaryLine = rows > 0 ? rows - 1 : 0; 
    
    // Clear the summary line area just in case
    m_pRenderer->Goto(summaryLine, 0);
    m_pRenderer->Write("\x1B[K", len("\x1B[K"));

    CString summary;
    summary.Format("Summary: %u total, %u passed, %u failed", m_allCount, passCount, failCount);
    
    m_pRenderer->Goto(summaryLine, 0);
    CString boldSummary;
    boldSummary.Format("\x1B[1m%s\x1B[0m", summary.c_str());
    m_pRenderer->Write(boldSummary.c_str(), boldSummary.GetLength());
    LOGNOTE("%s", summary.c_str());

    m_bSummaryActive = true;
    m_bWaitForKey = false;
    m_bKeyPressed = false;
}

void CVTTest::LogSummary(void)
{
    unsigned passCount = 0;
    unsigned failCount = 0;
    for (unsigned i = 0; i < m_allCount; ++i)
    {
        if (m_allResults[i] == ResultPass)
            ++passCount;
        else if (m_allResults[i] == ResultFail)
            ++failCount;
    }

    LOGNOTE("VTTest Summary: %u total, %u passed, %u failed", m_allCount, passCount, failCount);

    for (unsigned i = 0; i < m_allCount; ++i)
    {
        const char *status = "PENDING";
        if (m_allResults[i] == ResultPass)
            status = "PASS";
        else if (m_allResults[i] == ResultFail)
            status = "FAIL";

        LOGNOTE("VTTest %u/%u: %s [%s]", i + 1, m_allCount, m_allNames[i], status);
    }

    if (failCount > 0)
    {
        for (unsigned i = 0; i < m_allCount; ++i)
        {
            if (m_allResults[i] == ResultFail)
            {
                LOGNOTE("VTTest FAIL %u/%u: %s", i + 1, m_allCount, m_allNames[i]);
            }
        }
    }
}

void CVTTest::DrawTestFrame(const TVTTestStep &step)
{
    // Use renderer's ClearDisplay to clear screen and home cursor
    m_pRenderer->ClearDisplay();

    if (m_bShowRulers)
    {
        // Draw horizontal ruler (column numbers)
        unsigned cols = m_pRenderer->GetColumns();
        CString hRuler;
        for (unsigned c = 0; c < cols; ++c) {
            char digit[2] = { static_cast<char>('0' + (c % 10)), '\0' };
            hRuler.Append(digit);
        }
        m_pRenderer->Goto(0, 0);
        m_pRenderer->Write(hRuler.c_str(), hRuler.GetLength());

        if (!m_bTabLayout)
        {
            // Draw vertical ruler (row numbers)
            unsigned rows = m_pRenderer->GetRows();
            for (unsigned r = 1; r < rows; ++r) {
                CString vRuler;
                vRuler.Format("%02d", r + 1);
                m_pRenderer->Goto(r, 0);
                m_pRenderer->Write(vRuler.c_str(), vRuler.GetLength());
            }
        }
    }

    // Print test explanation at the top (line 1)
    const unsigned explLine = m_bTabLayout ? 4 : 1;
    unsigned hintLine = explLine + 1;
    if (step.name != nullptr && strcmp(step.name, "ANSI Erase Chars") == 0)
    {
        hintLine = explLine + 1;
    }
    m_pRenderer->Goto(explLine, m_bShowRulers ? 10 : 0);
    CString expl;
    expl.Format("VTTest %u/%u: %s", m_nStep + 1, m_stepCount, step.name);
    m_pRenderer->Write(expl.c_str(), expl.GetLength());
    if (step.hint != nullptr) {
        m_pRenderer->Goto(hintLine, m_bShowRulers ? 10 : 0);
        m_pRenderer->Write(step.hint, strlen(step.hint));
    }
}

void CVTTest::ShowHint(const TVTTestStep &step, bool pass)
{
    CString line;
    line.Format("VTTest %u/%u: %s [%s]", m_nStep + 1, m_stepCount, step.name, pass ? "PASS" : "CHECK");

    const unsigned rows = m_pRenderer->GetRows();
    if (rows > 0)
    {
        if (rows >= 2)
        {
            m_pRenderer->Goto(rows - 2, 0);
            m_pRenderer->Write("\x1B[K", len("\x1B[K"));
            m_pRenderer->Write(line.c_str(), line.GetLength());

            if (step.hint != nullptr)
            {
                m_pRenderer->Goto(rows - 1, 0);
                m_pRenderer->Write("\x1B[K", len("\x1B[K"));
                m_pRenderer->Write(step.hint, strlen(step.hint));
            }
        }
        else
        {
            m_pRenderer->Goto(rows - 1, 0);
            m_pRenderer->Write("\x1B[K", len("\x1B[K"));
            m_pRenderer->Write(line.c_str(), line.GetLength());
        }
    }
}
