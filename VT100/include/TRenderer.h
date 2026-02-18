//------------------------------------------------------------------------------
// Module:        CTRenderer
// Description:   Implements the VT100 display pipeline on top of Circle primitives.
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
#include <circle/device.h>
#include <circle/display.h>
#include <circle/string.h>
#include <circle/chargenerator.h>
#include <circle/bcmframebuffer.h>
#include <circle/spinlock.h>
#include <circle/types.h>

/**
 * @file TRenderer.h
 * @brief Declares the VT100 renderer combining device and task interfaces.
 * @details CTRenderer wraps the Circle framebuffer, character generator, and
 * ANSI state machine to present a faithful VT100 terminal surface. It exposes
 * high-level APIs for cursor control, color theming, and attribute handling so
 * the kernel and parser can render text without touching raw framebuffer
 * mechanics. The header also defines convenience palette constants used across
 * the application.
 */

// Forward declarations and includes for classes used in this module
#include "TColorPalette.h"
#include "TFontConverter.h"

/**
 * @class CTRenderer
 * @brief Combines Circle framebuffer access with a VT100-aware state machine.
 * @details CTRenderer implements the full terminal drawing pipeline including
 * escape sequence handling, scrolling regions, cursor updates, and attribute
 * effects. By inheriting from CDevice it can receive bytes directly from the
 * ANSI parser, while inheriting from CTask permits cooperative refresh and
 * cursor blinking without busy waiting.
 */
class CTRenderer : public CDevice, public CTask
{
public:
    // Define realistic vintage terminal colors
    // static constexpr TRendererColor kColorBlack = DISPLAY_COLOR(12, 12, 12);
    static constexpr TRendererColor kColorBlack = DISPLAY_COLOR(0, 0, 0);
    static constexpr TRendererColor kColorWhite = DISPLAY_COLOR(235, 235, 235);
    static constexpr TRendererColor kColorAmber = DISPLAY_COLOR(255, 176, 0);
    static constexpr TRendererColor kColorGreen = DISPLAY_COLOR(51, 255, 51);

    struct TRendererState
    {
        const TFont *font;
        CCharGenerator::TFontFlags fontFlags;
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        CDisplay::TRawColor defaultForeground;
        CDisplay::TRawColor defaultBackground;
        unsigned cursorX;
        unsigned cursorY;
        boolean cursorOn;
        boolean cursorBlock;
        boolean cursorVisible;
        boolean blinking;
        unsigned blinkTicks;
        unsigned nextBlink;
        unsigned scrollStart;
        unsigned scrollEnd;
        boolean reverseAttribute;
        boolean boldAttribute;
        boolean underlineAttribute;
        boolean blinkAttribute;
        boolean insertOn;
        boolean autoPage;
        boolean delayedUpdate;
        unsigned lastUpdateTicks;
        unsigned parserState;
        unsigned param1;
        unsigned param2;
        unsigned g0CharSet;
        unsigned g1CharSet;
        boolean useG1;
    };

    /// \brief Access the singleton renderer instance.
    /// \return Pointer to the renderer singleton.
    static CTRenderer *Get(void);

    /// Creates a CTRenderer instance with default font selection VT100Font10x20
    /// and no font flags and white on black colors.
    CTRenderer(void);

    /// \brief Release renderer resources and buffers.
    ~CTRenderer(void);

    /// \brief Initialize framebuffer access and Circle device registration.
    /// \return TRUE on success, FALSE otherwise.
    boolean Initialize(void);

    /// \brief Set the font to be used.
    /// \param rFont Font to be used for text rendering.
    /// \param FontFlags Optional generator font flags.
    /// \return TRUE if the font was applied successfully.
    boolean SetFont(const TFont &rFont,
                    CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);

    /// \brief Set the font by selection identifier.
    /// \param selection Font selection identifier.
    /// \param FontFlags Optional generator font flags.
    /// \return TRUE if the font was applied successfully.
    boolean SetFont(EFontSelection selection,
                    CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);

    /// \brief Translate a configured color selection into the renderer palette.
    /// \param color Logical color selection enum value.
    /// \return Render-specific color value.
    TRendererColor MapColor(EColorSelection color);

    

    /// \brief Query the screen width in pixels.
    /// \return Width in pixels.
    unsigned GetWidth(void) const;

    /// \brief Query the screen height in pixels.
    /// \return Height in pixels.
    unsigned GetHeight(void) const;

    /// \brief Query the screen width in characters.
    /// \return Width in text columns.
    unsigned GetColumns(void) const;
    /// \brief Query the screen height in characters.
    /// \return Height in text rows.
    unsigned GetRows(void) const;

    /// \brief Query the current cursor column (0-based).
    unsigned GetCursorColumn(void) const;
    /// \brief Query the current cursor row (0-based).
    unsigned GetCursorRow(void) const;

    /// \brief Access the underlying framebuffer device.
    /// \return Pointer to the framebuffer.
    CBcmFrameBuffer *GetDisplay(void);

    /// \brief Clear entire display area and home the cursor.
    void ClearDisplay(void);

    /// \brief Write characters to screen.
    /// \note Supports several escape sequences (see: doc/screen.txt).
    /// \param pBuffer Pointer to the characters to be written.
    /// \param nCount Number of characters to be written.
    /// \return Number of written characters.
    int Write(const void *pBuffer, size_t nCount) override;

    /// \brief Reset ANSI parser state (used by VT tests).
    void ResetParserState(void);

    /// \brief Move the cursor to a specific position.
    /// \param nRow Row number (based on 0).
    /// \param nColumn Column number (based on 0).
    void Goto(unsigned nRow, unsigned nColumn);

    /// \brief Set a pixel to a specific logical color.
    /// \param nPosX X-Position of the pixel (based on 0).
    /// \param nPosY Y-Position of the pixel (based on 0).
    /// \param Color The logical color to be set.
    void SetPixel(unsigned nPosX, unsigned nPosY, TRendererColor Color);

    /// \brief Set a pixel to a specific raw color.
    /// \param nPosX X-Position of the pixel (based on 0).
    /// \param nPosY Y-Position of the pixel (based on 0).
    /// \param nColor The raw color to be set.
    void SetPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);

    /// \brief Get the color value of a pixel.
    /// \param nPosX X-Position of the pixel (based on 0).
    /// \param nPosY Y-Position of the pixel (based on 0).
    /// \return The requested color value (CDisplay::Black if not matches).
    TRendererColor GetPixel(unsigned nPosX, unsigned nPosY);

    /// \brief Set the text colors using logical selections.
    /// \param Foreground Foreground color selection.
    /// \param Background Background color selection.
    /// \return TRUE if the colors were applied.
    boolean SetColors(EColorSelection Foreground, EColorSelection Background);

    /// \brief Set the text colors using explicit renderer colors.
    /// \param Foreground Foreground color value.
    /// \param Background Background color value.
    void SetColors(TRendererColor Foreground = kColorWhite, TRendererColor Background = kColorBlack);

    /// \brief Enable a block cursor instead of the default underline.
    /// \param bCursorBlock TRUE to enable block cursor mode.
    void SetCursorBlock(boolean bCursorBlock);

    /// \brief Enable or disable cursor blinking.
    /// \param bBlinkingCursor TRUE to enable blinking.
    /// \param nPeriodMilliSeconds Blink period in milliseconds.
    void SetBlinkingCursor(boolean bBlinkingCursor, unsigned nPeriodMilliSeconds = 500);

    /// \brief Periodic display maintenance invoked from the task loop.
    void Update(void);
    
    /// \brief Enable or disable cursor visibility.
    /// \param bVisible TRUE to show the cursor.
    void SetCursorMode(boolean bVisible);

    /// \brief Enable or disable VT52 emulation mode.
    /// \param bEnable TRUE to enable VT52 mode, FALSE for ANSI mode.
    void SetVT52Mode(boolean bEnable);

    /// \brief Enable or disable automatic page mode (cursor wrap to top).
    /// \param bEnable TRUE to enable auto page mode, FALSE for normal scrolling.
    void SetAutoPageMode(boolean bEnable);

    /// \brief Enable or disable smooth-scroll animation.
    /// \param bEnable TRUE to enable smooth-scroll animation.
    void SetSmoothScrollEnabled(boolean bEnable);

    /// \brief Query whether smooth-scroll animation is enabled.
    /// \return TRUE when smooth-scroll animation is enabled.
    boolean GetSmoothScrollEnabled(void) const { return m_bSmoothScrollEnabled; }

    /// \brief Force-hide the cursor and restore underlying pixels.
    void ForceHideCursor(void);


    /// \brief Entry point of the rendering task.
    void Run(void) override;

    /// \brief Set a pixel to a specific raw color.
    /// \param nPosX X-Position of the pixel (based on 0).
    /// \param nPosY Y-Position of the pixel (based on 0).
    /// \param nColor The raw color to be set.
    /// \note This method allows the direct access to the internal buffer.
    void SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);

    /// \brief Get the raw color value of a pixel.
    /// \param nPosX X-Position of the pixel (based on 0).
    /// \param nPosY Y-Position of the pixel (based on 0).
    /// \return The requested raw color value.
    /// \note This method allows the direct access to the internal buffer.
    CDisplay::TRawColor GetRawPixel(unsigned nPosX, unsigned nPosY);

    /// \brief Adjust brightness of a logical color.
    /// \param color The logical color to be adjusted.
    /// \param factor Brightness factor (1.0 = no change, < 1.0 = darker, > 1.0 = brighter).
    /// \return The adjusted logical color.
    CDisplay::TColor AdjustBrightness(CDisplay::TColor color, float factor);

    /// \brief Adjust brightness of a raw RGB565 color.
    /// \param color The raw RGB565 color to be adjusted.
    /// \param factor Brightness factor (1.0 = no change, < 1.0 = darker, > 1.0 = brighter).
    /// \return The adjusted raw RGB565 color.
    CDisplay::TRawColor AdjustBrightness565(CDisplay::TRawColor color, float factor);

    /// \brief Set scaling factors for bold and reverse video attributes.
    /// \param boldFactor Scaling factor for bold attribute.
    /// \param reverseBackgroundFactor Scaling factor for reverse video background.
    /// \param reverseForegroundFactor Scaling factor for reverse video foreground.
    void SetBrightnessScaling(float boldFactor = 1.6f,
        float reverseBackgroundFactor = 0.7f,
        float reverseForegroundFactor = 1.25f);

    /// \brief Conduct a rendering self-test using various attributes.
    void doRenderTest(void);

    /// \brief Save the current renderer state for later restore.
    void SaveState(TRendererState &state);

    /// \brief Restore a previously saved renderer state.
    void RestoreState(const TRendererState &state);

    /// \brief Query the size of the internal pixel buffer.
    size_t GetBufferSize(void) const;

    /// \brief Save the internal pixel buffer into a caller-provided buffer.
    void SaveScreenBuffer(void *buffer, size_t bufferSize);

    /// \brief Restore the internal pixel buffer from a caller-provided buffer.
    void RestoreScreenBuffer(const void *buffer, size_t bufferSize);


private:
    /// \brief Write a single character respecting current state machine.
    void Write(char chChar);

    /// \brief Move cursor to column zero without changing row.
    void CarriageReturn(void);
    /// \brief Clear the display from cursor to end of screen.
    void ClearDisplayEnd(void);
    /// \brief Clear the active line from cursor to end of line.
    void ClearLineEnd(void);
    /// \brief Move cursor down handling scrolling.
    void CursorDown(void);
    /// \brief Return cursor to home position.
    void CursorHome(void);
    /// \brief Move cursor left by one column.
    void CursorLeft(void);
    /// \brief Move cursor to specific row and column.
    void CursorMove(unsigned nRow, unsigned nColumn);
    /// \brief Move cursor right by one column.
    void CursorRight(void);
    /// \brief Move cursor up by one row.
    void CursorUp(void);
    /// \brief Delete characters starting at cursor position.
    void DeleteChars(unsigned nCount);
    /// \brief Delete lines beginning at the cursor row.
    void DeleteLines(unsigned nCount);
    /// \brief Render character at current cursor position.
    void DisplayChar(char chChar);
    /// \brief Erase characters and shift remainder of line.
    void EraseChars(unsigned nCount);
    /// \brief Obtain current background color.
    CDisplay::TRawColor GetTextBackgroundColor(void);
    /// \brief Obtain current foreground color.
    CDisplay::TRawColor GetTextColor(void);
    /// \brief Insert new blank lines starting at cursor row.
    void InsertLines(unsigned nCount);
    /// \brief Toggle insert mode state.
    void InsertMode(boolean bBegin);
    /// \brief Advance to next line applying scroll if necessary.
    void NewLine(void);
    /// \brief Scroll content downward for reverse index.
    void ReverseScroll(void);

    /// \brief Define the active scrolling region.
    void SetScrollRegion(unsigned nStartRow, unsigned nEndRow);
    /// \brief Apply standout (attribute) mode state.
    void SetStandoutMode(unsigned nMode);
    /// \brief Advance to the next tab stop.
    void Tabulator(void);
    /// \brief Move to the previous tab stop.
    void BackTabulator(void);

    /// \brief Save the current cursor position and attributes.
    void SaveCursor(void);

    /// \brief Restore the saved cursor position and attributes.
    void RestoreCursor(void);

    /// \brief Scroll display buffer content upward one line.
    void Scroll(void);
    /// \brief Schedule smooth scroll animation for a region.
    boolean BeginSmoothScrollAnimation(unsigned nStartY, unsigned nEndY, boolean bScrollDown);
    /// \brief Render one smooth scroll animation frame.
    void RenderSmoothScrollFrame(void);

    /// \brief Render a character at an explicit position with specified color.
    void DisplayChar(char chChar, unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);
    /// \brief Clear a character cell at the given position.
    void EraseChar(unsigned nPosX, unsigned nPosY);
    /// \brief Invert current cursor pixels to show cursor state.
    void InvertCursor(void);


    // We always update entire pixel lines.
    /// \brief Expand the pending update area to include the provided rows.
    void SetUpdateArea(unsigned nPosY1, unsigned nPosY2)
    {
        if (nPosY1 < m_UpdateArea.y1)
        {
            m_UpdateArea.y1 = nPosY1;
        }

        if (nPosY2 > m_UpdateArea.y2)
        {
            m_UpdateArea.y2 = nPosY2;
        }
    }

    enum TState
    {
        StateStart,
        StateEscape,
        StateVT52Row,
        StateVT52Col,
        StateBracket,
        StateNumber1,
        StateQuestionMark,
        StateSemicolon,
        StateNumber2,
        StateNumber3,
        StateAutoPage,
        StateFontChange,
        StateSkipTillCRLF,
        StateG0,
        StateG1
    };

    enum ECharacterSet
    {
        CharSetUS,
        CharSetGraphics
    };

    const TFont *m_pFont;
    CCharGenerator::TFontFlags m_FontFlags;
    CCharGenerator *m_pCharGen;
    CCharGenerator *m_pGraphicsCharGen;
    EFontSelection m_CurrentFontSelection;

    ECharacterSet m_G0CharSet;
    ECharacterSet m_G1CharSet;
    boolean m_bUseG1;

    CDisplay::TRawColor *m_pCursorPixels;
    union
    {
        u8 *m_pBuffer8;
        u16 *m_pBuffer16;
        u32 *m_pBuffer32;
    };
    CBcmFrameBuffer *m_pFrameBuffer;
    unsigned m_nDisplayIndex;
    unsigned m_nSize;
    unsigned m_nPitch;
    unsigned m_nWidth;
    unsigned m_nHeight;
    unsigned m_nUsedWidth;
    unsigned m_nUsedHeight;
    unsigned m_nDepth;
    CDisplay::TArea m_UpdateArea;
    TState m_State;
    unsigned m_nScrollStart;
    unsigned m_nScrollEnd;
    unsigned m_nCursorX;
    unsigned m_nCursorY;
    boolean m_bCursorOn;
    boolean m_bCursorBlock;
    boolean m_bBlinkingCursor;
    boolean m_bCursorVisible;
    unsigned m_nCursorBlinkPeriodTicks;
    unsigned m_nNextCursorBlink;
    CDisplay::TRawColor m_ForegroundColor;
    CDisplay::TRawColor m_BackgroundColor;
    CDisplay::TRawColor m_DefaultForegroundColor;
    CDisplay::TRawColor m_DefaultBackgroundColor;
    float m_BoldScaleFactor;
    float m_DimScaleFactor;
    float m_ReverseBackgroundScaleFactor;
    float m_ReverseForegroundScaleFactor;
    boolean m_bReverseAttribute;
    boolean m_bBoldAttribute;
    boolean m_bDimAttribute;
    boolean m_bUnderlineAttribute;
    boolean m_bBlinkAttribute;
    boolean m_bInsertOn;
    boolean m_bVT52Mode;
    unsigned m_nParam1;
    unsigned m_nParam2;
    boolean m_bAutoPage;
    boolean m_bDelayedUpdate;
    unsigned m_nLastUpdateTicks;
    boolean m_bSmoothScrollEnabled;
    boolean m_bSmoothScrollActive;
    boolean m_bSmoothScrollDown;
    unsigned m_nSmoothScrollStartY;
    unsigned m_nSmoothScrollEndY;
    unsigned m_nSmoothScrollOffset;
    unsigned m_nSmoothScrollStep;
    unsigned m_nSmoothScrollLastTick;
    unsigned m_nSmoothScrollTickInterval;
    u8 *m_pSmoothScrollSnapshot;
    u8 *m_pSmoothScrollCompose;
    size_t m_nSmoothScrollBufferSize;
    unsigned m_nSmoothScrollStartTick;
    unsigned  m_nSmoothScrollDebounceUntil; // tick until which we suppress smooth to avoid bursts
    unsigned m_nScrollStatsLastLogTick;
    unsigned long long m_ScrollNormalTicksAccum;
    unsigned long long m_ScrollSmoothTicksAccum;
    unsigned m_ScrollNormalCount;
    unsigned m_ScrollSmoothCount;
    TRendererState m_SavedState;
    /**
     * @brief Spinlock to protect the renderer state.
     * @details
     * Initialized with TASK_LEVEL to ensure interrupts remain enabled while held.
     * This prevents "Head-of-Line Blocking" where holding the lock during heavy
     * operations (like scrolling) would otherwise disable IRQs and cause UART
     * FIFO overflows.
     */
    mutable CSpinLock m_SpinLock;
};
