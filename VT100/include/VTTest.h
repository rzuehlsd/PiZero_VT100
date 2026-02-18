//------------------------------------------------------------------------------
// Module:        VTTest
// Description:   Simple VT100 test runner for renderer validation.
// Author:        R. Zuehlsdorff
// Created:       2026-02-09
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#pragma once

#include <circle/types.h>

#include "TConfig.h"

class CTRenderer;

class CVTTest
{
public:
    /// \brief Single test step definition used by suites.
    struct TVTTestStep
    {
        /// \brief Human-readable step name shown in the UI/log.
        const char *name;
        /// \brief Escape sequence payload executed for this step (may be empty for multi-part steps).
        const char *sequence;
        /// \brief Guidance text displayed during the step.
        const char *hint;
        /// \brief Optional expected row (0-based) for programmatic validation (unused).
        int expectedRow;
        /// \brief Optional expected column (0-based) for programmatic validation (unused).
        int expectedCol;
    };

    CVTTest(void);

    /// \brief Attach the renderer used for test output.
    void Initialize(CTRenderer *pRenderer);

    /// \brief Periodic tick invoked from the kernel periodic task.
    /// \note Drives step sequencing, delays, scroll tests, and summary rendering.
    void Tick(void);

    /// \brief Notify test runner about a key press for manual confirmation.
    /// \note ENTER=PASS, SPACE=FAIL. Keys pressed during timed steps are buffered
    ///       and applied once the test reaches the wait state.
    bool OnKeyPress(const char *pString);

    /// \brief Return whether VTTest is currently active and processing input.
    bool IsActive() const;

    static constexpr unsigned kMaxSteps = 64;

private:
    void Start(void);
    void StartSuite(unsigned index);
    void Stop(void);
    void RunStep(const TVTTestStep &step);
    void ShowHint(const TVTTestStep &step, bool pass);
    void ShowPrompt(void);
    void ShowIntro(void);
    void ShowSummary(void);
    void LogSummary(void);
    void DrawTestFrame(const TVTTestStep &step);
    void StartBoundaryAnimation(bool wrapAroundEnabled, bool marginBellMode);
    void ServiceBoundaryAnimation(unsigned nowTicks);

    enum TTestResult
    {
        ResultPending = 0,
        ResultPass,
        ResultFail
    };

    enum TBoundaryTestMode
    {
        BoundaryTestNone = 0,
        BoundaryTestWrapOn,
        BoundaryTestWrapOff,
        BoundaryTestMarginBell
    };

    CTRenderer *m_pRenderer;
    bool m_bActive;
    bool m_bCompleted;
    bool m_bLastEnabled;
    bool m_bStopRequested = false;
    unsigned m_nStep;
    unsigned m_nNextTick;
    /// \brief When true, waits for ENTER/SPACE to resolve current test.
    bool m_bWaitForKey;
    bool m_bKeyPressed = false;
    bool m_bHoldClearScreen = false;
    bool m_bSummaryActive = false;
    bool m_bIntroActive = false;
    const TVTTestStep *m_pHoldStep = nullptr;
    bool m_bHoldCursorToggle = false;

    /// \brief Current suite index (core or DEC).
    unsigned m_suiteIndex = 0;
    const char *m_suiteName = nullptr;
    const TVTTestStep *m_steps = nullptr;
    unsigned m_stepCount = 0;
    bool m_bAwaitNextSuite = false;
    bool m_bShowRulers = true;
    bool m_bTabLayout = false;

    /// \brief Flat results table spanning all suites for summary/logging.
    static constexpr unsigned kMaxAllResults = 128;
    const char *m_allNames[kMaxAllResults];
    TTestResult m_allResults[kMaxAllResults];
    unsigned m_allCount = 0;

    bool m_bScrollTestActive = false;
    unsigned m_scrollLineIndex = 0;
    unsigned m_scrollNextTick = 0;

    TBoundaryTestMode m_BoundaryTestMode = BoundaryTestNone;
    unsigned m_BoundaryRow = 0;
    unsigned m_BoundaryStartCol = 0;
    unsigned m_BoundaryCharIndex = 0;
    unsigned m_BoundaryNextTick = 0;
    unsigned m_BoundaryBellCol = 0;
    bool m_BoundaryBellTriggered = false;
    char m_BoundaryChars[24]{};

    /// \brief Multi-part sequence playback (used for DEC line/char attributes).
    bool m_bSequencePartsActive = false;
    const char **m_sequenceParts = nullptr;
    unsigned m_sequencePartCount = 0;
    unsigned m_sequencePartIndex = 0;
    unsigned m_sequenceNextTick = 0;
    bool m_bShowPromptAfterSequence = false;

    /// \brief Buffered confirmation when keys arrive during timed steps.
    bool m_bPendingResult = false;
    TTestResult m_pendingResult = ResultPending;

    bool m_hasSavedTabStops = false;
    bool m_savedTabStops[CTConfig::TabStopsMax]{};
    bool m_hasSavedSmoothScroll = false;
    boolean m_savedSmoothScroll = TRUE;
    bool m_hasSavedWrapAround = false;
    boolean m_savedWrapAround = TRUE;

    TTestResult m_TestResults[kMaxSteps];
};
