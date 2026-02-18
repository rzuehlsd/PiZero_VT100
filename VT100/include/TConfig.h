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

#pragma once

// Include Circle core components
#include <circle/sched/task.h>
#include <circle/string.h>
#include <fatfs/ff.h>

#include "TColorPalette.h"
#include "TFontConverter.h"

/**
 * @file TConfig.h
 * @brief Declares the configuration task that persists and exposes VT100 settings.
 * @details CTConfig loads user preferences from the SD card, provides defaults
 * for first boot, and offers synchronized runtime accessors for other
 * subsystems. It also interprets the human-readable configuration file and
 * converts high-level choices, such as colors, fonts, and logging backends,
 * into concrete values consumed by the renderer, UART, and logging facilities.
 */

// Forward declarations and includes for classes used in this module
class CKernel;

// Simple config parameter structure
struct TConfigParam
{
    const char *keyword;
    unsigned int *variable;    // Pointer to integer variable
    unsigned int defaultValue; // Default value
    const char *description;   // Description with value meanings
};

/**
 * @class CTConfig
 * @brief Cooperative task responsible for configuration persistence and lookup.
 * @details The configuration task boots with defaults, optionally loads the
 * persisted file, and offers getters/setters for every tunable VT100 parameter.
 * Other subsystems depend on CTConfig to discover active fonts, color themes,
 * serial behavior, and logging destinations. It also provides helper utilities
 * to decode bitmasks and emit state summaries for diagnostics.
 */
class CTConfig : public CTask
{
public:
    static constexpr unsigned int TabStopsMax = 160U;
    /// \brief Access the singleton configuration task.
    /// \return Pointer to the configuration task instance.
    static CTConfig *Get(void);

    /// \brief Construct the configuration task with default values.
    CTConfig(void);
    /// \brief Release configuration resources.
    ~CTConfig(void);

    /// \brief Initialize the configuration task and load defaults.
    /// \return TRUE on success, FALSE otherwise.
    boolean Initialize(void);

    /// \brief Scheduler entry point to service configuration events.
    void Run() override;

    /// \brief Load configuration from persistent storage if present.
    /// \return TRUE if loading succeeded, FALSE otherwise.
    boolean LoadFromFile(void);
    /// \brief Save current configuration values to persistent storage.
    /// \return TRUE if writing succeeded, FALSE otherwise.
    boolean SaveToFile(void);

    // --- Configuration Parameters: Getters and Setters ---

    /// \brief Retrieve the line ending mode (0=LF, 1=CRLF, 2=CR).
    /// \return Current line ending mode.
    unsigned int GetLineEndingMode(void) const { return m_LineEnding; }
    /// \brief Update the configured line ending mode.
    /// \param mode New mode value (0=LF, 1=CRLF, 2=CR).
    void SetLineEndingMode(unsigned int mode);

    /// \brief Retrieve the configured serial baud rate.
    /// \return Baud rate in bits per second.
    unsigned int GetBaudRate(void) const { return m_BaudRate; }
    /// \brief Set the serial baud rate.
    /// \param baudRate Desired baud rate.
    void SetBaudRate(unsigned int baudRate);

    /// \brief Check whether the cursor is configured as block style.
    /// \return TRUE if block cursor is selected, FALSE for underline.
    boolean GetCursorBlock(void) const { return m_CursorType != 0; }
    /// \brief Change cursor style.
    /// \param block TRUE to select block cursor, FALSE for underline.
    void SetCursorBlock(boolean block);

    /// \brief Check whether the cursor is configured to blink.
    /// \return TRUE if blinking is enabled.
    boolean GetCursorBlinking(void) const { return m_CursorBlinking != 0; }
    /// \brief Toggle cursor blinking behavior.
    /// \param blinking TRUE to enable blinking.
    void SetCursorBlinking(boolean blinking);

    /// \brief Check whether the VT test runner is enabled.
    /// \return TRUE if enabled.
    boolean GetVTTestEnabled(void) const { return m_VTTestEnabled != 0; }
    /// \brief Enable or disable the VT test runner.
    /// \param enabled TRUE to enable tests.
    void SetVTTestEnabled(boolean enabled);

    /// \brief Check whether VT52 emulation mode is enabled.
    /// \return TRUE if VT52 mode is enabled.
    boolean GetVT52ModeEnabled(void) const { return m_VT52Mode != 0; }
    /// \brief Enable or disable VT52 emulation mode.
    /// \param enabled TRUE to enable VT52 mode.
    void SetVT52ModeEnabled(boolean enabled);

    /// \brief Retrieve the configured log output bitmask.
    /// \return Bitmask selecting log destinations.
    unsigned int GetLogOutput(void) const { return m_LogOutput; }
    /// \brief Update the log output bitmask.
    /// \param logOutput Bitmask selecting log destinations.
    void SetLogOutput(unsigned int logOutput);

    /// \brief Retrieve the configured log file name.
    /// \return Pointer to the log file name string.
    const char *GetLogFileName(void) const { return m_LogFileName; }
    /// \brief Set the configured log file name.
    /// \param pFileName Null-terminated file name.
    void SetLogFileName(const char *pFileName);

    /// \brief Retrieve the configured text color index.
    /// \return Current text color selection.
    EColorSelection GetTextColor(void) const { return m_TextColorIndex; }
    /// \brief Set the configured text color index.
    /// \param color New text color selection.
    void SetTextColor(EColorSelection color);

    /// \brief Retrieve the configured background color index.
    /// \return Current background color selection.
    EColorSelection GetBackgroundColor(void) const { return m_BackgroundColorIndex; }
    /// \brief Set the configured background color index.
    /// \param color New background color selection.
    void SetBackgroundColor(EColorSelection color);

    /// \brief Retrieve the configured font selection.
    /// \return Current font selection value.
    EFontSelection GetFontSelection(void) const { return static_cast<EFontSelection>(m_FontSelection); }
    /// \brief Set the active font selection.
    /// \param selection Font selection enum value.
    void SetFontSelection(EFontSelection selection);

    /// \brief Retrieve the buzzer volume setting in percent.
    /// \return Volume percent (0-100).
    unsigned int GetBuzzerVolume(void) const { return m_BuzzerVolume; }
    /// \brief Adjust the buzzer volume setting.
    /// \param volume Volume percent (0-100).
    void SetBuzzerVolume(unsigned int volume);

    /// \brief Check whether key click feedback is enabled.
    /// \return TRUE if enabled.
    unsigned int GetKeyClick(void) const { return m_KeyClick; }
    /// \brief Enable or disable key click feedback.
    /// \param enabled TRUE to enable click feedback.
    void SetKeyClick(boolean enabled);

    /// \brief Check whether TX/RX wiring is swapped.
    /// \return TRUE if swapped.
    unsigned int GetSwitchTxRx(void) const { return m_SwitchTxRx; }
    /// \brief Enable or disable TX/RX swap mode.
    /// \param enabled TRUE to swap the wiring.
    void SetSwitchTxRx(boolean enabled);

    /// \brief Check whether WLAN host mode auto-start is enabled.
    /// \return 1 if host mode auto-start is enabled, else 0.
    unsigned int GetWlanHostAutoStart(void) const { return m_WlanHostAutoStart; }
    /// \brief Enable or disable WLAN host mode auto-start on telnet connect.
    /// \param enabled TRUE to auto-enable host mode for new sessions.
    void SetWlanHostAutoStart(boolean enabled);

    /// \brief Retrieve key repeat delay in milliseconds.
    /// \return Delay in milliseconds.
    unsigned int GetKeyRepeatDelayMs(void) const { return m_KeyRepeatDelayMs; }
    /// \brief Check whether keyboard auto-repeat is enabled.
    /// \return TRUE if auto-repeat is enabled.
    boolean GetKeyAutoRepeatEnabled(void) const { return m_KeyAutoRepeat != 0; }
    /// \brief Enable or disable keyboard auto-repeat.
    /// \param enabled TRUE to enable auto-repeat.
    void SetKeyAutoRepeatEnabled(boolean enabled);
    /// \brief Set key repeat delay.
    /// \param delayMs Delay in milliseconds.
    void SetKeyRepeatDelayMs(unsigned int delayMs);

    /// \brief Retrieve key repeat rate in characters per second.
    /// \return Rate in characters per second.
    unsigned int GetKeyRepeatRateCps(void) const { return m_KeyRepeatRateCps; }
    /// \brief Set key repeat rate.
    /// \param rateCps Rate in characters per second.
    void SetKeyRepeatRateCps(unsigned int rateCps);

    /// \brief Check whether screen colors are inverted (screen mode).
    /// \return TRUE if foreground/background are swapped.
    boolean GetScreenInverted(void) const { return m_ScreenInverted != 0; }
    /// \brief Enable or disable screen color inversion.
    /// \param inverted TRUE to swap foreground/background colors.
    void SetScreenInverted(boolean inverted);

    /// \brief Check whether smooth scrolling animation is enabled.
    /// \return TRUE if enabled.
    boolean GetSmoothScrollEnabled(void) const { return m_SmoothScrollEnabled != 0; }
    /// \brief Enable or disable smooth scrolling animation.
    /// \param enabled TRUE to enable smooth scrolling.
    void SetSmoothScrollEnabled(boolean enabled);

    /// \brief Check whether automatic line wrap-around is enabled.
    /// \return TRUE if enabled.
    boolean GetWrapAroundEnabled(void) const { return m_WrapAroundEnabled != 0; }
    /// \brief Enable or disable automatic line wrap-around.
    /// \param enabled TRUE to wrap to next line at right margin.
    void SetWrapAroundEnabled(boolean enabled);

    /// \brief Retrieve configured UART data bits.
    /// \return Data bits (7 or 8).
    unsigned int GetSerialDataBits(void) const { return m_SerialDataBits; }
    /// \brief Set UART data bits.
    /// \param dataBits Data bits (7 or 8).
    void SetSerialDataBits(unsigned int dataBits);

    /// \brief Retrieve configured UART parity mode.
    /// \return Parity mode (0=none, 1=even, 2=odd).
    unsigned int GetSerialParityMode(void) const { return m_SerialParityMode; }
    /// \brief Set UART parity mode.
    /// \param parityMode Parity mode (0=none, 1=even, 2=odd).
    void SetSerialParityMode(unsigned int parityMode);

    /// \brief Query whether software flow control (XON/XOFF) is enabled.
    /// \return TRUE if enabled.
    boolean GetSoftwareFlowControl(void) const { return m_SoftwareFlowControl != 0; }
    /// \brief Enable or disable software flow control.
    /// \param enabled TRUE to enable software flow control.
    void SetSoftwareFlowControl(boolean enabled);

    /// \brief Query whether margin bell is enabled.
    /// \return TRUE if enabled.
    boolean GetMarginBellEnabled(void) const { return m_MarginBellEnabled != 0; }
    /// \brief Enable or disable margin bell.
    /// \param enabled TRUE to enable margin bell.
    void SetMarginBellEnabled(boolean enabled);

    // --- End Configuration Parameters ---

    /// \brief Decode the log output bitmask into individual booleans.
    /// \param screen TRUE if screen logging is enabled (output).
    /// \param file TRUE if file logging is enabled (output).
    /// \param wlan TRUE if WLAN logging is enabled (output).
    void ResolveLogOutputs(bool &screen, bool &file, bool &wlan) const;
    /// \brief Obtain a textual description of the current line ending mode.
    /// \return Pointer to static string describing the mode.
    const char *GetLineEndingModeString(void) const;
    /// \brief Emit the active configuration to the logger.
    void logConfig(void) const;

    /// \brief Query whether a tab stop is set at the specified column.
    /// \note Column index is 0-based.
    bool IsTabStop(unsigned int column) const;
    /// \brief Set or clear a tab stop at the specified column.
    /// \note Column index is 0-based.
    void SetTabStop(unsigned int column, bool enabled);
    /// \brief Reset tab stops to default 8-column positions.
    /// \note Columns are 0-based; default stops set at 8,16,24,...
    void InitDefaultTabStops(unsigned int columns = TabStopsMax);

private:
    /// \brief Load and apply settings from the configuration file.
    /// \return TRUE if parsing succeeded.
    boolean LoadConfigFromFile(void);
    /// \brief Restore default configuration values.
    void LoadDefaults(void);
    /// \brief Parse a single configuration line.
    /// \param pLine Null-terminated configuration line.
    /// \return TRUE if the line was processed successfully.
    boolean ParseConfigLine(const char *pLine);
    /// \brief Trim leading and trailing whitespace in-place.
    /// \param pString Mutable string to trim.
    void TrimWhitespace(char *pString);

    // Configuration variables (all integers for simplicity)
    unsigned int m_LineEnding;              // 0=LF, 1=CRLF
    unsigned int m_BaudRate;                // Baud rate
    unsigned int m_CursorType;              // 0=underline, 1=block
    unsigned int m_CursorBlinking;          // 0=steady, 1=blinking
    unsigned int m_VTTestEnabled;           // 0=off, 1=on
    unsigned int m_VT52Mode;                // 0=ANSI, 1=VT52
    unsigned int m_LogOutput;               // bitmask: 1=screen, 2=file, 4=wlan
    EColorSelection m_TextColorIndex;       // Color index for text (EColorSelection)
    EColorSelection m_BackgroundColorIndex; // Color index for background (EColorSelection)
    unsigned int m_FontSelection;           // Encoded font selection identifier
    unsigned int m_BuzzerVolume;            // 0-100% duty cycle for buzzer
    unsigned int m_KeyClick;                // 0=disabled, 1=enabled key click feedback
    unsigned int m_SwitchTxRx;              // 0=normal wiring, 1=swap TX/RX via GPIO16
    unsigned int m_WlanHostAutoStart;       // 0=command/log mode on connect, 1=host mode auto-start
    unsigned int m_KeyAutoRepeat;           // 0=disabled, 1=enabled keyboard auto-repeat
    unsigned int m_KeyRepeatDelayMs;        // Key repeat delay in milliseconds
    unsigned int m_KeyRepeatRateCps;        // Repeat frequency in characters per second
    unsigned int m_ScreenInverted;          // 0=normal, 1=swap fg/bg screen colors
    unsigned int m_SmoothScrollEnabled;     // 0=off, 1=on smooth scrolling animation
    unsigned int m_WrapAroundEnabled;       // 0=off hold at right margin, 1=on wrap to next line
    unsigned int m_SerialDataBits;          // UART data bits (7 or 8)
    unsigned int m_SerialParityMode;        // UART parity (0=none, 1=even, 2=odd)
    unsigned int m_SoftwareFlowControl;     // 0=off, 1=on software flow control (XON/XOFF)
    unsigned int m_MarginBellEnabled;       // 0=off, 1=on margin bell (8 columns before right margin)
    char m_LogFileName[64];                 // Log filename (string, special handling)
    bool m_TabStops[TabStopsMax];           // Tab stop positions (0-based columns)

    static const char ConfigFileName[];
    TConfigParam s_ConfigParams[24]; // Instance array for config params
};
