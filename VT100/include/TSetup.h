//------------------------------------------------------------------------------
// Module:        CTSetup
// Description:   Handles the VT100 setup dialog overlay.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-02-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#pragma once

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
        ModernFieldRepeatDelay,
        ModernFieldRepeatRate,
        ModernFieldSwitchTxRx,
        ModernFieldWlanHostAutoStart,
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
        unsigned int repeatDelayMs;
        unsigned int repeatRateCps;
        bool switchTxRx;
        unsigned int wlanModePolicy;
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

    void Render();
    void RenderPageA();
    void RenderPageB();
    void RenderHeader(const char *pTitle, unsigned topRow);
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

    static void KeyPressedHandler(const char *pString);
    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);

    void OnKeyPressed(const char *pString);
    void OnRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6]);

private:
    CTRenderer *m_pRenderer;
    CTConfig *m_pConfig;
    CTKeyboard *m_pKeyboard;
    CTKeyboard::TKeyPressedHandler m_pPrevKeyPressed;
    CTKeyboard::TKeyStatusHandlerRaw m_pPrevKeyStatusRaw;
    struct TSetupSnapshot
    {
        u8 *buffer;
        size_t size;
        bool valid;
        bool stateValid;
        CTRenderer::TRendererState rendererState;
    };
    TSetupSnapshot m_Snapshot;
    bool m_Visible;
    bool m_ExitRequested;
    bool m_SaveRequested;
    bool m_KeyPending;
    bool m_F12Down;
    bool m_F11Down;
    char m_KeyBuffer[32];
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
    bool m_ModernLayoutValid;
    TModernLayoutState m_ModernLayout;

};
