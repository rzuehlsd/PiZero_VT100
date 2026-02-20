//------------------------------------------------------------------------------
// Module:        CTSetup
// Description:   Handles the VT100 setup dialog overlay.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-02-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------

#include "TSetup.h"

#include <circle/string.h>
#include <circle/sched/scheduler.h>
#include <string.h>

#include "TRenderer.h"
#include "TConfig.h"
#include "kernel.h"

namespace
{
static const unsigned kBaudRates[] = {
    50, 75, 110, 134, 150, 300, 600, 1200, 1800, 2400, 4800,
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const unsigned kBaudRateCount = sizeof(kBaudRates) / sizeof(kBaudRates[0]);
static const char *kColorNames[] = {"Black", "White", "Amber", "Green"};
static const char *kLineEndingNames[] = {"LF", "CRLF", "CR"};
static const char *kParityNames[] = {"None", "Even", "Odd"};
static const char *kFontNames[] = {"8x20", "10x20 CRT", "10x20 Solid"};
static const char *kLogOutputNames[] = {
    "None",
    "Screen",
    "File",
    "WLAN",
    "Screen+File",
    "Screen+WLAN",
    "File+WLAN",
    "Screen+File+WLAN"};
static const char *kWlanModeNames[] = {
    "Off",
    "Log",
    "Host"};
static const char *kPresetLogFiles[] = {"vt100.log", "session.log", "terminal.log", "serial.log"};
static const unsigned kPresetLogFileCount = sizeof(kPresetLogFiles) / sizeof(kPresetLogFiles[0]);
constexpr unsigned int kRepeatDelayMinMs = 250U;
constexpr unsigned int kRepeatDelayMaxMs = 1000U;
constexpr unsigned int kRepeatRateMinCps = 2U;
constexpr unsigned int kRepeatRateMaxCps = 20U;
constexpr unsigned int kModernDialogMinRows = 12U;
constexpr unsigned int kModernDialogMinCols = 72U;
constexpr unsigned int kModernRowBufferSize = 192U;
constexpr unsigned int kModernFieldCount = 20U;

static const char *kModernFieldNames[kModernFieldCount] = {
    "line_ending",
    "baud_rate",
    "serial_bits",
    "serial_parity",
    "cursor_type",
    "cursor_blinking",
    "vt_test",
    "vt52_mode",
    "font_selection",
    "text_color",
    "background_color",
    "buzzer_volume",
    "key_click",
    "key_auto_repeat",
    "repeat_delay_ms",
    "repeat_rate_cps",
    "switch_txrx",
    "wlan_host_autostart",
    "log_output",
    "log_filename"};

static const char *kModernFieldDescriptions[kModernFieldCount] = {
    "Line ending: LF/CRLF/CR",
    "Baud rate 300-115200 (default 115200)",
    "Data bits: 7 or 8 (default 8)",
    "Parity: none/even/odd (default none)",
    "Cursor: underline/block",
    "Cursor blink on/off",
    "Enable VT test runner",
    "Mode: ANSI or VT52",
    "Font: 8x20/10x20/10x20Solid",
    "Text color: black/white/amber/green (default white)",
    "Background: black/white/amber/green (default black)",
    "Buzzer volume 0-100%",
    "Key click on/off",
    "Auto-repeat on/off",
    "Repeat delay 250-1000 ms",
    "Repeat rate 2-20 cps",
    "Swap UART TX/RX",
    "WLAN mode: Off/Log/Host",
    "Log outputs bitmask: bit1=screen, bit2=file, bit3=wlan",
    "Log file name"};

static unsigned FindBaudIndex(unsigned value)
{
    for (unsigned i = 0; i < kBaudRateCount; ++i)
    {
        if (kBaudRates[i] == value)
        {
            return i;
        }
    }
    return 11U; // 9600 default
}

static unsigned CycleUnsigned(unsigned value, unsigned minValue, unsigned maxValue, int delta)
{
    if (maxValue < minValue)
    {
        return value;
    }
    if (delta > 0)
    {
        return (value >= maxValue) ? minValue : (value + 1U);
    }
    return (value <= minValue) ? maxValue : (value - 1U);
}

static const char *BoolName(bool value)
{
    return value ? "On" : "Off";
}

static void BuildTabLine(char *out, unsigned cols, const CTConfig *config)
{
    if (out == nullptr || cols == 0)
    {
        return;
    }

    for (unsigned i = 0; i < cols; ++i)
    {
        if (config != nullptr)
        {
            out[i] = config->IsTabStop(i) ? 'T' : ' ';
        }
        else
        {
            out[i] = ((i + 1) % 8 == 0) ? 'T' : ' ';
        }
    }
    out[cols] = '\0';
}

static void BuildDigitLine(char *out, unsigned cols)
{
    if (out == nullptr || cols == 0)
    {
        return;
    }

    const char *digits = "0123456789";
    unsigned pos = 0;

    while (pos < cols)
    {
        for (unsigned i = 0; i < 10 && pos < cols; ++i)
        {
            out[pos++] = digits[i];
        }
    }

    out[cols] = '\0';
}
}


static CTSetup *s_pThis = nullptr;
CTSetup *CTSetup::Get(void)
{
    if (s_pThis == nullptr)
    {
        s_pThis = new CTSetup();
    }
    return s_pThis;
}

CTSetup::CTSetup()
    : CTask()
    , m_pRenderer(nullptr)
    , m_pConfig(nullptr)
    , m_pKeyboard(nullptr)
    , m_pPrevKeyPressed(nullptr)
    , m_pPrevKeyStatusRaw(nullptr)
    , m_Snapshot{nullptr, 0, false, false, {}}
    , m_Visible(false)
    , m_ExitRequested(false)
    , m_SaveRequested(false)
    , m_KeyPending(false)
    , m_F12Down(false)
    , m_F11Down(false)
    , m_KeyBuffer{0}
    , m_DialogMode(DialogModeLegacy)
    , m_Page(SetupPageA)
    , m_SetupBToggle{0, 0, 0, 0}
    , m_SetupBTxSpeed(9600)
    , m_SetupBRxSpeed(9600)
    , m_SetupBField(SetupBFieldToggle1)
    , m_SetupBBitIndex(0)
    , m_TabRow(0)
    , m_TabCols(0)
    , m_TabEditCol(0)
    , m_ModernSelected(ModernFieldLineEnding)
    , m_ModernConfig{}
    , m_ModernLayoutValid(false)
    , m_ModernLayout{}
{
    SetName("Setup");
    Suspend();
}

bool CTSetup::Initialize(CTRenderer *renderer, CTConfig *config, CTKeyboard *keyboard)
{
    m_pRenderer = renderer;
    m_pConfig = config;
    m_pKeyboard = keyboard;
    return true;
}

bool CTSetup::IsVisible() const
{
    return m_Visible;
}

void CTSetup::Toggle()
{
    if (IsVisible())
    {
        Hide();
    }
    else
    {
        Show();
    }
}

void CTSetup::Show()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    if (m_pKeyboard != nullptr)
    {
        m_pPrevKeyPressed = m_pKeyboard->GetKeyPressedHandler();
        m_pPrevKeyStatusRaw = m_pKeyboard->GetKeyStatusHandlerRaw();
        m_pKeyboard->SetKeyPressedHandler(KeyPressedHandler);
        m_pKeyboard->SetKeyStatusHandlerRaw(KeyStatusHandlerRaw);
    }

    size_t size = m_pRenderer->GetBufferSize();
    if (size == 0)
    {
        return;
    }

    if (m_Snapshot.buffer == nullptr || m_Snapshot.size != size)
    {
        delete[] m_Snapshot.buffer;
        m_Snapshot.buffer = new u8[size];
        m_Snapshot.size = size;
    }

    if (m_Snapshot.buffer != nullptr)
    {
        m_pRenderer->SaveScreenBuffer(m_Snapshot.buffer, size);
        m_Snapshot.valid = true;
    }

    m_pRenderer->SaveState(m_Snapshot.rendererState);
    m_Snapshot.stateValid = true;

    m_DialogMode = DialogModeLegacy;
    InitializeSetupBFromConfig();
    m_ModernLayoutValid = false;

    m_Page = SetupPageA;
    m_F12Down = false;
    m_SaveRequested = false;
    Render();
    m_Visible = true;

    if (IsSuspended())
    {
        Start();
    }
}

void CTSetup::ShowModern()
{
    if (!m_Visible)
    {
        Show();
    }

    if (m_pRenderer == nullptr)
    {
        return;
    }

    m_DialogMode = DialogModeModern;
    m_F11Down = false;
    m_F12Down = false;
    m_SaveRequested = false;
    m_ExitRequested = false;
    m_ModernSelected = ModernFieldLineEnding;
    InitializeModernFromConfig();
    m_ModernLayoutValid = false;
    Render();
}

void CTSetup::Hide()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    m_pRenderer->ForceHideCursor();

    if (m_Snapshot.valid && m_Snapshot.buffer != nullptr)
    {
        m_pRenderer->RestoreScreenBuffer(m_Snapshot.buffer, m_Snapshot.size);
    }

    if (m_Snapshot.stateValid)
    {
        m_pRenderer->RestoreState(m_Snapshot.rendererState);
    }

    m_Visible = false;
    m_ExitRequested = false;
    m_SaveRequested = false;
    m_KeyPending = false;
    m_F12Down = false;
    m_F11Down = false;
    m_DialogMode = DialogModeLegacy;
    m_ModernLayoutValid = false;

    if (m_pKeyboard != nullptr)
    {
        m_pKeyboard->SetKeyPressedHandler(m_pPrevKeyPressed);
        m_pKeyboard->SetKeyStatusHandlerRaw(m_pPrevKeyStatusRaw);
    }

    if (!IsSuspended())
    {
        Suspend();
    }
}

void CTSetup::Run(void)
{
    while (true)
    {
        if (IsSuspended())
        {
            CScheduler::Get()->MsSleep(20);
            continue;
        }

        if (m_ExitRequested)
        {
            bool applyVisualSettings = false;
            if (m_SaveRequested)
            {
                if (m_DialogMode == DialogModeModern)
                {
                    ApplyModernToConfig();
                }
                else
                {
                    ApplySetupBToConfig();
                }
                applyVisualSettings = true;
                if (m_pConfig != nullptr)
                {
                    m_pConfig->SaveToFile();
                }
            }
            Hide();

            if (applyVisualSettings && m_pRenderer != nullptr && m_pConfig != nullptr)
            {
                CKernel *kernel = CKernel::Get();
                if (kernel != nullptr)
                {
                    kernel->ApplyRuntimeConfig();
                }
                else
                {
                    m_pRenderer->SetColors(m_pConfig->GetTextColor(), m_pConfig->GetBackgroundColor());
                    m_pRenderer->SetFont(m_pConfig->GetFontSelection(), CCharGenerator::FontFlagsNone);
                    m_pRenderer->SetCursorBlock(m_pConfig->GetCursorBlock());
                    m_pRenderer->SetBlinkingCursor(m_pConfig->GetCursorBlinking(), 500);
                    m_pRenderer->SetVT52Mode(m_pConfig->GetVT52ModeEnabled() ? TRUE : FALSE);
                    m_pRenderer->SetSmoothScrollEnabled(m_pConfig->GetSmoothScrollEnabled() ? TRUE : FALSE);
                }
            }
            continue;
        }

        if (m_KeyPending)
        {
            m_KeyPending = false;
        }

        CScheduler::Get()->MsSleep(20);
    }
}

void CTSetup::KeyPressedHandler(const char *pString)
{
    CTSetup *instance = CTSetup::Get();
    if (instance != nullptr)
    {
        instance->OnKeyPressed(pString);
    }
}

void CTSetup::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    CTSetup *instance = CTSetup::Get();
    if (instance != nullptr)
    {
        instance->OnRawKeyStatus(ucModifiers, RawKeys);
    }
}

void CTSetup::OnKeyPressed(const char *pString)
{
    if (pString == nullptr)
    {
        return;
    }

    if (!m_Visible || m_pRenderer == nullptr)
    {
        return;
    }

    if (m_DialogMode == DialogModeModern)
    {
        HandleModernKeyPress(pString);
        return;
    }

    if (m_Page != SetupPageA)
    {
        if (strcmp(pString, "\x1b[D") == 0)
        {
            MoveSetupBFieldLeft();
            RenderPageB();
            return;
        }

        if (strcmp(pString, "\x1b[C") == 0)
        {
            MoveSetupBFieldRight();
            RenderPageB();
            return;
        }

        if (strcmp(pString, "\x1b[H") == 0)
        {
            m_SetupBField = SetupBFieldToggle1;
            m_SetupBBitIndex = 0;
            RenderPageB();
            return;
        }

        if (strcmp(pString, "\x1b[F") == 0)
        {
            m_SetupBField = SetupBFieldRxSpeed;
            m_SetupBBitIndex = 0;
            RenderPageB();
            return;
        }

        if (strcmp(pString, "\x1b[A") == 0)
        {
            if (m_SetupBField >= SetupBFieldToggle1 && m_SetupBField <= SetupBFieldToggle4)
            {
                ToggleSetupBFieldBit(true);
            }
            else
            {
                ChangeSetupBSpeed(true);
            }
            RenderPageB();
            return;
        }

        if (strcmp(pString, "\x1b[B") == 0)
        {
            if (m_SetupBField >= SetupBFieldToggle1 && m_SetupBField <= SetupBFieldToggle4)
            {
                ToggleSetupBFieldBit(false);
            }
            else
            {
                ChangeSetupBSpeed(false);
            }
            RenderPageB();
            return;
        }

        if (pString[1] == '\0')
        {
            const char ch = pString[0];
            if (ch == '1')
            {
                ToggleSetupBFieldBit(true);
                RenderPageB();
                return;
            }
            if (ch == '0' || ch == ' ')
            {
                ToggleSetupBFieldBit(false);
                RenderPageB();
                return;
            }
        }

        return;
    }

    if (strcmp(pString, "\x1b[D") == 0)
    {
        if (m_TabEditCol > 0)
        {
            --m_TabEditCol;
            UpdateTabCursor();
        }
        return;
    }

    if (strcmp(pString, "\x1b[C") == 0)
    {
        if (m_TabCols > 0 && m_TabEditCol + 1 < m_TabCols)
        {
            ++m_TabEditCol;
            UpdateTabCursor();
        }
        return;
    }

    if (strcmp(pString, "\x1b[H") == 0)
    {
        m_TabEditCol = 0;
        UpdateTabCursor();
        return;
    }

    if (strcmp(pString, "\x1b[F") == 0)
    {
        if (m_TabCols > 0)
        {
            m_TabEditCol = m_TabCols - 1;
            UpdateTabCursor();
        }
        return;
    }

    if (pString[1] == '\0')
    {
        const char ch = pString[0];
        if ((ch == 'T' || ch == 't' || ch == ' ') && m_pConfig != nullptr)
        {
            m_pConfig->SetTabStop(m_TabEditCol, ch != ' ');
            UpdateTabCell();
            return;
        }
    }

    strncpy(m_KeyBuffer, pString, sizeof(m_KeyBuffer) - 1);
    m_KeyBuffer[sizeof(m_KeyBuffer) - 1] = '\0';
    m_KeyPending = true;
}

void CTSetup::OnRawKeyStatus(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    (void)ucModifiers;

    bool f12Down = false;
    bool f11Down = false;
    for (unsigned i = 0; i < 6; ++i)
    {
        if (RawKeys[i] == 0x44)
        {
            f11Down = true;
        }
        if (RawKeys[i] == 0x45)
        {
            f12Down = true;
        }
    }

    if (f11Down && !m_F11Down)
    {
        m_DialogMode = DialogModeModern;
        m_ModernSelected = ModernFieldLineEnding;
        InitializeModernFromConfig();
        Render();
    }

    m_F11Down = f11Down;

    if (m_DialogMode == DialogModeModern)
    {
        m_F12Down = f12Down;
        return;
    }

    if (f12Down && !m_F12Down)
    {
        if (m_Page == SetupPageA)
        {
            m_Page = SetupPageB;
            Render();
        }
        else
        {
            m_SaveRequested = true;
            m_ExitRequested = true;
        }
    }

    m_F12Down = f12Down;
}

void CTSetup::Render()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    if (m_DialogMode == DialogModeModern)
    {
        RenderModernDialog();
        return;
    }

    if (m_Page == SetupPageB)
    {
        RenderPageB();
        return;
    }

    RenderPageA();
}

void CTSetup::RenderHeader(const char *pTitle, unsigned topRow)
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    static const char kESC_3[] = "\x1B#3";
    static const char kESC_4[] = "\x1B#4";
    static const char kESC_5[] = "\x1B#5";
    static const char kESC_6[] = "\x1B#6";
    static const char kESC_Reset[] = "\x1B[0m";
    static const char kESC_G0_US[] = "\x1B(B";
    static const char kESC_G1_US[] = "\x1B)B";
    static const char kSI_G0[] = "\x0F";

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->SetCursorMode(false);
    m_pRenderer->SetBlinkingCursor(false);
    m_pRenderer->ClearDisplay();
    m_pRenderer->Goto(0, 0);
    m_pRenderer->Write(kESC_Reset, strlen(kESC_Reset));
    m_pRenderer->Write(kESC_G0_US, strlen(kESC_G0_US));
    m_pRenderer->Write(kESC_G1_US, strlen(kESC_G1_US));
    m_pRenderer->Write(kSI_G0, strlen(kSI_G0));
    m_pRenderer->Write(kESC_5, strlen(kESC_5));

    // Title: double width + double height (top and bottom lines)
    m_pRenderer->Goto(topRow, 0);
    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->Write(kESC_3, strlen(kESC_3));
    m_pRenderer->Write(pTitle, strlen(pTitle));

    m_pRenderer->Goto(topRow + 1, 0);
    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->Write(kESC_4, strlen(kESC_4));
    m_pRenderer->Write("\r", 1);

    // Subtitle: double width
    m_pRenderer->Goto(topRow + 3, 0);
    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->Write(kESC_6, strlen(kESC_6));
    m_pRenderer->Write("TO EXIT PRESS \"SET-UP\"", strlen("TO EXIT PRESS \"SET-UP\""));

    // Return to normal width
    m_pRenderer->Write(kESC_5, strlen(kESC_5));
}

void CTSetup::RenderPageA()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    RenderHeader("SET-UP A", 1);

    unsigned rows = m_pRenderer->GetRows();
    unsigned cols = m_pRenderer->GetColumns();
    if (rows >= 2 && cols > 0)
    {
        const unsigned maxCols = 160;
        unsigned drawCols = cols > maxCols ? maxCols : cols;
        char tabLine[maxCols + 1];
        char digitLine[maxCols + 1];

        BuildTabLine(tabLine, drawCols, m_pConfig);
        BuildDigitLine(digitLine, drawCols);

        m_pRenderer->Goto(rows - 2, 0);
        m_pRenderer->Write(tabLine, strlen(tabLine));

        m_pRenderer->Goto(rows - 1, 0);

        const unsigned fullBlocks = drawCols / 10;
        const unsigned remainder = drawCols % 10;

        for (unsigned block = 0; block < fullBlocks; ++block)
        {
            const bool reverse = (block % 2 == 1);
            if (reverse)
            {
                m_pRenderer->SetColors(bgColor, fgColor);
            }
            else
            {
                m_pRenderer->SetColors(fgColor, bgColor);
            }
            m_pRenderer->Write(digitLine + block * 10, 10);
        }

        if (remainder > 0)
        {
            const bool reverse = (fullBlocks % 2 == 1);
            if (reverse)
            {
                m_pRenderer->SetColors(bgColor, fgColor);
            }
            else
            {
                m_pRenderer->SetColors(fgColor, bgColor);
            }
            m_pRenderer->Write(digitLine + fullBlocks * 10, remainder);
        }

        m_pRenderer->SetColors(fgColor, bgColor);

        // Keep tab edit cursor on the visible tab ruler line
        m_TabRow = rows - 2;
        m_TabCols = drawCols;
        if (m_TabEditCol >= m_TabCols)
        {
            m_TabEditCol = 0;
        }
        m_pRenderer->SetCursorMode(true);
        UpdateTabCursor();
        m_pRenderer->SetBlinkingCursor(false);
        UpdateTabCursor();
    }
}

void CTSetup::RenderPageB()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    // Keep SET-UP B at its previous/original position
    RenderHeader("SET-UP B", 1);

    unsigned rows = m_pRenderer->GetRows();
    unsigned cols = m_pRenderer->GetColumns();
    if (rows < 2 || cols == 0)
    {
        return;
    }

    const unsigned infoRow = (rows >= 3) ? (rows - 3) : 0;
    const unsigned dataRow = (rows >= 2) ? (rows - 2) : 0;

    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->Goto(infoRow, 0);
    m_pRenderer->Write("!", 1);

    auto writeToggleBits = [this](unsigned value)
    {
        char bits[5];
        bits[0] = (value & 0x8U) ? '1' : '0';
        bits[1] = (value & 0x4U) ? '1' : '0';
        bits[2] = (value & 0x2U) ? '1' : '0';
        bits[3] = (value & 0x1U) ? '1' : '0';
        bits[4] = '\0';
        m_pRenderer->Write(bits, 4);
    };

    m_pRenderer->Goto(dataRow, 0);

    // 1 TOGxxxx 2 TOGxxxx 3 TOGxxxx 4 TOGxxxx
    m_pRenderer->Write(" 1 ", 3);
    m_pRenderer->SetColors(bgColor, fgColor);
    writeToggleBits(m_SetupBToggle[0] & 0x0FU);
    m_pRenderer->SetColors(fgColor, bgColor);

    m_pRenderer->Write("  2 ", 4);
    m_pRenderer->SetColors(bgColor, fgColor);
    writeToggleBits(m_SetupBToggle[1] & 0x0FU);
    m_pRenderer->SetColors(fgColor, bgColor);

    m_pRenderer->Write("  3 ", 4);
    m_pRenderer->SetColors(bgColor, fgColor);
    writeToggleBits(m_SetupBToggle[2] & 0x0FU);
    m_pRenderer->SetColors(fgColor, bgColor);

    m_pRenderer->Write("  4 ", 4);
    m_pRenderer->SetColors(bgColor, fgColor);
    writeToggleBits(m_SetupBToggle[3] & 0x0FU);
    m_pRenderer->SetColors(fgColor, bgColor);

    CString speedPart;
    speedPart.Format("      T SPEED %5u   R SPEED %5u", m_SetupBTxSpeed, m_SetupBRxSpeed);
    m_pRenderer->Write(speedPart.c_str(), speedPart.GetLength());

    m_TabRow = 0;
    m_TabCols = 0;
    m_pRenderer->SetCursorMode(true);
    unsigned cursorRow = dataRow;
    unsigned cursorCol = 0;
    GetSetupBSpeedFieldPosition(m_SetupBField, cursorRow, cursorCol);
    m_pRenderer->Goto(cursorRow, cursorCol);
    m_pRenderer->SetBlinkingCursor(false);
}

void CTSetup::InitializeSetupBFromConfig()
{
    m_SetupBToggle[0] = 0;
    m_SetupBToggle[1] = 0;
    m_SetupBToggle[2] = 0;
    m_SetupBToggle[3] = 0;

    unsigned baud = 9600;
    if (m_pConfig != nullptr)
    {
        baud = m_pConfig->GetBaudRate();
        if (m_pConfig->GetCursorBlock())
        {
            m_SetupBToggle[0] |= 0x1U;
        }
        if (m_pConfig->GetSmoothScrollEnabled())
        {
            m_SetupBToggle[0] |= 0x8U;
        }
        if (m_pConfig->GetScreenInverted())
        {
            m_SetupBToggle[0] |= 0x2U;
        }
        if (m_pConfig->GetKeyAutoRepeatEnabled())
        {
            m_SetupBToggle[0] |= 0x4U;
        }
        if (m_pConfig->GetMarginBellEnabled())
        {
            m_SetupBToggle[1] |= 0x8U;
        }
        if (m_pConfig->GetSoftwareFlowControl())
        {
            m_SetupBToggle[1] |= 0x1U; // Group 2, bit 4 (rightmost shown bit)
        }
        if (m_pConfig->GetKeyClick())
        {
            m_SetupBToggle[1] |= 0x4U;
        }
        if (m_pConfig->GetVT52ModeEnabled())
        {
            m_SetupBToggle[1] |= 0x2U;
        }
        if (m_pConfig->GetLineEndingMode() != 0)
        {
            m_SetupBToggle[2] |= 0x2U;
        }
        if (m_pConfig->GetWrapAroundEnabled())
        {
            m_SetupBToggle[2] |= 0x4U;
        }
        if (m_pConfig->GetSerialParityMode() != 0U)
        {
            m_SetupBToggle[3] |= 0x4U;
        }
        if (m_pConfig->GetSerialParityMode() == 2U)
        {
            m_SetupBToggle[3] |= 0x8U;
        }
        if (m_pConfig->GetSerialDataBits() == 7U)
        {
            m_SetupBToggle[3] |= 0x2U;
        }
    }

    m_SetupBTxSpeed = baud;
    m_SetupBRxSpeed = baud;
    m_SetupBField = SetupBFieldToggle1;
    m_SetupBBitIndex = 0;
}

void CTSetup::ApplySetupBToConfig()
{
    if (m_pConfig == nullptr)
    {
        return;
    }

    m_pConfig->SetBaudRate(m_SetupBTxSpeed);

    const bool cursorBlock = (m_SetupBToggle[0] & 0x1U) != 0;
    m_pConfig->SetCursorBlock(cursorBlock ? TRUE : FALSE);

    const bool smoothScroll = (m_SetupBToggle[0] & 0x8U) != 0;
    m_pConfig->SetSmoothScrollEnabled(smoothScroll ? TRUE : FALSE);

    const bool screenInverted = (m_SetupBToggle[0] & 0x2U) != 0;
    m_pConfig->SetScreenInverted(screenInverted ? TRUE : FALSE);

    const bool autoRepeatEnabled = (m_SetupBToggle[0] & 0x4U) != 0;
    m_pConfig->SetKeyAutoRepeatEnabled(autoRepeatEnabled ? TRUE : FALSE);

    const bool marginBellEnabled = (m_SetupBToggle[1] & 0x8U) != 0;
    m_pConfig->SetMarginBellEnabled(marginBellEnabled ? TRUE : FALSE);

    const bool keyClick = (m_SetupBToggle[1] & 0x4U) != 0;
    m_pConfig->SetKeyClick(keyClick ? TRUE : FALSE);

    const bool flowControl = (m_SetupBToggle[1] & 0x1U) != 0;
    m_pConfig->SetSoftwareFlowControl(flowControl ? TRUE : FALSE);

    const bool vt52Mode = (m_SetupBToggle[1] & 0x2U) != 0;
    m_pConfig->SetVT52ModeEnabled(vt52Mode ? TRUE : FALSE);

    const bool newLineMode = (m_SetupBToggle[2] & 0x2U) != 0;
    m_pConfig->SetLineEndingMode(newLineMode ? 1U : 0U);

    const bool wrapAround = (m_SetupBToggle[2] & 0x4U) != 0;
    m_pConfig->SetWrapAroundEnabled(wrapAround ? TRUE : FALSE);

    const bool parityEnabled = (m_SetupBToggle[3] & 0x4U) != 0;
    const bool paritySenseOdd = (m_SetupBToggle[3] & 0x8U) != 0;
    m_pConfig->SetSerialParityMode(parityEnabled ? (paritySenseOdd ? 2U : 1U) : 0U);

    const bool bits7 = (m_SetupBToggle[3] & 0x2U) != 0;
    m_pConfig->SetSerialDataBits(bits7 ? 7U : 8U);
}

void CTSetup::MoveSetupBFieldLeft()
{
    if (m_SetupBField == SetupBFieldToggle1)
    {
        if (m_SetupBBitIndex > 0)
        {
            --m_SetupBBitIndex;
        }
        return;
    }

    if (m_SetupBField >= SetupBFieldToggle2 && m_SetupBField <= SetupBFieldToggle4)
    {
        if (m_SetupBBitIndex > 0)
        {
            --m_SetupBBitIndex;
        }
        else
        {
            m_SetupBField = static_cast<TSetupBField>(static_cast<int>(m_SetupBField) - 1);
            m_SetupBBitIndex = 3;
        }
        return;
    }

    if (m_SetupBField == SetupBFieldTxSpeed)
    {
        m_SetupBField = SetupBFieldToggle4;
        m_SetupBBitIndex = 3;
        return;
    }

    if (m_SetupBField == SetupBFieldRxSpeed)
    {
        m_SetupBField = SetupBFieldTxSpeed;
        return;
    }
}

void CTSetup::MoveSetupBFieldRight()
{
    if (m_SetupBField >= SetupBFieldToggle1 && m_SetupBField <= SetupBFieldToggle4)
    {
        if (m_SetupBBitIndex < 3)
        {
            ++m_SetupBBitIndex;
            return;
        }

        if (m_SetupBField == SetupBFieldToggle4)
        {
            m_SetupBField = SetupBFieldTxSpeed;
            m_SetupBBitIndex = 0;
            return;
        }

        m_SetupBField = static_cast<TSetupBField>(static_cast<int>(m_SetupBField) + 1);
        m_SetupBBitIndex = 0;
        return;
    }

    if (m_SetupBField == SetupBFieldRxSpeed)
    {
        return;
    }

    m_SetupBField = static_cast<TSetupBField>(static_cast<int>(m_SetupBField) + 1);
}

void CTSetup::ToggleSetupBFieldBit(bool setOne)
{
    if (m_SetupBField >= SetupBFieldToggle1 && m_SetupBField <= SetupBFieldToggle4)
    {
        const unsigned idx = static_cast<unsigned>(m_SetupBField);
        const unsigned bitMask = 1U << (3U - (m_SetupBBitIndex & 0x3U));
        if (setOne)
        {
            m_SetupBToggle[idx] = (m_SetupBToggle[idx] | bitMask) & 0x0FU;
        }
        else
        {
            m_SetupBToggle[idx] = (m_SetupBToggle[idx] & ~bitMask) & 0x0FU;
        }
        return;
    }

    if (m_SetupBField == SetupBFieldTxSpeed || m_SetupBField == SetupBFieldRxSpeed)
    {
        ChangeSetupBSpeed(setOne);
    }
}

void CTSetup::ChangeSetupBSpeed(bool increase)
{
    unsigned *pSpeed = nullptr;
    if (m_SetupBField == SetupBFieldTxSpeed || m_SetupBField == SetupBFieldRxSpeed)
    {
        // VT100 emulation currently uses one common serial speed.
        // Keep RX speed following TX speed at all times.
        pSpeed = &m_SetupBTxSpeed;
    }

    if (pSpeed == nullptr)
    {
        return;
    }

    unsigned currentIndex = 0;
    for (unsigned i = 0; i < kBaudRateCount; ++i)
    {
        if (kBaudRates[i] == *pSpeed)
        {
            currentIndex = i;
            break;
        }
        if (kBaudRates[i] > *pSpeed)
        {
            currentIndex = i;
            break;
        }
        if (i + 1 == kBaudRateCount)
        {
            currentIndex = i;
        }
    }

    if (increase)
    {
        currentIndex = (currentIndex + 1U) % kBaudRateCount;
    }
    else
    {
        currentIndex = (currentIndex == 0U) ? (kBaudRateCount - 1U) : (currentIndex - 1U);
    }

    *pSpeed = kBaudRates[currentIndex];
    m_SetupBRxSpeed = m_SetupBTxSpeed;
}

void CTSetup::GetSetupBSpeedFieldPosition(TSetupBField field, unsigned &row, unsigned &col) const
{
    row = m_pRenderer != nullptr && m_pRenderer->GetRows() > 1 ? (m_pRenderer->GetRows() - 2) : 0;

    switch (field)
    {
    case SetupBFieldToggle1:
        col = 3 + (m_SetupBBitIndex & 0x3U);
        break;
    case SetupBFieldToggle2:
        col = 11 + (m_SetupBBitIndex & 0x3U);
        break;
    case SetupBFieldToggle3:
        col = 19 + (m_SetupBBitIndex & 0x3U);
        break;
    case SetupBFieldToggle4:
        col = 27 + (m_SetupBBitIndex & 0x3U);
        break;
    case SetupBFieldTxSpeed:
        col = 49;
        break;
    case SetupBFieldRxSpeed:
        col = 65;
        break;
    default:
        col = 0;
        break;
    }
}

void CTSetup::InitializeModernFromConfig()
{
    if (m_pConfig == nullptr)
    {
        memset(&m_ModernConfig, 0, sizeof(m_ModernConfig));
        strncpy(m_ModernConfig.logFileName, "vt100.log", sizeof(m_ModernConfig.logFileName) - 1);
        m_ModernConfig.logFileName[sizeof(m_ModernConfig.logFileName) - 1] = '\0';
        m_ModernConfig.lineEnding = 0U;
        m_ModernConfig.baudRate = 9600U;
        m_ModernConfig.serialBits = 8U;
        m_ModernConfig.fontSelection = EFontSelection::VT100Font10x20;
        m_ModernConfig.textColor = TerminalColorGreen;
        m_ModernConfig.backgroundColor = TerminalColorBlack;
        m_ModernConfig.repeatDelayMs = kRepeatDelayMinMs;
        m_ModernConfig.repeatRateCps = 10U;
        return;
    }

    m_ModernConfig.lineEnding = m_pConfig->GetLineEndingMode();
    m_ModernConfig.baudRate = m_pConfig->GetBaudRate();
    m_ModernConfig.serialBits = m_pConfig->GetSerialDataBits();
    m_ModernConfig.serialParity = m_pConfig->GetSerialParityMode();
    m_ModernConfig.cursorBlock = m_pConfig->GetCursorBlock() ? true : false;
    m_ModernConfig.cursorBlinking = m_pConfig->GetCursorBlinking() ? true : false;
    m_ModernConfig.vtTestEnabled = m_pConfig->GetVTTestEnabled() ? true : false;
    m_ModernConfig.vt52Mode = m_pConfig->GetVT52ModeEnabled() ? true : false;
    m_ModernConfig.fontSelection = m_pConfig->GetFontSelection();
    m_ModernConfig.textColor = m_pConfig->GetTextColor();
    m_ModernConfig.backgroundColor = m_pConfig->GetBackgroundColor();
    m_ModernConfig.buzzerVolume = m_pConfig->GetBuzzerVolume();
    m_ModernConfig.keyClick = m_pConfig->GetKeyClick() != 0U;
    m_ModernConfig.keyAutoRepeat = m_pConfig->GetKeyAutoRepeatEnabled() ? true : false;
    m_ModernConfig.repeatDelayMs = m_pConfig->GetKeyRepeatDelayMs();
    m_ModernConfig.repeatRateCps = m_pConfig->GetKeyRepeatRateCps();
    m_ModernConfig.switchTxRx = m_pConfig->GetSwitchTxRx() != 0U;
    m_ModernConfig.wlanModePolicy = m_pConfig->GetWlanHostAutoStart();
    m_ModernConfig.logOutput = m_pConfig->GetLogOutput() & 0x7U;
    strncpy(m_ModernConfig.logFileName, m_pConfig->GetLogFileName(), sizeof(m_ModernConfig.logFileName) - 1);
    m_ModernConfig.logFileName[sizeof(m_ModernConfig.logFileName) - 1] = '\0';
}

void CTSetup::ApplyModernToConfig()
{
    if (m_pConfig == nullptr)
    {
        return;
    }

    m_pConfig->SetLineEndingMode(m_ModernConfig.lineEnding);
    m_pConfig->SetBaudRate(m_ModernConfig.baudRate);
    m_pConfig->SetSerialDataBits(m_ModernConfig.serialBits);
    m_pConfig->SetSerialParityMode(m_ModernConfig.serialParity);
    m_pConfig->SetCursorBlock(m_ModernConfig.cursorBlock ? TRUE : FALSE);
    m_pConfig->SetCursorBlinking(m_ModernConfig.cursorBlinking ? TRUE : FALSE);
    m_pConfig->SetVTTestEnabled(m_ModernConfig.vtTestEnabled ? TRUE : FALSE);
    m_pConfig->SetVT52ModeEnabled(m_ModernConfig.vt52Mode ? TRUE : FALSE);
    m_pConfig->SetFontSelection(m_ModernConfig.fontSelection);
    m_pConfig->SetTextColor(m_ModernConfig.textColor);
    m_pConfig->SetBackgroundColor(m_ModernConfig.backgroundColor);
    m_pConfig->SetBuzzerVolume(m_ModernConfig.buzzerVolume);
    m_pConfig->SetKeyClick(m_ModernConfig.keyClick ? TRUE : FALSE);
    m_pConfig->SetKeyAutoRepeatEnabled(m_ModernConfig.keyAutoRepeat ? TRUE : FALSE);
    m_pConfig->SetKeyRepeatDelayMs(m_ModernConfig.repeatDelayMs);
    m_pConfig->SetKeyRepeatRateCps(m_ModernConfig.repeatRateCps);
    m_pConfig->SetSwitchTxRx(m_ModernConfig.switchTxRx ? TRUE : FALSE);
    m_pConfig->SetWlanHostAutoStart(m_ModernConfig.wlanModePolicy);
    m_pConfig->SetLogOutput(m_ModernConfig.logOutput);
    m_pConfig->SetLogFileName(m_ModernConfig.logFileName);
}

void CTSetup::RenderModernDialog()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    TModernLayoutState layout{};
    if (!ComputeModernLayout(layout))
    {
        m_pRenderer->SetColors(fgColor, bgColor);
        m_pRenderer->ClearDisplay();
        m_pRenderer->Goto(0, 0);
        m_pRenderer->Write("Display too small for modern setup", strlen("Display too small for modern setup"));
        m_pRenderer->SetCursorMode(false);
        m_pRenderer->SetBlinkingCursor(false);
        m_ModernLayoutValid = false;
        return;
    }

    char line[kModernRowBufferSize];
    const unsigned drawWidth = (layout.width < sizeof(line) - 1U) ? layout.width : (sizeof(line) - 1U);
    const unsigned drawInnerWidth = (layout.innerWidth < sizeof(line) - 1U) ? layout.innerWidth : (sizeof(line) - 1U);

    m_pRenderer->SetColors(fgColor, bgColor);
    m_pRenderer->SetCursorMode(false);
    m_pRenderer->SetBlinkingCursor(false);
    m_pRenderer->ClearDisplay();

    memset(line, 'q', drawWidth);
    if (drawWidth >= 2)
    {
        line[0] = 'l';
        line[drawWidth - 1] = 'k';
    }
    line[drawWidth] = '\0';
    m_pRenderer->Goto(layout.top, layout.left);
    m_pRenderer->Write("\x1B(0", 3);
    m_pRenderer->Write(line, drawWidth);
    m_pRenderer->Write("\x1B(B", 3);

    memset(line, ' ', drawWidth);
    if (drawWidth >= 2)
    {
        line[0] = 'x';
        line[drawWidth - 1] = 'x';
    }
    line[drawWidth] = '\0';
    m_pRenderer->Write("\x1B(0", 3);
    for (unsigned row = layout.top + 1; row < layout.bottom; ++row)
    {
        m_pRenderer->Goto(row, layout.left);
        m_pRenderer->Write(line, drawWidth);
    }
    m_pRenderer->Write("\x1B(B", 3);

    memset(line, 'q', drawWidth);
    if (drawWidth >= 2)
    {
        line[0] = 'm';
        line[drawWidth - 1] = 'j';
    }
    line[drawWidth] = '\0';
    m_pRenderer->Goto(layout.bottom, layout.left);
    m_pRenderer->Write("\x1B(0", 3);
    m_pRenderer->Write(line, drawWidth);
    m_pRenderer->Write("\x1B(B", 3);

    const char *titleText = "VT100 Emulation Setup";
    const unsigned titleLen = strlen(titleText);
    const unsigned innerStartDouble = (layout.left + 1U) / 2U;
    const unsigned innerWidthDouble = drawInnerWidth / 2U;
    unsigned titleCol = innerStartDouble;
    if (innerWidthDouble > titleLen)
    {
        titleCol = innerStartDouble + ((innerWidthDouble - titleLen) / 2U);
    }
    m_pRenderer->Write("\x1B[1m", strlen("\x1B[1m"));
    m_pRenderer->Write("\x1B#6", strlen("\x1B#6"));
    m_pRenderer->Goto(layout.top + 1, titleCol);
    m_pRenderer->Write(titleText, titleLen);
    m_pRenderer->Write("\x1B#5", strlen("\x1B#5"));
    m_pRenderer->Write("\x1B[22m", strlen("\x1B[22m"));

    m_pRenderer->Goto(layout.top + 3, layout.left + 2);
    m_pRenderer->Write("Parameter", strlen("Parameter"));
    m_pRenderer->Goto(layout.top + 3, layout.left + 28);
    m_pRenderer->Write("Value", strlen("Value"));
    m_pRenderer->Goto(layout.top + 3, layout.left + 46);
    m_pRenderer->Write("Description", strlen("Description"));
    RenderModernFieldRows(layout, fgColor, bgColor);

    m_pRenderer->SetColors(fgColor, bgColor);
    CString footer;
    footer.Format(" Enter=Save  Esc=Cancel  Up/Down=Select  Left/Right=Edit ");
    memset(line, ' ', drawInnerWidth);
    const unsigned footerLen = footer.GetLength() < drawInnerWidth ? footer.GetLength() : drawInnerWidth;
    unsigned footerStartCol = layout.left + 1;
    if (drawInnerWidth > footerLen)
    {
        footerStartCol = layout.left + 1 + ((drawInnerWidth - footerLen) / 2);
    }
    memcpy(line + (footerStartCol - (layout.left + 1)), footer.c_str(), footerLen);
    line[drawInnerWidth] = '\0';
    m_pRenderer->Goto(layout.footerRow, layout.left + 1);
    m_pRenderer->Write(line, drawInnerWidth);

    m_ModernLayout = layout;
    m_ModernLayoutValid = true;
}

bool CTSetup::ComputeModernLayout(TModernLayoutState &layout) const
{
    if (m_pRenderer == nullptr)
    {
        return false;
    }

    layout.rows = m_pRenderer->GetRows();
    layout.cols = m_pRenderer->GetColumns();
    if (layout.rows < kModernDialogMinRows || layout.cols < kModernDialogMinCols)
    {
        return false;
    }

    layout.top = 1;
    layout.left = 1;
    layout.width = layout.cols - 2;
    layout.bottom = layout.rows - 2;
    layout.innerWidth = layout.width - 2;
    layout.dataStartRow = layout.top + 5;
    layout.footerRow = layout.bottom - 1;
    layout.availableRows = (layout.footerRow > layout.dataStartRow) ? (layout.footerRow - layout.dataStartRow) : 1;

    const unsigned maxStart = (ModernFieldCount > layout.availableRows) ? (ModernFieldCount - layout.availableRows) : 0;
    layout.startIndex = 0;
    if (static_cast<unsigned>(m_ModernSelected) >= layout.availableRows)
    {
        layout.startIndex = static_cast<unsigned>(m_ModernSelected) - layout.availableRows + 1;
        if (layout.startIndex > maxStart)
        {
            layout.startIndex = maxStart;
        }
    }

    return true;
}

void CTSetup::RenderModernFieldRow(const TModernLayoutState &layout, unsigned fieldIndex, bool selected, const TRendererColor &fgColor, const TRendererColor &bgColor)
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    const unsigned drawInnerWidth = (layout.innerWidth < (kModernRowBufferSize - 1U)) ? layout.innerWidth : (kModernRowBufferSize - 1U);
    if (drawInnerWidth == 0)
    {
        return;
    }

    char line[kModernRowBufferSize];
    memset(line, ' ', drawInnerWidth);
    line[drawInnerWidth] = '\0';

    if (fieldIndex < ModernFieldCount)
    {
        char valueBuffer[96];
        FormatModernValue(static_cast<TModernField>(fieldIndex), valueBuffer, sizeof(valueBuffer));

        CString rowText;
        rowText.Format(" %-22s %-14s %s", kModernFieldNames[fieldIndex], valueBuffer, kModernFieldDescriptions[fieldIndex]);
        const unsigned textLen = rowText.GetLength() < drawInnerWidth ? rowText.GetLength() : drawInnerWidth;
        memcpy(line, rowText.c_str(), textLen);
    }

    if (selected)
    {
        m_pRenderer->SetColors(bgColor, fgColor);
    }
    else
    {
        m_pRenderer->SetColors(fgColor, bgColor);
    }

    const unsigned row = layout.dataStartRow + (fieldIndex - layout.startIndex);
    m_pRenderer->Goto(row, layout.left + 1);
    m_pRenderer->Write(line, drawInnerWidth);
}

void CTSetup::RenderModernFieldRows(const TModernLayoutState &layout, const TRendererColor &fgColor, const TRendererColor &bgColor)
{
    for (unsigned rowIndex = 0; rowIndex < layout.availableRows; ++rowIndex)
    {
        const unsigned fieldIndex = layout.startIndex + rowIndex;
        if (layout.dataStartRow + rowIndex >= layout.footerRow)
        {
            break;
        }

        if (fieldIndex < ModernFieldCount)
        {
            const bool selected = (fieldIndex == static_cast<unsigned>(m_ModernSelected));
            RenderModernFieldRow(layout, fieldIndex, selected, fgColor, bgColor);
        }
        else
        {
            const unsigned drawInnerWidth = (layout.innerWidth < (kModernRowBufferSize - 1U)) ? layout.innerWidth : (kModernRowBufferSize - 1U);
            if (drawInnerWidth == 0)
            {
                continue;
            }

            char line[kModernRowBufferSize];
            memset(line, ' ', drawInnerWidth);
            line[drawInnerWidth] = '\0';
            m_pRenderer->SetColors(fgColor, bgColor);
            m_pRenderer->Goto(layout.dataStartRow + rowIndex, layout.left + 1);
            m_pRenderer->Write(line, drawInnerWidth);
        }
    }

    m_pRenderer->SetColors(fgColor, bgColor);
}

bool CTSetup::RenderModernSelectionDelta(TModernField previousSelected, unsigned previousStartIndex)
{
    if (!m_ModernLayoutValid || m_pRenderer == nullptr)
    {
        return false;
    }

    TModernLayoutState currentLayout{};
    if (!ComputeModernLayout(currentLayout))
    {
        return false;
    }

    if (currentLayout.rows != m_ModernLayout.rows || currentLayout.cols != m_ModernLayout.cols)
    {
        return false;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    m_ModernLayout = currentLayout;
    if (currentLayout.startIndex != previousStartIndex)
    {
        RenderModernFieldRows(currentLayout, fgColor, bgColor);
        return true;
    }

    const unsigned previousIndex = static_cast<unsigned>(previousSelected);
    const unsigned currentIndex = static_cast<unsigned>(m_ModernSelected);

    if (previousIndex >= previousStartIndex && previousIndex < previousStartIndex + currentLayout.availableRows)
    {
        RenderModernFieldRow(currentLayout, previousIndex, false, fgColor, bgColor);
    }

    if (currentIndex >= previousStartIndex && currentIndex < previousStartIndex + currentLayout.availableRows)
    {
        RenderModernFieldRow(currentLayout, currentIndex, true, fgColor, bgColor);
    }

    m_pRenderer->SetColors(fgColor, bgColor);
    return true;
}

bool CTSetup::RenderModernValueDelta()
{
    if (!m_ModernLayoutValid || m_pRenderer == nullptr)
    {
        return false;
    }

    TModernLayoutState currentLayout{};
    if (!ComputeModernLayout(currentLayout))
    {
        return false;
    }

    if (currentLayout.rows != m_ModernLayout.rows || currentLayout.cols != m_ModernLayout.cols)
    {
        return false;
    }

    const unsigned selectedIndex = static_cast<unsigned>(m_ModernSelected);
    if (selectedIndex < currentLayout.startIndex || selectedIndex >= currentLayout.startIndex + currentLayout.availableRows)
    {
        return false;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);

    m_ModernLayout = currentLayout;
    RenderModernFieldRow(currentLayout, selectedIndex, true, fgColor, bgColor);
    m_pRenderer->SetColors(fgColor, bgColor);
    return true;
}

void CTSetup::HandleModernKeyPress(const char *pString)
{
    if (pString == nullptr)
    {
        return;
    }

    if (strcmp(pString, "\x1b[A") == 0)
    {
        const TModernField previousSelected = m_ModernSelected;
        const unsigned previousStartIndex = m_ModernLayout.startIndex;
        MoveModernSelection(-1);
        if (!RenderModernSelectionDelta(previousSelected, previousStartIndex))
        {
            RenderModernDialog();
        }
        return;
    }
    if (strcmp(pString, "\x1b[B") == 0)
    {
        const TModernField previousSelected = m_ModernSelected;
        const unsigned previousStartIndex = m_ModernLayout.startIndex;
        MoveModernSelection(1);
        if (!RenderModernSelectionDelta(previousSelected, previousStartIndex))
        {
            RenderModernDialog();
        }
        return;
    }
    if (strcmp(pString, "\x1b[D") == 0)
    {
        ChangeModernValue(-1);
        if (!RenderModernValueDelta())
        {
            RenderModernDialog();
        }
        return;
    }
    if (strcmp(pString, "\x1b[C") == 0)
    {
        ChangeModernValue(1);
        if (!RenderModernValueDelta())
        {
            RenderModernDialog();
        }
        return;
    }

    if (strcmp(pString, "\x1b") == 0)
    {
        m_SaveRequested = false;
        m_ExitRequested = true;
        return;
    }

    if (strcmp(pString, "\r") == 0 || strcmp(pString, "\n") == 0 ||
        strcmp(pString, "\r\n") == 0 || strcmp(pString, "\n\r") == 0)
    {
        m_SaveRequested = true;
        m_ExitRequested = true;
    }
}

void CTSetup::MoveModernSelection(int delta)
{
    int selected = static_cast<int>(m_ModernSelected);
    selected += (delta < 0) ? -1 : 1;
    if (selected < 0)
    {
        selected = static_cast<int>(ModernFieldCount) - 1;
    }
    else if (selected >= static_cast<int>(ModernFieldCount))
    {
        selected = 0;
    }
    m_ModernSelected = static_cast<TModernField>(selected);
}

void CTSetup::ChangeModernValue(int delta)
{
    switch (m_ModernSelected)
    {
    case ModernFieldLineEnding:
        m_ModernConfig.lineEnding = CycleUnsigned(m_ModernConfig.lineEnding, 0U, 2U, delta);
        break;
    case ModernFieldBaudRate:
    {
        unsigned idx = FindBaudIndex(m_ModernConfig.baudRate);
        if (delta > 0)
        {
            idx = (idx + 1U) % kBaudRateCount;
        }
        else
        {
            idx = (idx == 0U) ? (kBaudRateCount - 1U) : (idx - 1U);
        }
        m_ModernConfig.baudRate = kBaudRates[idx];
        break;
    }
    case ModernFieldSerialBits:
        m_ModernConfig.serialBits = (m_ModernConfig.serialBits == 7U) ? 8U : 7U;
        break;
    case ModernFieldSerialParity:
        m_ModernConfig.serialParity = CycleUnsigned(m_ModernConfig.serialParity, 0U, 2U, delta);
        break;
    case ModernFieldCursorType:
        m_ModernConfig.cursorBlock = !m_ModernConfig.cursorBlock;
        break;
    case ModernFieldCursorBlinking:
        m_ModernConfig.cursorBlinking = !m_ModernConfig.cursorBlinking;
        break;
    case ModernFieldVTTest:
        m_ModernConfig.vtTestEnabled = !m_ModernConfig.vtTestEnabled;
        break;
    case ModernFieldVT52Mode:
        m_ModernConfig.vt52Mode = !m_ModernConfig.vt52Mode;
        break;
    case ModernFieldFontSelection:
    {
        unsigned current = static_cast<unsigned>(m_ModernConfig.fontSelection);
        current = CycleUnsigned(current, 1U, 3U, delta);
        m_ModernConfig.fontSelection = static_cast<EFontSelection>(current);
        break;
    }
    case ModernFieldTextColor:
    {
        unsigned current = static_cast<unsigned>(m_ModernConfig.textColor);
        current = CycleUnsigned(current, 0U, 3U, delta);
        m_ModernConfig.textColor = static_cast<EColorSelection>(current);
        break;
    }
    case ModernFieldBackgroundColor:
    {
        unsigned current = static_cast<unsigned>(m_ModernConfig.backgroundColor);
        current = CycleUnsigned(current, 0U, 3U, delta);
        m_ModernConfig.backgroundColor = static_cast<EColorSelection>(current);
        break;
    }
    case ModernFieldBuzzerVolume:
        if (delta > 0)
        {
            m_ModernConfig.buzzerVolume = (m_ModernConfig.buzzerVolume >= 80U) ? 0U : (m_ModernConfig.buzzerVolume + 10U);
        }
        else
        {
            m_ModernConfig.buzzerVolume = (m_ModernConfig.buzzerVolume == 0U) ? 80U : (m_ModernConfig.buzzerVolume - 10U);
        }
        break;
    case ModernFieldKeyClick:
        m_ModernConfig.keyClick = !m_ModernConfig.keyClick;
        break;
    case ModernFieldKeyAutoRepeat:
        m_ModernConfig.keyAutoRepeat = !m_ModernConfig.keyAutoRepeat;
        break;
    case ModernFieldRepeatDelay:
        if (delta > 0)
        {
            m_ModernConfig.repeatDelayMs = (m_ModernConfig.repeatDelayMs >= kRepeatDelayMaxMs) ? kRepeatDelayMinMs : (m_ModernConfig.repeatDelayMs + 50U);
        }
        else
        {
            m_ModernConfig.repeatDelayMs = (m_ModernConfig.repeatDelayMs <= kRepeatDelayMinMs) ? kRepeatDelayMaxMs : (m_ModernConfig.repeatDelayMs - 50U);
        }
        break;
    case ModernFieldRepeatRate:
        m_ModernConfig.repeatRateCps = CycleUnsigned(m_ModernConfig.repeatRateCps, kRepeatRateMinCps, kRepeatRateMaxCps, delta);
        break;
    case ModernFieldSwitchTxRx:
        m_ModernConfig.switchTxRx = !m_ModernConfig.switchTxRx;
        break;
    case ModernFieldWlanHostAutoStart:
        m_ModernConfig.wlanModePolicy = CycleUnsigned(m_ModernConfig.wlanModePolicy, 0U, 2U, delta);
        break;
    case ModernFieldLogOutput:
    {
        static unsigned toggleBitIndex = 0;
        const unsigned bitCount = 3U;
        if (delta > 0)
        {
            toggleBitIndex = (toggleBitIndex + 1U) % bitCount;
        }
        else
        {
            toggleBitIndex = (toggleBitIndex == 0U) ? (bitCount - 1U) : (toggleBitIndex - 1U);
        }

        m_ModernConfig.logOutput ^= (1U << toggleBitIndex);
        m_ModernConfig.logOutput &= 0x7U;
        break;
    }
    case ModernFieldLogFileName:
    {
        int match = -1;
        for (unsigned i = 0; i < kPresetLogFileCount; ++i)
        {
            if (strcmp(m_ModernConfig.logFileName, kPresetLogFiles[i]) == 0)
            {
                match = static_cast<int>(i);
                break;
            }
        }
        if (match < 0)
        {
            match = (delta > 0) ? 0 : static_cast<int>(kPresetLogFileCount - 1U);
        }
        else
        {
            match += (delta > 0) ? 1 : -1;
            if (match < 0)
            {
                match = static_cast<int>(kPresetLogFileCount - 1U);
            }
            else if (match >= static_cast<int>(kPresetLogFileCount))
            {
                match = 0;
            }
        }
        strncpy(m_ModernConfig.logFileName, kPresetLogFiles[match], sizeof(m_ModernConfig.logFileName) - 1);
        m_ModernConfig.logFileName[sizeof(m_ModernConfig.logFileName) - 1] = '\0';
        break;
    }
    default:
        break;
    }
}

void CTSetup::FormatModernValue(TModernField field, char *pBuffer, size_t bufferSize) const
{
    if (pBuffer == nullptr || bufferSize == 0)
    {
        return;
    }

    pBuffer[0] = '\0';
    CString text;
    switch (field)
    {
    case ModernFieldLineEnding:
        text = kLineEndingNames[m_ModernConfig.lineEnding <= 2U ? m_ModernConfig.lineEnding : 0U];
        break;
    case ModernFieldBaudRate:
        text.Format("%u", m_ModernConfig.baudRate);
        break;
    case ModernFieldSerialBits:
        text.Format("%u", m_ModernConfig.serialBits);
        break;
    case ModernFieldSerialParity:
        text = kParityNames[m_ModernConfig.serialParity <= 2U ? m_ModernConfig.serialParity : 0U];
        break;
    case ModernFieldCursorType:
        text = m_ModernConfig.cursorBlock ? "Block" : "Underline";
        break;
    case ModernFieldCursorBlinking:
        text = BoolName(m_ModernConfig.cursorBlinking);
        break;
    case ModernFieldVTTest:
        text = BoolName(m_ModernConfig.vtTestEnabled);
        break;
    case ModernFieldVT52Mode:
        text = BoolName(m_ModernConfig.vt52Mode);
        break;
    case ModernFieldFontSelection:
    {
        unsigned idx = static_cast<unsigned>(m_ModernConfig.fontSelection);
        idx = (idx >= 1U && idx <= 3U) ? (idx - 1U) : 1U;
        text = kFontNames[idx];
        break;
    }
    case ModernFieldTextColor:
        text = kColorNames[static_cast<unsigned>(m_ModernConfig.textColor) <= 3U ? static_cast<unsigned>(m_ModernConfig.textColor) : 0U];
        break;
    case ModernFieldBackgroundColor:
        text = kColorNames[static_cast<unsigned>(m_ModernConfig.backgroundColor) <= 3U ? static_cast<unsigned>(m_ModernConfig.backgroundColor) : 0U];
        break;
    case ModernFieldBuzzerVolume:
        text.Format("%u%%", m_ModernConfig.buzzerVolume);
        break;
    case ModernFieldKeyClick:
        text = BoolName(m_ModernConfig.keyClick);
        break;
    case ModernFieldKeyAutoRepeat:
        text = BoolName(m_ModernConfig.keyAutoRepeat);
        break;
    case ModernFieldRepeatDelay:
        text.Format("%u ms", m_ModernConfig.repeatDelayMs);
        break;
    case ModernFieldRepeatRate:
        text.Format("%u cps", m_ModernConfig.repeatRateCps);
        break;
    case ModernFieldSwitchTxRx:
        text = BoolName(m_ModernConfig.switchTxRx);
        break;
    case ModernFieldWlanHostAutoStart:
        text = kWlanModeNames[m_ModernConfig.wlanModePolicy <= 2U ? m_ModernConfig.wlanModePolicy : 0U];
        break;
    case ModernFieldLogOutput:
        text = kLogOutputNames[m_ModernConfig.logOutput <= 7U ? m_ModernConfig.logOutput : 0U];
        break;
    case ModernFieldLogFileName:
        text = m_ModernConfig.logFileName;
        break;
    default:
        break;
    }

    strncpy(pBuffer, text.c_str(), bufferSize - 1);
    pBuffer[bufferSize - 1] = '\0';
}

void CTSetup::UpdateTabCursor()
{
    if (m_pRenderer == nullptr)
    {
        return;
    }

    if (m_TabCols == 0)
    {
        return;
    }

    if (m_TabEditCol >= m_TabCols)
    {
        m_TabEditCol = m_TabCols - 1;
    }

    m_pRenderer->Goto(m_TabRow, m_TabEditCol);
}

void CTSetup::UpdateTabCell()
{
    if (m_pRenderer == nullptr || m_pConfig == nullptr || m_TabCols == 0)
    {
        return;
    }

    EColorSelection fgSel = TerminalColorGreen;
    EColorSelection bgSel = TerminalColorBlack;
    if (m_pConfig != nullptr)
    {
        fgSel = m_pConfig->GetTextColor();
        bgSel = m_pConfig->GetBackgroundColor();
    }

    const TRendererColor fgColor = m_pRenderer->MapColor(fgSel);
    const TRendererColor bgColor = m_pRenderer->MapColor(bgSel);
    m_pRenderer->SetColors(fgColor, bgColor);

    const char ch = m_pConfig->IsTabStop(m_TabEditCol) ? 'T' : ' ';
    m_pRenderer->Goto(m_TabRow, m_TabEditCol);
    m_pRenderer->Write(&ch, 1);
    m_pRenderer->Goto(m_TabRow, m_TabEditCol);
}
