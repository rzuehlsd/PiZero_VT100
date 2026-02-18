//------------------------------------------------------------------------------
// Module:        CTConfig
// Description:   Loads VT100 configuration from storage and exposes runtime settings.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

// Include class header
#include "TConfig.h"

// Full class definitions for classes used in this module
// Include Circle core components
#include "kernel.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <circle/string.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <circle/sched/scheduler.h>

// #include "log_device.h"  // Include AFTER circle/logger.h to override macros
// #include "framebuffer.h"

// Include application components
#include "TRenderer.h"



LOGMODULE("TConfig");

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CTConfig *s_pThis = 0;
CTConfig *CTConfig::Get(void)
{
    if (s_pThis == 0)
    {
        s_pThis = new CTConfig();
    }
    return s_pThis;
}

const char CTConfig::ConfigFileName[] = "SD:/VT100.txt";

namespace
{
    constexpr unsigned int KeyRepeatDelayMinMs = 250U;
    constexpr unsigned int KeyRepeatDelayMaxMs = 1000U;
    constexpr unsigned int KeyRepeatRateMinCps = 2U;
    constexpr unsigned int KeyRepeatRateMaxCps = 20U;

    constexpr unsigned int FontSelectionMin = static_cast<unsigned int>(EFontSelection::VT100Font8x20);
    constexpr unsigned int FontSelectionMax = static_cast<unsigned int>(EFontSelection::VT100Font10x20Solid);
    constexpr unsigned int FontSelectionDefault = static_cast<unsigned int>(EFontSelection::VT100Font10x20);

    void TrimWhitespaceInPlace(char *pString)
    {
        if (pString == nullptr)
        {
            return;
        }

        char *pStart = pString;
        while (*pStart && (*pStart == ' ' || *pStart == '\t'))
        {
            pStart++;
        }

        if (pStart != pString)
        {
            memmove(pString, pStart, strlen(pStart) + 1);
        }

        int len = strlen(pString);
        while (len > 0 && (pString[len - 1] == ' ' ||
                           pString[len - 1] == '\t' ||
                           pString[len - 1] == '\r' ||
                           pString[len - 1] == '\n'))
        {
            pString[len - 1] = '\0';
            len--;
        }
    }

    class ConfigLineReader
    {
    public:
        explicit ConfigLineReader(FIL &file)
            : m_File(file),
              m_BufferPos(0),
              m_BufferLen(0),
              m_EOF(false),
              m_ReadFailed(false),
              m_LastWasCR(false),
              m_LinePos(0),
              m_LineOverflow(false),
              m_TotalBytesRead(0),
              m_PreviewLen(0),
              m_LineNumber(0)
        {
            m_Preview[0] = '\0';
        }

        const char *GetLine()
        {
            while (true)
            {
                if (m_BufferPos >= m_BufferLen)
                {
                    if (m_EOF)
                    {
                        if (m_LineOverflow)
                        {
                            m_LineOverflow = false;
                            m_LinePos = 0;
                            return nullptr;
                        }

                        if (m_LinePos > 0)
                        {
                            m_Line[m_LinePos] = '\0';
                            TrimWhitespaceInPlace(m_Line);
                            if (m_Line[0] == '\0')
                            {
                                m_LinePos = 0;
                                return nullptr;
                            }
                            m_LinePos = 0;
                            ++m_LineNumber;
                            return m_Line;
                        }

                        return nullptr;
                    }

                    if (!RefillBuffer())
                    {
                        return nullptr;
                    }

                    if (m_BufferLen == 0)
                    {
                        continue;
                    }
                }

                char c = m_Buffer[m_BufferPos++];

                AppendPreview(c);

                if (c == '\r' || c == '\n')
                {
                    if (c == '\n' && m_LastWasCR)
                    {
                        m_LastWasCR = false;
                        continue;
                    }

                    if (!m_LineOverflow)
                    {
                        m_Line[m_LinePos] = '\0';
                        TrimWhitespaceInPlace(m_Line);
                        if (m_Line[0] != '\0')
                        {
                            m_LinePos = 0;
                            ++m_LineNumber;
                            m_LastWasCR = (c == '\r');
                            return m_Line;
                        }
                        m_LinePos = 0;
                    }
                    else
                    {
                        m_LineOverflow = false;
                        m_LinePos = 0;
                    }

                    m_LastWasCR = (c == '\r');
                    continue;
                }

                m_LastWasCR = false;

                if (m_LineOverflow)
                {
                    continue;
                }

                if (m_LinePos < sizeof(m_Line) - 1)
                {
                    m_Line[m_LinePos++] = c;
                }
                else
                {
                    LOGWARN("Config: Line %u exceeds %u characters, skipping", m_LineNumber + 1,
                            (unsigned int)(sizeof(m_Line) - 1));
                    m_LineOverflow = true;
                    m_LinePos = 0;
                }
            }
        }

        UINT GetTotalBytesRead() const { return m_TotalBytesRead; }
        const char *GetPreview() const { return m_Preview; }
        unsigned int GetParsedLineCount() const { return m_LineNumber; }
        bool HasError() const { return m_ReadFailed; }

    private:
        static const UINT ChunkSize = 512;

        bool RefillBuffer()
        {
            UINT bytesRead = 0;
            FRESULT result = f_read(&m_File, m_Buffer, ChunkSize, &bytesRead);
            if (result != FR_OK)
            {
                LOGERR("Config: File read failed (err=%d)", (int)result);
                m_ReadFailed = true;
                m_EOF = true;
                m_BufferLen = 0;
                m_BufferPos = 0;
                return false;
            }

            m_BufferPos = 0;
            m_BufferLen = bytesRead;
            m_TotalBytesRead += bytesRead;
            if (bytesRead == 0)
            {
                m_EOF = true;
            }

            return true;
        }

        void AppendPreview(char c)
        {
            if (m_PreviewLen < sizeof(m_Preview) - 1)
            {
                m_Preview[m_PreviewLen++] = c;
                m_Preview[m_PreviewLen] = '\0';
            }
        }

        FIL &m_File;
        char m_Buffer[ChunkSize];
        UINT m_BufferPos;
        UINT m_BufferLen;
        bool m_EOF;
        bool m_ReadFailed;
        bool m_LastWasCR;
        char m_Line[256];
        unsigned int m_LinePos;
        bool m_LineOverflow;
        UINT m_TotalBytesRead;
        char m_Preview[201];
        unsigned int m_PreviewLen;
        unsigned int m_LineNumber;
    };
}

static const char *FontSelectionToString(EFontSelection fontSelection)
{
    switch (fontSelection)
    {
    case EFontSelection::VT100Font8x20:
        return "8x20";
    case EFontSelection::VT100Font10x20:
        return "10x20";
    case EFontSelection::VT100Font10x20Solid:
        return "10x20Solid";
    default:
        return "Default";
    }
}

void CTConfig::logConfig(void) const
{
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);
    LOGNOTE("Pi VT100 020: Config Settings");
    LOGNOTE("Screen: %ux%u", CTRenderer::Get()->GetWidth(), CTRenderer::Get()->GetHeight());
    LOGNOTE("Serial: %u baud", GetBaudRate());
        LOGNOTE("Serial framing: %u data bits, parity=%s",
            GetSerialDataBits(),
            GetSerialParityMode() == 0 ? "none" : (GetSerialParityMode() == 1 ? "even" : "odd"));
    LOGNOTE("Serial flow: software XON/XOFF %s", GetSoftwareFlowControl() ? "enabled" : "disabled");
            LOGNOTE("Margin bell: %s", GetMarginBellEnabled() ? "enabled" : "disabled");
    LOGNOTE("Line endings: %s", GetLineEndingModeString());
    LOGNOTE("Cursor: %s, %s", GetCursorBlock() ? "block" : "underline", GetCursorBlinking() ? "blinking" : "solid");
    LOGNOTE("VT test: %s", GetVTTestEnabled() ? "enabled" : "disabled");
    LOGNOTE("Terminal mode: %s", GetVT52ModeEnabled() ? "VT52" : "ANSI");
    LOGNOTE("Font: %u", GetFontSelection());
    LOGNOTE("Display: text color index=%d, background color index=%d", (int)GetTextColor(), (int)GetBackgroundColor());
    LOGNOTE("Buzzer: %u%% volume", GetBuzzerVolume());
    LOGNOTE("Key click: %s", GetKeyClick() ? "enabled" : "disabled");
    LOGNOTE("Key auto-repeat: %s", GetKeyAutoRepeatEnabled() ? "enabled" : "disabled");
    LOGNOTE("TX/RX wiring: %s", GetSwitchTxRx() ? "swapped" : "normal");
    LOGNOTE("WLAN host auto-start: %s", GetWlanHostAutoStart() ? "enabled" : "disabled");
    LOGNOTE("Screen mode: %s", GetScreenInverted() ? "inverse" : "normal");
    LOGNOTE("Smooth scroll: %s", GetSmoothScrollEnabled() ? "enabled" : "disabled");
    LOGNOTE("Wrap around: %s", GetWrapAroundEnabled() ? "enabled" : "disabled");
    LOGNOTE("Key repeat: delay=%u ms, rate=%u cps", GetKeyRepeatDelayMs(), GetKeyRepeatRateCps());
    bool logScreen = false;
    bool logFile = false;
    bool logWlan = false;
    ResolveLogOutputs(logScreen, logFile, logWlan);
        LOGNOTE("Logging: outputs -> screen=%s, file=%s, wlan=%s (mode %u)",
            logScreen ? "on" : "off",
            logFile ? "on" : "off",
            logWlan ? "on" : "off",
            GetLogOutput());
        LOGNOTE("Logging: active file=%s", GetLogFileName());
}

CTConfig::CTConfig(void) : CTask()
{
    strcpy(m_LogFileName, "vt100.log");

    // Initialize configuration parameter table with direct references
    static const TConfigParam configParams[] = {
        {"line_ending", &m_LineEnding, 0, "Line ending (0=LF, 1=CRLF, 2=CR)"},
        {"baud_rate", &m_BaudRate, 115200, "Serial baud rate"},
        {"serial_bits", &m_SerialDataBits, 8, "UART data bits (7 or 8)"},
        {"serial_parity", &m_SerialParityMode, 0, "UART parity (0=none, 1=even, 2=odd)"},
        {"cursor_type", &m_CursorType, 0, "Cursor type (0=underline, 1=block)"},
        {"cursor_blinking", &m_CursorBlinking, 0, "Cursor blinking (0=steady, 1=blinking)"},
        {"vt_test", &m_VTTestEnabled, 0, "VT test runner (0=off, 1=on)"},
        {"vt52_mode", &m_VT52Mode, 0, "Terminal mode (0=ANSI, 1=VT52)"},
        {"log_output", &m_LogOutput, 0, "Log output (0=off, 1=screen, 2=file, 3=wlan, 4=screen+file, 5=screen+wlan, 6=file+wlan, 7=screen+file+wlan)"},
        {"text_color", (unsigned int*)&m_TextColorIndex, 1, "Text color index (0=black,1=white,2=amber,3=green)"},
        {"background_color", (unsigned int*)&m_BackgroundColorIndex, 0, "Background color index (0=black,1=white,2=amber,3=green)"},
        {"font_selection", &m_FontSelection, FontSelectionDefault, "Font selection (1=8x20,2=10x20,3=10x20Solid)"},
        {"buzzer_volume", &m_BuzzerVolume, 50, "Buzzer volume (0-100 percent duty cycle)"},
        {"key_click", &m_KeyClick, 1, "Key click feedback (0=off, 1=on)"},
        {"key_auto_repeat", &m_KeyAutoRepeat, 1, "Keyboard auto-repeat (0=off, 1=on)"},
        {"smooth_scroll", &m_SmoothScrollEnabled, 1, "Smooth scroll animation (0=off, 1=on)"},
        {"wrap_around", &m_WrapAroundEnabled, 1, "Wrap around at right margin (0=off, 1=on)"},
        {"switch_txrx", &m_SwitchTxRx, 0, "Swap TX/RX wiring using GPIO16 (0=normal, 1=swapped)"},
        {"flow_control", &m_SoftwareFlowControl, 0, "Software flow control (0=off, 1=on XON/XOFF)"},
        {"margin_bell", &m_MarginBellEnabled, 0, "Margin bell (0=off, 1=on; rings 8 columns before right margin)"},
        {"wlan_host_autostart", &m_WlanHostAutoStart, 0, "Auto-start WLAN host bridge mode on telnet connect (0=off, 1=on)"},
        {"repeat_delay_ms", &m_KeyRepeatDelayMs, KeyRepeatDelayMinMs, "Key repeat delay in milliseconds (250-1000)"},
        {"repeat_rate_cps", &m_KeyRepeatRateCps, 10, "Key repeat rate in characters per second (2-20)"},
        // Note: log_filename is handled as special case in ParseConfigLine()
        {nullptr, nullptr, 0, nullptr} // Terminator
    };

    // Copy to member variable for later use
    memcpy((void *)s_ConfigParams, configParams, sizeof(configParams));

    SetName("Config");
    Suspend();
}

CTConfig::~CTConfig(void)
{
}


boolean CTConfig::Initialize(void)
{
    LoadDefaults();

    LOGNOTE("Config defaults loaded and initialized");
    Start();
    return TRUE;
}

void CTConfig::Run()
{
    static boolean m_Loaded = false;
    while (!IsSuspended())
    {
        // Load configuration from file
        if (!m_Loaded)
            m_Loaded = LoadFromFile();
        CScheduler::Get()->MsSleep(1000);
    }
}

void CTConfig::LoadDefaults(void)
{
    // Use the configuration table to set defaults
    for (int i = 0; s_ConfigParams[i].keyword != nullptr; i++)
    {
        const TConfigParam *param = &s_ConfigParams[i];
        *(param->variable) = param->defaultValue;
    }

    // Handle special case: log filename
    strcpy(m_LogFileName, "vt100.log");

    // Runtime-only setting (intentionally not persisted in VT100.txt)
    m_ScreenInverted = 0U;

    InitDefaultTabStops(TabStopsMax);

    LOGNOTE("Config: Defaults loaded");
}

bool CTConfig::IsTabStop(unsigned int column) const
{
    if (column >= TabStopsMax)
    {
        return false;
    }
    return m_TabStops[column];
}

void CTConfig::SetTabStop(unsigned int column, bool enabled)
{
    if (column >= TabStopsMax)
    {
        return;
    }
    m_TabStops[column] = enabled;
}

void CTConfig::InitDefaultTabStops(unsigned int columns)
{
    for (unsigned int i = 0; i < TabStopsMax; ++i)
    {
        m_TabStops[i] = false;
    }

    unsigned int limit = columns > TabStopsMax ? TabStopsMax : columns;
    for (unsigned int i = 0; i < limit; ++i)
    {
        if (i > 0 && (i % 8) == 0)
        {
            m_TabStops[i] = true;
        }
    }
}

boolean CTConfig::LoadFromFile(void)
{
    LOGNOTE("Config: Loading from SD...");

    boolean success = LoadConfigFromFile();

    if (success)
    {
        LOGNOTE("Config: File loaded!");
    }
    else
    {
        LOGERR("Config: File not found, using defaults");
    }

    return success;
}

boolean CTConfig::SaveToFile(void)
{
    // Prepare key/value pairs for persistence
    struct KeyValue
    {
        const char *key;
        CString value;
        bool written;
    };

    KeyValue kv[] = {
        {"line_ending", CString(), false},
        {"baud_rate", CString(), false},
        {"serial_bits", CString(), false},
        {"serial_parity", CString(), false},
        {"flow_control", CString(), false},
        {"cursor_type", CString(), false},
        {"cursor_blinking", CString(), false},
        {"vt_test", CString(), false},
        {"vt52_mode", CString(), false},
        {"font_selection", CString(), false},
        {"text_color", CString(), false},
        {"background_color", CString(), false},
        {"buzzer_volume", CString(), false},
        {"key_click", CString(), false},
        {"key_auto_repeat", CString(), false},
        {"smooth_scroll", CString(), false},
        {"wrap_around", CString(), false},
        {"repeat_delay_ms", CString(), false},
        {"repeat_rate_cps", CString(), false},
        {"switch_txrx", CString(), false},
        {"margin_bell", CString(), false},
        {"wlan_host_autostart", CString(), false},
        {"log_output", CString(), false},
        {"log_filename", CString(), false},
    };

    kv[0].value.Format("%u", m_LineEnding);
    kv[1].value.Format("%u", m_BaudRate);
    kv[2].value.Format("%u", m_SerialDataBits);
    kv[3].value.Format("%u", m_SerialParityMode);
    kv[4].value.Format("%u", m_SoftwareFlowControl);
    kv[5].value.Format("%u", m_CursorType);
    kv[6].value.Format("%u", m_CursorBlinking);
    kv[7].value.Format("%u", m_VTTestEnabled);
    kv[8].value.Format("%u", m_VT52Mode);
    kv[9].value.Format("%u", m_FontSelection);
    kv[10].value.Format("%u", (unsigned)m_TextColorIndex);
    kv[11].value.Format("%u", (unsigned)m_BackgroundColorIndex);
    kv[12].value.Format("%u", m_BuzzerVolume);
    kv[13].value.Format("%u", m_KeyClick);
    kv[14].value.Format("%u", m_KeyAutoRepeat);
    kv[15].value.Format("%u", m_SmoothScrollEnabled);
    kv[16].value.Format("%u", m_WrapAroundEnabled);
    kv[17].value.Format("%u", m_KeyRepeatDelayMs);
    kv[18].value.Format("%u", m_KeyRepeatRateCps);
    kv[19].value.Format("%u", m_SwitchTxRx);
    kv[20].value.Format("%u", m_MarginBellEnabled);
    kv[21].value.Format("%u", m_WlanHostAutoStart);
    kv[22].value.Format("%u", m_LogOutput);
    kv[23].value.Format("%s", m_LogFileName);

    // Attempt to load existing content to preserve comments/order
    CString existing;
    FIL fileRead;
    bool haveExisting = false;
    if (f_open(&fileRead, ConfigFileName, FA_READ | FA_OPEN_EXISTING) == FR_OK)
    {
        char buffer[256];
        UINT bytesRead = 0;
        while (f_read(&fileRead, buffer, sizeof(buffer) - 1, &bytesRead) == FR_OK && bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            existing += buffer;
        }
        f_close(&fileRead);
        haveExisting = existing.GetLength() > 0;
    }

    auto TrimCopy = [](const CString &src) -> CString
    {
        const char *text = src.c_str();
        size_t len = src.GetLength();
        size_t begin = 0;
        while (begin < len && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
        {
            ++begin;
        }
        size_t end = len;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
        {
            --end;
        }

        CString out;
        for (size_t i = begin; i < end; ++i)
        {
            out += text[i];
        }
        return out;
    };

    CString output;
    if (haveExisting)
    {
        // Process line by line, replacing known keys and keeping comments/unknowns
        const char *text = existing.c_str();
        const size_t len = existing.GetLength();
        size_t start = 0;
        while (start < len)
        {
            size_t end = start;
            while (end < len && text[end] != '\n')
            {
                ++end;
            }

            CString line;
            for (size_t i = start; i < end; ++i)
            {
                line += text[i];
            }

            // Preserve line endings as \n
            CString trimmed = TrimCopy(line);
            if (trimmed.GetLength() == 0 || trimmed.c_str()[0] == '#')
            {
                output += line;
                output += "\n";
            }
            else
            {
                const char *eq = strchr(trimmed.c_str(), '=');
                if (eq != nullptr)
                {
                    CString key;
                    for (const char *p = trimmed.c_str(); p < eq; ++p)
                    {
                        key += *p;
                    }
                    key = TrimCopy(key);
                    bool replaced = false;
                    for (unsigned i = 0; i < sizeof(kv) / sizeof(kv[0]); ++i)
                    {
                        if (key.Compare(kv[i].key) == 0)
                        {
                            CString formatted;
                            formatted.Format("%s=%s\n", kv[i].key, kv[i].value.c_str());
                            output += formatted.c_str();
                            kv[i].written = true;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced)
                    {
                        output += line;
                        output += "\n";
                    }
                }
                else
                {
                    output += line;
                    output += "\n";
                }
            }

            start = (end < len && text[end] == '\n') ? end + 1 : end;
        }
    }

    // Append any missing keys
    for (unsigned i = 0; i < sizeof(kv) / sizeof(kv[0]); ++i)
    {
        if (!kv[i].written)
        {
            CString formatted;
            formatted.Format("%s=%s\n", kv[i].key, kv[i].value.c_str());
            output += formatted.c_str();
        }
    }

    // Fallback: if no existing content and output is empty, create minimal header
    if (!haveExisting && output.GetLength() == 0)
    {
        output.Format("# VT100 Terminal Configuration File\n# Auto-generated by SET-UP\n\n");
        for (unsigned i = 0; i < sizeof(kv) / sizeof(kv[0]); ++i)
        {
            CString formatted;
            formatted.Format("%s=%s\n", kv[i].key, kv[i].value.c_str());
            output += formatted.c_str();
        }
    }

    FIL file;
    FRESULT openResult = f_open(&file, ConfigFileName, FA_WRITE | FA_CREATE_ALWAYS);
    if (openResult != FR_OK)
    {
        LOGERR("Config: Save failed, cannot open %s (err=%d)", ConfigFileName, (int)openResult);
        return FALSE;
    }

    auto writeText = [&file](const char *text) -> boolean
    {
        if (text == nullptr)
        {
            return FALSE;
        }

        const UINT len = (UINT)strlen(text);
        UINT written = 0;
        FRESULT wr = f_write(&file, text, len, &written);
        if (wr != FR_OK || written != len)
        {
            return FALSE;
        }
        return TRUE;
    };

    boolean ok = writeText(output.c_str());

    FRESULT closeResult = f_close(&file);
    if (!ok || closeResult != FR_OK)
    {
        LOGERR("Config: Save failed while writing %s", ConfigFileName);
        return FALSE;
    }

    LOGNOTE("Config: Saved to %s", ConfigFileName);
    return TRUE;
}

boolean CTConfig::LoadConfigFromFile(void)
{
    // Filesystem is already mounted by kernel

    FIL file;
    if (f_open(&file, ConfigFileName, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        LOGWARN("Config: File open failed");
        return FALSE;
    }

    ConfigLineReader reader(file);

    const char *lineText = nullptr;
    while ((lineText = reader.GetLine()) != nullptr)
    {
        LOGNOTE("Config: Parsing line %u: '%s'", reader.GetParsedLineCount(), lineText);
        ParseConfigLine(lineText);
    }

    bool readFailed = reader.HasError();

    f_close(&file);

    if (readFailed)
    {
        LOGNOTE("Config: Read error occurred");
        return FALSE;
    }

    LOGNOTE("Config: Read %u bytes from file", reader.GetTotalBytesRead());
    LOGNOTE("Config: File content (first 200 chars): %.200s", reader.GetPreview());
    LOGNOTE("Config: Parsed %u lines total", reader.GetParsedLineCount());

    return TRUE;
}

boolean CTConfig::ParseConfigLine(const char *pLine)
{
    if (pLine == nullptr || strlen(pLine) == 0)
    {
        return TRUE;
    }

    char LineCopy[256];
    strncpy(LineCopy, pLine, sizeof(LineCopy) - 1);
    LineCopy[sizeof(LineCopy) - 1] = '\0';

    TrimWhitespace(LineCopy);

    // Skip comments and empty lines
    if (LineCopy[0] == '#' || LineCopy[0] == '\0')
    {
        return TRUE;
    }

    // Find '=' separator
    char *equals = strchr(LineCopy, '=');
    if (equals == nullptr)
    {
        return TRUE;
    }

    *equals = '\0';
    char *keyword = LineCopy;
    char *value = equals + 1;

    TrimWhitespace(keyword);
    TrimWhitespace(value);

    // Special case: log_filename (string)
    if (strcmp(keyword, "log_filename") == 0)
    {
        strncpy(m_LogFileName, value, sizeof(m_LogFileName) - 1);
        m_LogFileName[sizeof(m_LogFileName) - 1] = '\0';
        LOGNOTE("Config: log_filename set to %s ", m_LogFileName);
        return TRUE;
    }

    // Use table to find and set parameter
    for (int i = 0; s_ConfigParams[i].keyword != nullptr; i++)
    {
        const TConfigParam *param = &s_ConfigParams[i];

        if (strcmp(keyword, param->keyword) == 0)
        {
            char *endPtr = nullptr;
            unsigned long parsedValue = strtoul(value, &endPtr, 0);
            if (endPtr == value)
            {
                LOGWARN("Config: Failed to parse numeric value for %s (value='%s')", keyword, value);
                return FALSE;
            }

            if (param->variable == (unsigned int*)&m_TextColorIndex || param->variable == (unsigned int*)&m_BackgroundColorIndex)
            {
                if (parsedValue > 3) {
                    LOGWARN("Config: Invalid color index %lu for %s, clamping to 0", parsedValue, keyword);
                    parsedValue = 0;
                }
                *(param->variable) = static_cast<unsigned int>(parsedValue);
                LOGNOTE("Config: Parameter %s set to color index %lu",
                        keyword,
                        parsedValue);
            }
            else if (param->variable == &m_FontSelection)
            {
                unsigned int previousValue = *(param->variable);
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (sanitizedValue < FontSelectionMin || sanitizedValue > FontSelectionMax)
                {
                    LOGWARN("Config: Invalid font_selection %lu, keeping %u", parsedValue, previousValue);
                }
                else
                {
                    *(param->variable) = sanitizedValue;
                    LOGNOTE("Config: Parameter %s set to %u", keyword, *(param->variable));
                }
            }
            else if (param->variable == &m_BuzzerVolume)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative buzzer_volume %s, clamping to 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 80U)
                {
                    LOGWARN("Config: Invalid buzzer_volume %lu, clamping to 80", parsedValue);
                    sanitizedValue = 80U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %u%% duty", keyword, *(param->variable));
            }
            else if (param->variable == &m_SerialDataBits)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative serial_bits %s, using 8", value);
                    sanitizedValue = 8U;
                }
                if (sanitizedValue != 7U && sanitizedValue != 8U)
                {
                    LOGWARN("Config: Invalid serial_bits %lu, clamping to 8", parsedValue);
                    sanitizedValue = 8U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %u", keyword, sanitizedValue);
            }
            else if (param->variable == &m_SerialParityMode)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative serial_parity %s, using 0", value);
                    sanitizedValue = 0U;
                }
                if (sanitizedValue > 2U)
                {
                    LOGWARN("Config: Invalid serial_parity %lu, clamping to 0", parsedValue);
                    sanitizedValue = 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %u", keyword, sanitizedValue);
            }
            else if (param->variable == &m_KeyClick)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative key_click %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid key_click %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_KeyAutoRepeat)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative key_auto_repeat %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid key_auto_repeat %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_WrapAroundEnabled)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative wrap_around %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid wrap_around %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_VTTestEnabled)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative vt_test %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid vt_test %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_VT52Mode)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative vt52_mode %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid vt52_mode %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %s", keyword, sanitizedValue ? "VT52" : "ANSI");
            }
            else if (param->variable == &m_SwitchTxRx)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative switch_txrx %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid switch_txrx %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_WlanHostAutoStart)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative wlan_host_autostart %s, using 0", value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid wlan_host_autostart %lu, clamping to 1", parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_SoftwareFlowControl || param->variable == &m_MarginBellEnabled)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative %s %s, using 0", keyword, value);
                    sanitizedValue = 0U;
                }
                else if (sanitizedValue > 1U)
                {
                    LOGWARN("Config: Invalid %s %lu, clamping to 1", keyword, parsedValue);
                    sanitizedValue = sanitizedValue ? 1U : 0U;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s %s", keyword, sanitizedValue ? "enabled" : "disabled");
            }
            else if (param->variable == &m_KeyRepeatDelayMs)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative repeat_delay_ms %s, using %u", value, KeyRepeatDelayMinMs);
                    sanitizedValue = KeyRepeatDelayMinMs;
                }
                if (sanitizedValue < KeyRepeatDelayMinMs)
                {
                    LOGWARN("Config: repeat_delay_ms %lu below minimum, clamping to %u", parsedValue, KeyRepeatDelayMinMs);
                    sanitizedValue = KeyRepeatDelayMinMs;
                }
                else if (sanitizedValue > KeyRepeatDelayMaxMs)
                {
                    LOGWARN("Config: repeat_delay_ms %lu above maximum, clamping to %u", parsedValue, KeyRepeatDelayMaxMs);
                    sanitizedValue = KeyRepeatDelayMaxMs;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %u ms", keyword, *(param->variable));
            }
            else if (param->variable == &m_KeyRepeatRateCps)
            {
                unsigned int sanitizedValue = static_cast<unsigned int>(parsedValue);
                if (value[0] == '-')
                {
                    LOGWARN("Config: Negative repeat_rate_cps %s, using %u", value, KeyRepeatRateMinCps);
                    sanitizedValue = KeyRepeatRateMinCps;
                }
                if (sanitizedValue < KeyRepeatRateMinCps)
                {
                    LOGWARN("Config: repeat_rate_cps %lu below minimum, clamping to %u", parsedValue, KeyRepeatRateMinCps);
                    sanitizedValue = KeyRepeatRateMinCps;
                }
                else if (sanitizedValue > KeyRepeatRateMaxCps)
                {
                    LOGWARN("Config: repeat_rate_cps %lu above maximum, clamping to %u", parsedValue, KeyRepeatRateMaxCps);
                    sanitizedValue = KeyRepeatRateMaxCps;
                }
                *(param->variable) = sanitizedValue;
                LOGNOTE("Config: Parameter %s set to %u cps", keyword, *(param->variable));
            }
            else
            {
                *(param->variable) = static_cast<unsigned int>(parsedValue);
                LOGNOTE("Config: Parameter %s set to %u (0x%X)",
                        keyword,
                        *(param->variable),
                        *(param->variable));
            }
            return TRUE;
        }
    }

    // Debug: Show unmatched parameters
    LOGWARN("Config: Unknown parameter: '%s' = '%s'", keyword, value);

    return TRUE; // Unknown parameter, but not an error
}

void CTConfig::SetBaudRate(unsigned int baudRate)
{
    m_BaudRate = baudRate;
    LOGNOTE("Config: baud_rate updated to %u", m_BaudRate);
}

void CTConfig::SetLineEndingMode(unsigned int mode)
{
    unsigned int sanitized = mode;
    if (sanitized > 2U)
    {
        sanitized = 0U;
    }
    m_LineEnding = sanitized;
    LOGNOTE("Config: line_ending updated to %u", m_LineEnding);
}

void CTConfig::SetCursorBlock(boolean block)
{
    m_CursorType = block ? 1U : 0U;
    LOGNOTE("Config: cursor_type set to %s", m_CursorType ? "block" : "underline");
}

void CTConfig::SetCursorBlinking(boolean blinking)
{
    m_CursorBlinking = blinking ? 1U : 0U;
    LOGNOTE("Config: cursor_blinking set to %s", m_CursorBlinking ? "true" : "false");
}

void CTConfig::SetVTTestEnabled(boolean enabled)
{
    m_VTTestEnabled = enabled ? 1U : 0U;
    LOGNOTE("Config: vt_test %s", m_VTTestEnabled ? "enabled" : "disabled");
}

void CTConfig::SetVT52ModeEnabled(boolean enabled)
{
    m_VT52Mode = enabled ? 1U : 0U;
    LOGNOTE("Config: vt52_mode set to %s", m_VT52Mode ? "VT52" : "ANSI");
}

void CTConfig::SetLogOutput(unsigned int logOutput)
{
    unsigned int sanitized = logOutput;
    if (sanitized > 7U)
    {
        sanitized = 0U;
    }
    m_LogOutput = sanitized;
    LOGNOTE("Config: log_output updated to %u", m_LogOutput);
}

void CTConfig::ResolveLogOutputs(bool &screen, bool &file, bool &wlan) const
{
    screen = false;
    file = false;
    wlan = false;

    switch (m_LogOutput)
    {
    case 0:
        break;
    case 1:
        screen = true;
        break;
    case 2:
        file = true;
        break;
    case 3:
        wlan = true;
        break;
    case 4:
        screen = true;
        file = true;
        break;
    case 5:
        screen = true;
        wlan = true;
        break;
    case 6:
        file = true;
        wlan = true;
        break;
    case 7:
        screen = true;
        file = true;
        wlan = true;
        break;
    default:
        screen = (m_LogOutput & 0x1U) != 0U;
        file = (m_LogOutput & 0x2U) != 0U;
        wlan = (m_LogOutput & 0x4U) != 0U;
        break;
    }
}

void CTConfig::SetTextColor(EColorSelection color)
{
    unsigned int sanitized = static_cast<unsigned int>(color);
    if (sanitized > static_cast<unsigned int>(TerminalColorGreen))
    {
        sanitized = static_cast<unsigned int>(TerminalColorGreen);
    }
    m_TextColorIndex = static_cast<EColorSelection>(sanitized);
    LOGNOTE("Config: text_color updated to %u", static_cast<unsigned int>(m_TextColorIndex));
}

void CTConfig::SetBackgroundColor(EColorSelection color)
{
    unsigned int sanitized = static_cast<unsigned int>(color);
    if (sanitized > static_cast<unsigned int>(TerminalColorGreen))
    {
        sanitized = static_cast<unsigned int>(TerminalColorBlack);
    }
    m_BackgroundColorIndex = static_cast<EColorSelection>(sanitized);
    LOGNOTE("Config: background_color updated to %u", static_cast<unsigned int>(m_BackgroundColorIndex));
}

void CTConfig::SetLogFileName(const char *pFileName)
{
    if (pFileName == nullptr || pFileName[0] == '\0')
    {
        strncpy(m_LogFileName, "vt100.log", sizeof(m_LogFileName) - 1);
    }
    else
    {
        strncpy(m_LogFileName, pFileName, sizeof(m_LogFileName) - 1);
    }
    m_LogFileName[sizeof(m_LogFileName) - 1] = '\0';
    LOGNOTE("Config: log_filename updated to %s", m_LogFileName);
}

void CTConfig::SetFontSelection(EFontSelection selection)
{
    unsigned int sanitized = static_cast<unsigned int>(selection);
    if (sanitized < FontSelectionMin || sanitized > FontSelectionMax)
    {
        sanitized = FontSelectionDefault;
    }
    m_FontSelection = sanitized;
    LOGNOTE("Config: font_selection updated to %s", FontSelectionToString(static_cast<EFontSelection>(m_FontSelection)));
}

void CTConfig::SetBuzzerVolume(unsigned int volume)
{
    unsigned int sanitized = volume;
    if (sanitized > 80U)
    {
        sanitized = 80U;
    }
    m_BuzzerVolume = sanitized;
    LOGNOTE("Config: buzzer_volume updated to %u", m_BuzzerVolume);
}

void CTConfig::SetKeyClick(boolean enabled)
{
    m_KeyClick = enabled ? 1U : 0U;
    LOGNOTE("Config: key_click %s", m_KeyClick ? "enabled" : "disabled");
}

void CTConfig::SetSwitchTxRx(boolean enabled)
{
    m_SwitchTxRx = enabled ? 1U : 0U;
    LOGNOTE("Config: switch_txrx %s", m_SwitchTxRx ? "enabled" : "disabled");
}

void CTConfig::SetWlanHostAutoStart(boolean enabled)
{
    m_WlanHostAutoStart = enabled ? 1U : 0U;
    LOGNOTE("Config: wlan_host_autostart %s", m_WlanHostAutoStart ? "enabled" : "disabled");
}

void CTConfig::SetKeyAutoRepeatEnabled(boolean enabled)
{
    m_KeyAutoRepeat = enabled ? 1U : 0U;
    LOGNOTE("Config: key_auto_repeat %s", m_KeyAutoRepeat ? "enabled" : "disabled");
}

void CTConfig::SetKeyRepeatDelayMs(unsigned int delayMs)
{
    unsigned int sanitized = delayMs;
    if (sanitized < KeyRepeatDelayMinMs)
    {
        sanitized = KeyRepeatDelayMinMs;
    }
    else if (sanitized > KeyRepeatDelayMaxMs)
    {
        sanitized = KeyRepeatDelayMaxMs;
    }
    m_KeyRepeatDelayMs = sanitized;
    LOGNOTE("Config: repeat_delay_ms updated to %u", m_KeyRepeatDelayMs);
}

void CTConfig::SetKeyRepeatRateCps(unsigned int rateCps)
{
    unsigned int sanitized = rateCps;
    if (sanitized < KeyRepeatRateMinCps)
    {
        sanitized = KeyRepeatRateMinCps;
    }
    else if (sanitized > KeyRepeatRateMaxCps)
    {
        sanitized = KeyRepeatRateMaxCps;
    }
    m_KeyRepeatRateCps = sanitized;
    LOGNOTE("Config: repeat_rate_cps updated to %u", m_KeyRepeatRateCps);
}

void CTConfig::SetScreenInverted(boolean inverted)
{
    m_ScreenInverted = inverted ? 1U : 0U;
    LOGNOTE("Config: screen mode %s", m_ScreenInverted ? "inverse" : "normal");
}

void CTConfig::SetSmoothScrollEnabled(boolean enabled)
{
    m_SmoothScrollEnabled = enabled ? 1U : 0U;
    LOGNOTE("Config: smooth_scroll %s", m_SmoothScrollEnabled ? "enabled" : "disabled");
}

void CTConfig::SetWrapAroundEnabled(boolean enabled)
{
    m_WrapAroundEnabled = enabled ? 1U : 0U;
    LOGNOTE("Config: wrap_around %s", m_WrapAroundEnabled ? "enabled" : "disabled");
}

void CTConfig::SetSerialDataBits(unsigned int dataBits)
{
    unsigned int sanitized = dataBits;
    if (sanitized != 7U && sanitized != 8U)
    {
        sanitized = 8U;
    }
    m_SerialDataBits = sanitized;
    LOGNOTE("Config: serial_bits updated to %u", m_SerialDataBits);
}

void CTConfig::SetSerialParityMode(unsigned int parityMode)
{
    unsigned int sanitized = parityMode;
    if (sanitized > 2U)
    {
        sanitized = 0U;
    }
    m_SerialParityMode = sanitized;
    LOGNOTE("Config: serial_parity updated to %u", m_SerialParityMode);
}

void CTConfig::SetSoftwareFlowControl(boolean enabled)
{
    m_SoftwareFlowControl = enabled ? 1U : 0U;
    LOGNOTE("Config: flow_control %s", m_SoftwareFlowControl ? "enabled" : "disabled");
}

void CTConfig::SetMarginBellEnabled(boolean enabled)
{
    m_MarginBellEnabled = enabled ? 1U : 0U;
    LOGNOTE("Config: margin_bell %s", m_MarginBellEnabled ? "enabled" : "disabled");
}

void CTConfig::TrimWhitespace(char *pString)
{
    TrimWhitespaceInPlace(pString);
}

const char *CTConfig::GetLineEndingModeString(void) const
{
    switch (m_LineEnding)
    {
    case 0:
        return "LF (Unix)";
    case 1:
        return "CRLF (Windows)";
    case 2:
        return "CR (Classic)";
    default:
        return "Unknown";
    }
}
