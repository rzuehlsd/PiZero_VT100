//------------------------------------------------------------------------------
// Module:        CTSetup
// Description:   Handles the VT100 setup dialog overlay.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-02-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#pragma once

#include <circle/spinlock.h>
#include <circle/types.h>
#include <circle/sched/task.h>
#include <stddef.h>

#include "TRenderer.h"
#include "TKeyboard.h"

class CTConfig;

class CTSetup : public CTask
{
public:
    /// \brief Access the singleton setup dialog instance.
    static CTSetup *Get(void);

    CTSetup();

    bool Initialize(CTRenderer *renderer, CTConfig *config, CTKeyboard *keyboard);

    void Toggle();
    void Show();
    void ShowModern();
    void Hide();
    bool IsVisible() const;
    /// \brief Queue cooked key input for the visible dialog.
    /// \details The keyboard callback only captures the latest pending input.
    /// Rendering and state changes are deferred to Run() so dialog redraws stay
    /// in task context even while WLAN or shell-client tasks are active.
    void HandleVisibleKeyPressed(const char *pString);
    /// \brief Queue raw key status for the visible dialog.
    void HandleVisibleRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6]);

    void Run(void) override;

private:
    enum TSetupPage
    {
        SetupPageA,
        SetupPageB
    };

    enum TSetupBField
    {
        SetupBFieldToggle1,
        SetupBFieldToggle2,
        SetupBFieldToggle3,
        SetupBFieldToggle4,
        SetupBFieldTxSpeed,
        SetupBFieldRxSpeed,
        SetupBFieldCount
    };

    enum TDialogMode
    {
        DialogModeLegacy,
        DialogModeModern
    };

    enum TModernField
    {
        ModernFieldLineEnding,
        ModernFieldBaudRate,
        ModernFieldSerialBits,
        ModernFieldSerialParity,
        ModernFieldCursorType,
        ModernFieldCursorBlinking,
        ModernFieldVTTest,
        ModernFieldVT52Mode,
        ModernFieldFontSelection,
        ModernFieldTextColor,
        ModernFieldBackgroundColor,
        ModernFieldBuzzerVolume,
        ModernFieldKeyClick,
        ModernFieldKeyAutoRepeat,
        ModernFieldSmoothScrollEnabled,
        ModernFieldSmoothScrollLineMs,
        ModernFieldRepeatDelay,
        ModernFieldRepeatRate,
        ModernFieldSwitchTxRx,
        ModernFieldSbcPower,
        ModernFieldWlanHostAutoStart,
        ModernFieldHostId,
        ModernFieldLogOutput,
        ModernFieldLogFileName,
        ModernFieldCount
    };

    struct TModernConfigState
    {
        unsigned int lineEnding;
        unsigned int baudRate;
        unsigned int serialBits;
        unsigned int serialParity;
        bool cursorBlock;
        bool cursorBlinking;
        bool vtTestEnabled;
        bool vt52Mode;
        EFontSelection fontSelection;
        EColorSelection textColor;
        EColorSelection backgroundColor;
        unsigned int buzzerVolume;
        bool keyClick;
        bool keyAutoRepeat;
        bool smoothScrollEnabled;
        unsigned int smoothScrollLineMs;
        unsigned int repeatDelayMs;
        unsigned int repeatRateCps;
        bool switchTxRx;
        bool sbcPower;
        unsigned int wlanModePolicy;
        char hostId[64];
        unsigned int logOutput;
        char logFileName[64];
    };

    struct TModernLayoutState
    {
        unsigned rows;
        unsigned cols;
        unsigned top;
        unsigned left;
        unsigned width;
        unsigned bottom;
        unsigned innerWidth;
        unsigned dataStartRow;
        unsigned footerRow;
        unsigned availableRows;
        unsigned startIndex;
    };

    enum TPendingInputType
    {
        PendingInputNone,
        PendingInputKey,
        PendingInputRaw
    };

    struct TPendingInputEvent
    {
        TPendingInputType type;
        char key[32];
        unsigned char modifiers;
        unsigned char rawKeys[6];
    };

    void Render();
    void RenderPageA();
    void RenderPageB();
    void RenderHeader(const char *pTitle, unsigned topRow, unsigned subtitleRowOffset = 2, bool clearBottomPixelRow = false);
    bool PrepareToShow();
    void NormalizeRenderState(bool graphicsInG1);
    void InitializeSetupBFromConfig();
    void ApplySetupBToConfig();
    void MoveSetupBFieldLeft();
    void MoveSetupBFieldRight();
    void ToggleSetupBFieldBit(bool setOne);
    void ChangeSetupBSpeed(bool increase);
    void GetSetupBSpeedFieldPosition(TSetupBField field, unsigned &row, unsigned &col) const;
    void UpdateTabCursor();
    void UpdateTabCell();
    void InitializeModernFromConfig();
    void ApplyModernToConfig();
    void RenderModernDialog();
    bool ComputeModernLayout(TModernLayoutState &layout) const;
    void RenderModernFieldRow(const TModernLayoutState &layout, unsigned fieldIndex, bool selected, const TRendererColor &fgColor, const TRendererColor &bgColor);
    void RenderModernFieldRows(const TModernLayoutState &layout, const TRendererColor &fgColor, const TRendererColor &bgColor);
    bool RenderModernSelectionDelta(TModernField previousSelected, unsigned previousStartIndex);
    bool RenderModernValueDelta();
    void HandleModernKeyPress(const char *pString);
    void MoveModernSelection(int delta);
    void ChangeModernValue(int delta);
    void FormatModernValue(TModernField field, char *pBuffer, size_t bufferSize) const;
    bool HandleModernTextEdit(const char *pString);
    void ProcessQueuedKeyPressed(const char *pString);
    void ProcessQueuedRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    void EnqueuePendingKey(const char *pString);
    void EnqueuePendingRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    bool DequeuePendingInput(TPendingInputEvent &event);
    void ResetPendingInputQueue();

    void OnKeyPressed(const char *pString);
    void OnRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6]);

private:
    CTRenderer *m_pRenderer;
    CTConfig *m_pConfig;
    CTKeyboard *m_pKeyboard;
    struct TSetupSnapshot
    {
        bool stateValid;
        CTRenderer::TRendererState rendererState;
    };
    TSetupSnapshot m_Snapshot;
    bool m_Visible;
    bool m_TaskStarted;
    bool m_ExitRequested;
    bool m_SaveRequested;
    bool m_F12Down;
    bool m_F11Down;
    // Protects the pending input FIFO shared by keyboard callbacks and Run().
    CSpinLock m_PendingInputLock;
    static const unsigned PendingInputQueueSize = 16;
    TPendingInputEvent m_PendingInputQueue[PendingInputQueueSize];
    unsigned m_PendingInputReadIndex;
    unsigned m_PendingInputWriteIndex;
    unsigned m_PendingInputCount;
    TDialogMode m_DialogMode;
    TSetupPage m_Page;
    unsigned m_SetupBToggle[4];
    unsigned m_SetupBTxSpeed;
    unsigned m_SetupBRxSpeed;
    TSetupBField m_SetupBField;
    unsigned m_SetupBBitIndex;
    unsigned m_TabRow;
    unsigned m_TabCols;
    unsigned m_TabEditCol;
    TModernField m_ModernSelected;
    TModernConfigState m_ModernConfig;
    bool m_ModernHostIdOverwriteOnEdit;
    bool m_ModernLayoutValid;
    TModernLayoutState m_ModernLayout;
};
