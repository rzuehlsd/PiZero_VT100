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

// Include class header
#include "TRenderer.h"

// Full class definitions for classes used in this module
// Include Circle core components
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/devicenameservice.h>
#include <circle/bcmframebuffer.h>
#include <circle/synchronize.h>
#include <circle/sysconfig.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <string.h>

// Include application components
#include "TFontConverter.h"
#include "TConfig.h"
#include "hal.h"

LOGMODULE("TRenderer");

#define DEPTH 16

// default screen device name prefix
static const char DevicePrefix[] = "tty";

// Singleton instance creation and access
// teardown handled by runtime
// CAUTION: Only possible if constructor does not need parameters
static CTRenderer *s_pThis = 0;
CTRenderer *CTRenderer::Get(void)
{
    if (s_pThis == 0)
    {
        s_pThis = new CTRenderer();
    }
    return s_pThis;
}

CTRenderer::CTRenderer(void)
    : m_pFont(nullptr),
      m_FontFlags(CCharGenerator::FontFlagsNone),
      m_pCharGen(nullptr),
      m_pGraphicsCharGen(nullptr),
      m_CurrentFontSelection(EFontSelection::VT100Font10x20),
      m_G0CharSet(CharSetUS),
      m_G1CharSet(CharSetGraphics),
      m_bUseG1(FALSE),
      m_pCursorPixels(nullptr),
      m_pBuffer8(nullptr),
      m_pFrameBuffer(nullptr),
      m_nDisplayIndex(0),
      m_nSize(0),
      m_nPitch(0),
      m_nWidth(0),
      m_nHeight(0),
      m_nUsedWidth(0),
      m_nUsedHeight(0),
      m_nDepth(0),
      m_State(StateStart),
      m_nScrollStart(0),
      m_nScrollEnd(0),
      m_nCursorX(0),
      m_nCursorY(0),
      m_bCursorOn(TRUE),
      m_bCursorBlock(FALSE),
      m_bBlinkingCursor(TRUE),
      m_bCursorVisible(FALSE),
      m_nCursorBlinkPeriodTicks(MSEC2HZ(500)),
      m_nNextCursorBlink(0),
      m_ForegroundColor(0),
      m_BackgroundColor(0),
      m_DefaultForegroundColor(0),
      m_DefaultBackgroundColor(0),
      m_BoldScaleFactor(1.6f),
      m_DimScaleFactor(0.6f),
      m_ReverseBackgroundScaleFactor(0.6f),
      m_ReverseForegroundScaleFactor(1.6f),
      m_bReverseAttribute(FALSE),
      m_bBoldAttribute(FALSE),
    m_bDimAttribute(FALSE),
      m_bUnderlineAttribute(FALSE),
      m_bBlinkAttribute(FALSE),
      m_bInsertOn(FALSE),
    m_bVT52Mode(FALSE),
      m_bAutoPage(FALSE),
      m_bDelayedUpdate(FALSE),
    m_bSmoothScrollEnabled(TRUE),
    m_bSmoothScrollActive(FALSE),
    m_bSmoothScrollDown(FALSE),
    m_nSmoothScrollStartY(0),
    m_nSmoothScrollEndY(0),
    m_nSmoothScrollOffset(0),
    m_nSmoothScrollStep(0),
    m_nSmoothScrollLastTick(0),
    m_nSmoothScrollTickInterval(MSEC2HZ(8)),
    m_pSmoothScrollSnapshot(nullptr),
    m_pSmoothScrollCompose(nullptr),
        m_nSmoothScrollBufferSize(0),
        m_nSmoothScrollStartTick(0),
                m_nSmoothScrollDebounceUntil(0),
        m_nScrollStatsLastLogTick(0),
        m_ScrollNormalTicksAccum(0),
        m_ScrollSmoothTicksAccum(0),
        m_ScrollNormalCount(0),
        m_ScrollSmoothCount(0),
      // Initialize spinlock with TASK_LEVEL so acquiring it does NOT disable interrupts.
      // This is crucial to prevent UART FIFO overflows during heavy render ops.
      m_SpinLock(TASK_LEVEL)
{
    // Initialize saved state with safe defaults
    memset(&m_SavedState, 0, sizeof(m_SavedState));

    SetName("Renderer");
    Suspend();
}

CTRenderer::~CTRenderer(void)
{
    CDeviceNameService::Get()->RemoveDevice(DevicePrefix, m_nDisplayIndex + 1, FALSE);

    delete[] m_pBuffer8;
    m_pBuffer8 = nullptr;

    delete[] m_pCursorPixels;
    m_pCursorPixels = nullptr;

    delete[] m_pSmoothScrollSnapshot;
    m_pSmoothScrollSnapshot = nullptr;

    delete[] m_pSmoothScrollCompose;
    m_pSmoothScrollCompose = nullptr;

    delete m_pCharGen;
    m_pCharGen = nullptr;

    delete m_pGraphicsCharGen;
    m_pGraphicsCharGen = nullptr;

    delete m_pFrameBuffer;
    m_pFrameBuffer = nullptr;
}

boolean CTRenderer::Initialize(void)
{
    m_pFrameBuffer = new CBcmFrameBuffer(0, 0, DEPTH, 0, 0, m_nDisplayIndex);
    if (!m_pFrameBuffer)
    {
        return FALSE;
    }

    if (!m_pFrameBuffer->Initialize())
    {
        return FALSE;
    }

    m_nWidth = m_pFrameBuffer->GetWidth();
    m_nHeight = m_pFrameBuffer->GetHeight();
    m_nDepth = m_pFrameBuffer->GetDepth();
    m_nSize = m_nWidth * m_nHeight * m_nDepth / 8;
    m_nPitch = m_nWidth * m_nDepth / 8;

    if (m_nDepth == 1 && m_nWidth % 8 != 0)
    {
        return FALSE;
    }

    m_pBuffer8 = new u8[m_nSize];
    if (!m_pBuffer8)
    {
        return FALSE;
    }

    m_nSmoothScrollBufferSize = m_nSize;
    m_pSmoothScrollSnapshot = new u8[m_nSmoothScrollBufferSize];
    if (!m_pSmoothScrollSnapshot)
    {
        return FALSE;
    }

    m_pSmoothScrollCompose = new u8[m_nSmoothScrollBufferSize];
    if (!m_pSmoothScrollCompose)
    {
        return FALSE;
    }

    if (!SetFont(EFontSelection::VT100Font10x20, m_FontFlags))
    {
        return FALSE;
    }

    m_ForegroundColor = m_pFrameBuffer->GetColor(CDisplay::NormalColor);
    m_BackgroundColor = m_pFrameBuffer->GetColor(CDisplay::Black);
    m_DefaultForegroundColor = m_ForegroundColor;
    m_DefaultBackgroundColor = m_BackgroundColor;
    m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;

    CursorHome();
    ClearDisplayEnd();
    InvertCursor();

    // Initial update
    m_UpdateArea.x1 = 0;
    m_UpdateArea.x2 = m_nWidth - 1;
    m_UpdateArea.y1 = 0;
    m_UpdateArea.y2 = m_nHeight - 1;
    m_pFrameBuffer->SetArea(m_UpdateArea, m_pBuffer8);

    m_UpdateArea.y1 = m_nHeight;
    m_UpdateArea.y2 = 0;

    if (!CDeviceNameService::Get()->GetDevice(DevicePrefix, m_nDisplayIndex + 1, FALSE))
    {
        CDeviceNameService::Get()->AddDevice(DevicePrefix, m_nDisplayIndex + 1, this, FALSE);
    }

    LOGNOTE("Renderer initialized");

    m_nScrollStatsLastLogTick = CTimer::Get()->GetTicks();

    // Set initial font and colors from config (if available)
    CTConfig *config = CTConfig::Get();
    if (config != nullptr)
    {
        SetFont(config->GetFontSelection(), CCharGenerator::FontFlagsNone);
        TRendererColor fg = MapColor(config->GetTextColor());
        TRendererColor bg = MapColor(config->GetBackgroundColor());
        SetColors(fg, bg);
        SetCursorBlock(config->GetCursorBlock());
        SetBlinkingCursor(config->GetCursorBlinking(), 500);
    }

    Start();
    return TRUE;
}

bool CTRenderer::SetFont(EFontSelection selection, CCharGenerator::TFontFlags FontFlags)
{
    m_CurrentFontSelection = selection;
    const TFont &font = CTFontConverter::Get()->GetFont(selection);
    return SetFont(font, FontFlags);
}

bool CTRenderer::SetFont(const TFont &rFont, CCharGenerator::TFontFlags FontFlags)
{
    m_SpinLock.Acquire();

    const bool cursorWasVisible = m_bCursorVisible;
    const bool blinkingWasEnabled = m_bBlinkingCursor;

    unsigned cursorColumn = 0;
    unsigned cursorRow = 0;
    if (m_pCharGen)
    {
        const unsigned oldCharWidth = m_pCharGen->GetCharWidth();
        const unsigned oldCharHeight = m_pCharGen->GetCharHeight();
        if (oldCharWidth)
        {
            cursorColumn = m_nCursorX / oldCharWidth;
        }
        if (oldCharHeight)
        {
            cursorRow = m_nCursorY / oldCharHeight;
        }
    }

    m_bBlinkingCursor = FALSE;

    if (cursorWasVisible)
    {
        InvertCursor();
    }

    delete m_pCharGen;
    m_pCharGen = nullptr;

    m_pCharGen = new CCharGenerator(rFont, FontFlags);
    if (!m_pCharGen)
    {
        if (cursorWasVisible)
        {
            m_bCursorVisible = false;
        }
        m_bBlinkingCursor = blinkingWasEnabled;
        m_SpinLock.Release();
        return FALSE;
    }

    delete m_pGraphicsCharGen;
    m_pGraphicsCharGen = nullptr;

    EFontSelection gfxSelection = EFontSelection::VT100GraphicsFont10x20;
    switch (m_CurrentFontSelection)
    {
    case EFontSelection::VT100Font8x20:
        gfxSelection = EFontSelection::VT100GraphicsFont8x20;
        break;
    case EFontSelection::VT100Font10x20:
        gfxSelection = EFontSelection::VT100GraphicsFont10x20;
        break;
    case EFontSelection::VT100Font10x20Solid:
        gfxSelection = EFontSelection::VT100GraphicsFont10x20Solid;
        break;
    default:
        // Default to 10x20 graphics if unknown
        gfxSelection = EFontSelection::VT100GraphicsFont10x20;
        break;
    }

    const TFont &gfxFont = CTFontConverter::Get()->GetFont(gfxSelection);
    m_pGraphicsCharGen = new CCharGenerator(gfxFont, FontFlags);

    delete[] m_pCursorPixels;
    m_pCursorPixels = nullptr;

    const unsigned cursorPixelCount = m_pCharGen->GetCharWidth() * m_pCharGen->GetCharHeight();
    m_pCursorPixels = new CDisplay::TRawColor[cursorPixelCount];
    if (!m_pCursorPixels)
    {
        delete m_pCharGen;
        m_pCharGen = nullptr;
        if (cursorWasVisible)
        {
            m_bCursorVisible = false;
        }
        m_bBlinkingCursor = blinkingWasEnabled;
        m_SpinLock.Release();
        return FALSE;
    }

    for (unsigned i = 0; i < cursorPixelCount; ++i)
    {
        m_pCursorPixels[i] = 0;
    }

    m_pFont = &rFont;
    m_FontFlags = FontFlags;

    m_nUsedWidth = m_nWidth / m_pCharGen->GetCharWidth() * m_pCharGen->GetCharWidth();
    m_nUsedHeight = m_nHeight / m_pCharGen->GetCharHeight() * m_pCharGen->GetCharHeight();
    m_nScrollEnd = m_nUsedHeight;

    const unsigned newColumns = GetColumns();
    const unsigned newRows = GetRows();
    if (newColumns > 0)
    {
        if (cursorColumn >= newColumns)
        {
            cursorColumn = newColumns - 1;
        }
        m_nCursorX = cursorColumn * m_pCharGen->GetCharWidth();
    }
    else
    {
        m_nCursorX = 0;
    }

    if (newRows > 0)
    {
        if (cursorRow >= newRows)
        {
            cursorRow = newRows - 1;
        }
        m_nCursorY = cursorRow * m_pCharGen->GetCharHeight();
    }
    else
    {
        m_nCursorY = 0;
    }

    m_bCursorVisible = false;

    if (cursorWasVisible && m_bCursorOn)
    {
        InvertCursor();
    }

    m_bBlinkingCursor = blinkingWasEnabled;
    if (m_bBlinkingCursor)
    {
        m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;
    }

    m_SpinLock.Release();

    return true;
}

TRendererColor CTRenderer::MapColor(EColorSelection color)
{
    switch (color)
    {
    case TerminalColorBlack:
        return CTRenderer::kColorBlack;
    case TerminalColorWhite:
        return CTRenderer::kColorWhite;
    case TerminalColorAmber:
        return CTRenderer::kColorAmber;
    case TerminalColorGreen:
        return CTRenderer::kColorGreen;
    default:
        return CTRenderer::kColorWhite;
    }
}

boolean CTRenderer::SetColors(EColorSelection Foreground, EColorSelection Background)
{
    if (m_pFrameBuffer == nullptr)
    {
        return false;
    }

    EColorSelection fgSelection = Foreground;
    EColorSelection bgSelection = Background;

    CTConfig *config = CTConfig::Get();
    if (config != nullptr && config->GetScreenInverted())
    {
        fgSelection = Background;
        bgSelection = Foreground;
    }

    m_SpinLock.Acquire();
    const TRendererColor fgLogical = MapColor(fgSelection);
    const TRendererColor bgLogical = MapColor(bgSelection);
    const CDisplay::TRawColor fgColor = m_pFrameBuffer->GetColor(fgLogical);
    const CDisplay::TRawColor bgColor = m_pFrameBuffer->GetColor(bgLogical);
    m_DefaultForegroundColor = fgColor;
    m_DefaultBackgroundColor = bgColor;
    m_ForegroundColor = fgColor;
    m_BackgroundColor = bgColor;
    m_SpinLock.Release();
    return true;
}

void CTRenderer::Goto(unsigned nRow, unsigned nColumn)
{
    m_SpinLock.Acquire();

    const bool cursorWasVisible = m_bCursorVisible;
    if (cursorWasVisible)
    {
        InvertCursor();
    }

    if (nColumn < GetColumns())
    {
        m_nCursorX = nColumn * m_pCharGen->GetCharWidth();
    }
    else
    {
        m_nCursorX = (GetColumns() - 1) * m_pCharGen->GetCharWidth();
    }

    if (nRow < GetRows())
    {
        m_nCursorY = nRow * m_pCharGen->GetCharHeight();
    }
    else
    {
        m_nCursorY = (GetRows() - 1) * m_pCharGen->GetCharHeight();
    }

    if (cursorWasVisible && m_bCursorOn)
    {
        InvertCursor();
        if (m_bBlinkingCursor)
        {
            m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;
        }
    }

    m_SpinLock.Release();
}

void CTRenderer::Run()
{
    while (!IsSuspended())
    {
        m_SpinLock.Acquire();

        if (m_bCursorOn && m_bBlinkingCursor)
        {
            unsigned currentTicks = CTimer::Get()->GetTicks();
            if ((int)(currentTicks - m_nNextCursorBlink) >= 0)
            {
                InvertCursor();
                m_nNextCursorBlink = currentTicks + m_nCursorBlinkPeriodTicks;
            }
        }

        m_SpinLock.Release();

        Update();

        const unsigned now = CTimer::Get()->GetTicks();
        const unsigned logInterval = MSEC2HZ(30000);
        if ((int)(now - m_nScrollStatsLastLogTick) >= 0 && (now - m_nScrollStatsLastLogTick) >= logInterval)
        {
            const unsigned long long normalCount = m_ScrollNormalCount;
            const unsigned long long smoothCount = m_ScrollSmoothCount;
            const unsigned long long normalAvgMs = normalCount ? (m_ScrollNormalTicksAccum * 1000ULL / HZ) / normalCount : 0ULL;
            const unsigned long long smoothAvgMs = smoothCount ? (m_ScrollSmoothTicksAccum * 1000ULL / HZ) / smoothCount : 0ULL;

            LOGNOTE("Scroll stats: normal count=%llu avg=%llums, smooth count=%llu avg=%llums", normalCount, normalAvgMs, smoothCount, smoothAvgMs);

            m_ScrollNormalTicksAccum = 0;
            m_ScrollSmoothTicksAccum = 0;
            m_ScrollNormalCount = 0;
            m_ScrollSmoothCount = 0;
            m_nScrollStatsLastLogTick = now;
        }

        // CScheduler::Get()->MsSleep(1);
        CScheduler::Get()->Yield();
    }
}

unsigned CTRenderer::GetWidth(void) const
{
    return m_nWidth;
}

unsigned CTRenderer::GetHeight(void) const
{
    return m_nHeight;
}

unsigned CTRenderer::GetColumns(void) const
{
    if (m_pCharGen == nullptr)
    {
        return 0;
    }

    return m_nWidth / m_pCharGen->GetCharWidth();
}

unsigned CTRenderer::GetRows(void) const
{
    if (m_pCharGen == nullptr)
    {
        return 0;
    }

    return m_nHeight / m_pCharGen->GetCharHeight();
}

unsigned CTRenderer::GetCursorColumn(void) const
{
    if (m_pCharGen == nullptr)
    {
        return 0;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    if (charWidth == 0)
    {
        return 0;
    }

    m_SpinLock.Acquire();
    unsigned column = m_nCursorX / charWidth;
    m_SpinLock.Release();
    return column;
}

unsigned CTRenderer::GetCursorRow(void) const
{
    if (m_pCharGen == nullptr)
    {
        return 0;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return 0;
    }

    m_SpinLock.Acquire();
    unsigned row = m_nCursorY / charHeight;
    m_SpinLock.Release();
    return row;
}

CBcmFrameBuffer *CTRenderer::GetDisplay(void)
{
    return m_pFrameBuffer;
}

void CTRenderer::SetColors(TRendererColor Foreground, TRendererColor Background)
{
    if (m_pFrameBuffer == nullptr)
    {
        return;
    }

    m_SpinLock.Acquire();

    const CDisplay::TRawColor fgColor = m_pFrameBuffer->GetColor(Foreground);
    const CDisplay::TRawColor bgColor = m_pFrameBuffer->GetColor(Background);

    m_DefaultForegroundColor = fgColor;
    m_DefaultBackgroundColor = bgColor;
    m_ForegroundColor = fgColor;
    m_BackgroundColor = bgColor;

    m_SpinLock.Release();
}

int CTRenderer::Write(const void *pBuffer, size_t nCount)
{
#ifdef REALTIME
    // cannot write from IRQ_LEVEL to prevent deadlock, just ignore it
    if (CurrentExecutionLevel() > TASK_LEVEL)
    {
        return nCount;
    }
#endif

    m_SpinLock.Acquire();

    const bool cursorWasVisible = m_bCursorVisible;
    if (cursorWasVisible)
    {
        InvertCursor();
    }

    const char *pChar = (const char *)pBuffer;
    int nResult = 0;

    while (nCount--)
    {
        Write(*pChar++);

        nResult++;
    }

    if (cursorWasVisible && m_bCursorOn)
    {
        InvertCursor();
        if (m_bBlinkingCursor)
        {
            m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;
        }
    }

    // Update display
    if (!m_bDelayedUpdate && !m_bSmoothScrollActive && m_UpdateArea.y1 <= m_UpdateArea.y2)
    {
        m_pFrameBuffer->SetArea(m_UpdateArea, m_pBuffer8 + m_UpdateArea.y1 * m_nPitch);

        m_UpdateArea.y1 = m_nHeight;
        m_UpdateArea.y2 = 0;
    }

    m_SpinLock.Release();

    return nResult;
}

void CTRenderer::ResetParserState(void)
{
    m_SpinLock.Acquire();
    m_State = StateStart;
    m_nParam1 = 0;
    m_nParam2 = 0;
    m_SpinLock.Release();
}

inline void CTRenderer::SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    switch (m_nDepth)
    {
    case 1:
    {
        u8 *pBuffer = &m_pBuffer8[(m_nWidth * nPosY + nPosX) / 8];
        u8 uchMask = 0x80 >> (nPosX & 7);
        if (nColor)
        {
            *pBuffer |= uchMask;
        }
        else
        {
            *pBuffer &= ~uchMask;
        }
    }
    break;

    case 8:
        m_pBuffer8[m_nWidth * nPosY + nPosX] = (u8)nColor;
        break;
    case 16:
        m_pBuffer16[m_nWidth * nPosY + nPosX] = (u16)nColor;
        break;
    case 32:
        m_pBuffer32[m_nWidth * nPosY + nPosX] = nColor;
        break;
    }
}

inline CDisplay::TRawColor CTRenderer::GetRawPixel(unsigned nPosX, unsigned nPosY)
{
    switch (m_nDepth)
    {
    case 1:
    {
        u8 *pBuffer = &m_pBuffer8[(m_nWidth * nPosY + nPosX) / 8];
        u8 uchMask = 0x80 >> (nPosX & 7);
        return !!(*pBuffer & uchMask);
    }
    break;

    case 8:
        return m_pBuffer8[m_nWidth * nPosY + nPosX];
    case 16:
        return m_pBuffer16[m_nWidth * nPosY + nPosX];
    case 32:
        return m_pBuffer32[m_nWidth * nPosY + nPosX];
    }

    return 0;
}

CDisplay::TColor CTRenderer::AdjustBrightness(CDisplay::TColor color, float factor)
{
    auto clamp = [](int v)
    { return static_cast<u32>(v < 0 ? 0 : (v > 255 ? 255 : v)); };

    u32 r = (color >> 16) & 0xFF;
    u32 g = (color >> 8) & 0xFF;
    u32 b = color & 0xFF;

    r = clamp(static_cast<int>(r * factor));
    g = clamp(static_cast<int>(g * factor));
    b = clamp(static_cast<int>(b * factor));

    return DISPLAY_COLOR(r, g, b);
}

CDisplay::TRawColor CTRenderer::AdjustBrightness565(CDisplay::TRawColor color, float factor)
{
    auto clampComponent = [](float value, u32 maxValue) -> u32
    {
        if (value < 0.0f)
        {
            return 0;
        }

        if (value > static_cast<float>(maxValue))
        {
            return maxValue;
        }

        return static_cast<u32>(value + 0.5f);
    };

    const float r = static_cast<float>((color >> 11) & 0x1F);
    const float g = static_cast<float>((color >> 5) & 0x3F);
    const float b = static_cast<float>(color & 0x1F);

    float scaledFactor = factor;
    if (scaledFactor < 0.0f)
    {
        scaledFactor = 0.0f;
    }

    if (scaledFactor > 2.0f)
    {
        scaledFactor = 2.0f;
    }

    float newR;
    float newG;
    float newB;

    if (scaledFactor <= 1.0f)
    {
        newR = r * scaledFactor;
        newG = g * scaledFactor;
        newB = b * scaledFactor;
    }
    else
    {
        // First scale the colour vector and clamp to the 5:6:5 limits to keep the hue dominant
        newR = r * scaledFactor;
        newG = g * scaledFactor;
        newB = b * scaledFactor;

        const float redClamp = (newR > 31.0f) ? (31.0f / newR) : 1.0f;
        const float greenClamp = (newG > 63.0f) ? (63.0f / newG) : 1.0f;
        const float blueClamp = (newB > 31.0f) ? (31.0f / newB) : 1.0f;

        float clampScale = redClamp;
        if (greenClamp < clampScale)
        {
            clampScale = greenClamp;
        }
        if (blueClamp < clampScale)
        {
            clampScale = blueClamp;
        }

        if (clampScale < 1.0f)
        {
            newR *= clampScale;
            newG *= clampScale;
            newB *= clampScale;
        }

        // Add a controlled bias towards a warmer highlight to boost perceived brightness
        float mix = (scaledFactor - 1.0f) * 0.45f;
        if (mix > 1.0f)
        {
            mix = 1.0f;
        }
        else if (mix < 0.0f)
        {
            mix = 0.0f;
        }
        if (mix > 0.0f)
        {
            const float redWeight = 0.30f;
            const float greenWeight = 0.60f;
            const float blueWeight = 0.10f;

            newR += (31.0f - newR) * mix * redWeight;
            newG += (63.0f - newG) * mix * greenWeight;
            newB += (31.0f - newB) * mix * blueWeight;
        }
    }

    const u32 resultR = clampComponent(newR, 0x1F);
    const u32 resultG = clampComponent(newG, 0x3F);
    const u32 resultB = clampComponent(newB, 0x1F);

    return static_cast<CDisplay::TRawColor>((resultR << 11) | (resultG << 5) | resultB);
}

void CTRenderer::SetPixel(unsigned nPosX, unsigned nPosY, TRendererColor Color)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }

    CDisplay::TRawColor nColor = m_pFrameBuffer->GetColor(Color);

    SetRawPixel(nPosX, nPosY, nColor);

    m_pFrameBuffer->SetPixel(nPosX, nPosY, nColor);
}

void CTRenderer::SetPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }

    SetRawPixel(nPosX, nPosY, nColor);

    m_pFrameBuffer->SetPixel(nPosX, nPosY, nColor);
}

TRendererColor CTRenderer::GetPixel(unsigned nPosX, unsigned nPosY)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return CDisplay::Black;
    }

    return m_pFrameBuffer->GetColor(GetRawPixel(nPosX, nPosY));
}

void CTRenderer::SetCursorBlock(boolean bCursorBlock)
{
    m_bCursorBlock = bCursorBlock;
}

void CTRenderer::SetBlinkingCursor(boolean bBlinkingCursor, unsigned nPeriodMilliSeconds)
{
    if (nPeriodMilliSeconds == 0)
    {
        nPeriodMilliSeconds = 1;
    }

    unsigned periodTicks = MSEC2HZ(nPeriodMilliSeconds);
    if (periodTicks == 0)
    {
        periodTicks = 1;
    }

    m_SpinLock.Acquire();

    m_bBlinkingCursor = bBlinkingCursor;
    m_nCursorBlinkPeriodTicks = periodTicks;
    m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;

    if (!m_bBlinkingCursor && m_bCursorOn && !m_bCursorVisible)
    {
        InvertCursor();
    }

    m_SpinLock.Release();
}

void CTRenderer::Update()
{
    m_SpinLock.Acquire();

    if (m_bSmoothScrollActive)
    {
        const unsigned now = CTimer::Get()->GetTicks();
        if ((int)(now - m_nSmoothScrollLastTick) >= 0)
        {
            RenderSmoothScrollFrame();

            if (m_nSmoothScrollOffset + m_nSmoothScrollStep < m_pCharGen->GetCharHeight())
            {
                m_nSmoothScrollOffset += m_nSmoothScrollStep;
                m_nSmoothScrollLastTick = now + m_nSmoothScrollTickInterval;
            }
            else
            {
                CDisplay::TArea area;
                area.x1 = 0;
                area.x2 = m_nWidth - 1;
                area.y1 = m_nSmoothScrollStartY;
                area.y2 = m_nSmoothScrollEndY;
                m_pFrameBuffer->SetArea(area, m_pBuffer8 + area.y1 * m_nPitch);
                if (m_nSmoothScrollStartTick != 0)
                {
                    m_ScrollSmoothTicksAccum += static_cast<unsigned>(now - m_nSmoothScrollStartTick);
                    ++m_ScrollSmoothCount;
                }
                m_bSmoothScrollActive = FALSE;
            }
        }
    }

    if (!m_bSmoothScrollActive && m_UpdateArea.y1 <= m_UpdateArea.y2)
    {
        m_pFrameBuffer->SetArea(m_UpdateArea, m_pBuffer8 + m_UpdateArea.y1 * m_nPitch);

        m_UpdateArea.y1 = m_nHeight;
        m_UpdateArea.y2 = 0;
    }

    m_SpinLock.Release();
}

boolean CTRenderer::BeginSmoothScrollAnimation(unsigned nStartY, unsigned nEndY, boolean bScrollDown)
{
    if (!m_bSmoothScrollEnabled || m_pCharGen == nullptr || !m_pSmoothScrollSnapshot || !m_pSmoothScrollCompose)
    {
        return FALSE;
    }

    const unsigned now = CTimer::Get()->GetTicks();

    // Debounce: if an animation is active or we recently animated, skip smooth and let caller fall back to instant
    if (m_bSmoothScrollActive || (int)(m_nSmoothScrollDebounceUntil - now) > 0)
    {
        return FALSE;
    }

    if (nStartY >= m_nHeight || nEndY >= m_nHeight || nStartY >= nEndY)
    {
        return FALSE;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight < 2)
    {
        return FALSE;
    }

    const unsigned regionHeight = nEndY - nStartY + 1;
    const size_t regionBytes = static_cast<size_t>(regionHeight) * m_nPitch;
    if (regionBytes > m_nSmoothScrollBufferSize)
    {
        return FALSE;
    }

    memcpy(m_pSmoothScrollSnapshot, m_pBuffer8 + nStartY * m_nPitch, regionBytes);
    m_nSmoothScrollStartY = nStartY;
    m_nSmoothScrollEndY = nEndY;
    m_bSmoothScrollDown = bScrollDown;
    // Target roughly 6 lines/sec like real VT100: ~170ms per line, evenly spaced frames.
    const unsigned targetLineMs = 170;
    unsigned frameMs = targetLineMs / charHeight;
    if (frameMs == 0)
    {
        frameMs = 1;
    }
    m_nSmoothScrollTickInterval = MSEC2HZ(frameMs);
    if (m_nSmoothScrollTickInterval == 0)
    {
        m_nSmoothScrollTickInterval = 1;
    }

    m_nSmoothScrollStep = 1;
    m_nSmoothScrollOffset = m_nSmoothScrollStep;
    m_nSmoothScrollLastTick = CTimer::Get()->GetTicks();
    m_nSmoothScrollStartTick = m_nSmoothScrollLastTick;
    const unsigned debounceMs = 50;
    m_nSmoothScrollDebounceUntil = m_nSmoothScrollLastTick + MSEC2HZ(debounceMs);
    m_bSmoothScrollActive = TRUE;
    return TRUE;
}

void CTRenderer::RenderSmoothScrollFrame(void)
{
    if (!m_bSmoothScrollActive)
    {
        return;
    }

    const unsigned regionHeight = m_nSmoothScrollEndY - m_nSmoothScrollStartY + 1;
    const unsigned offset = m_nSmoothScrollOffset;

    for (unsigned y = 0; y < regionHeight; ++y)
    {
        bool fillBackground = FALSE;
        unsigned srcY = 0;

        if (m_bSmoothScrollDown)
        {
            if (y < offset)
            {
                fillBackground = TRUE;
            }
            else
            {
                srcY = y - offset;
            }
        }
        else
        {
            if (y + offset >= regionHeight)
            {
                fillBackground = TRUE;
            }
            else
            {
                srcY = y + offset;
            }
        }

        u8 *pDst = m_pSmoothScrollCompose + y * m_nPitch;
        if (!fillBackground)
        {
            const u8 *pSrc = m_pSmoothScrollSnapshot + srcY * m_nPitch;
            memcpy(pDst, pSrc, m_nPitch);
            continue;
        }

        // Show live buffer content (including newly drawn bottom lines) as soon as it scrolls into view
        const u8 *pLive = m_pBuffer8 + (m_nSmoothScrollStartY + y) * m_nPitch;
        memcpy(pDst, pLive, m_nPitch);
    }

    CDisplay::TArea area;
    area.x1 = 0;
    area.x2 = m_nWidth - 1;
    area.y1 = m_nSmoothScrollStartY;
    area.y2 = m_nSmoothScrollEndY;
    m_pFrameBuffer->SetArea(area, m_pSmoothScrollCompose);
}

void CTRenderer::Write(char chChar)
{
    switch (m_State)
    {
    case StateSkipTillCRLF: // skip processing of second double height line
        if (chChar == '\n' || chChar == '\r')
        {
            m_State = StateStart;
        }
        break;

    case StateStart:
        switch (chChar)
        {
        case '\b':
            CursorLeft();
            break;

        case '\t':
            Tabulator();
            break;

        case '\f':
            ClearDisplay();
            break;

        case '\n':
            NewLine();
            break;

        case '\r':
            CarriageReturn();
            break;

        case '\x0E': // Shift Out (Ctrl-N) -> Switch to G1
            m_bUseG1 = TRUE;
            break;

        case '\x0F': // Shift In (Ctrl-O) -> Switch to G0
            m_bUseG1 = FALSE;
            break;

        case '\x1b':
            m_State = StateEscape;
            break;

        default:
        {
            const unsigned char printable = static_cast<unsigned char>(chChar);
            if (printable >= 0x20U && printable != 0x7FU)
            {
                CTConfig *config = CTConfig::Get();
                if (config != nullptr && config->GetMarginBellEnabled() && config->GetBuzzerVolume() > 0U)
                {
                    const unsigned cols = GetColumns();
                    if (cols > 8U && m_pCharGen != nullptr)
                    {
                        const unsigned currentCol = m_nCursorX / m_pCharGen->GetCharWidth();
                        const unsigned bellCol = cols - 9U;
                        if (currentCol == bellCol)
                        {
                            CHAL::Get()->BEEP();
                        }
                    }
                }
            }
            DisplayChar(chChar);
            break;
        }
        }
        break;

    case StateEscape:
        if (m_bVT52Mode)
        {
            switch (chChar)
            {
            case 'A':
                CursorUp();
                m_State = StateStart;
                break;

            case 'B':
                CursorDown();
                m_State = StateStart;
                break;

            case 'C':
                CursorRight();
                m_State = StateStart;
                break;

            case 'D':
                CursorLeft();
                m_State = StateStart;
                break;

            case 'H':
                // VT52 cursor home
                m_nCursorX = 0;
                m_nCursorY = 0;
                m_State = StateStart;
                break;

            case 'I':
                ReverseScroll();
                m_State = StateStart;
                break;

            case 'J':
                ClearDisplayEnd();
                m_State = StateStart;
                break;

            case 'K':
                ClearLineEnd();
                m_State = StateStart;
                break;

            case 'Y':
                m_State = StateVT52Row;
                break;

            case '<':
                // switch to ANSI mode
                m_bVT52Mode = FALSE;
                m_State = StateStart;
                break;

            default:
                m_State = StateStart;
                break;
            }
        }
        else
        {
            switch (chChar)
            {
            case '[':
                m_State = StateBracket;
                m_nParam1 = 0;
                m_nParam2 = 0;
                break;

            case 'D':
                // IND
                CursorDown();
                m_State = StateStart;
                break;

            case 'M':
                // RI
                ReverseScroll();
                m_State = StateStart;
                break;

            case 'E':
                // NEL
                CarriageReturn();
                NewLine();
                m_State = StateStart;
                break;

            case 'H':
                // HTS
                if (m_pCharGen != nullptr)
                {
                    CTConfig *config = CTConfig::Get();
                    if (config != nullptr)
                    {
                        const unsigned charWidth = m_pCharGen->GetCharWidth();
                        if (charWidth != 0)
                        {
                            const unsigned currentCol = m_nCursorX / charWidth;
                            config->SetTabStop(currentCol, true);
                        }
                    }
                }
                m_State = StateStart;
                break;

            case '7':
                // DECSC save cursor
                SaveCursor();
                m_State = StateStart;
                break;

            case '8':
                // DECRC restore cursor
                RestoreCursor();
                m_State = StateStart;
                break;

            case '#':
                // implement DEC Terminal font size switch
                m_State = StateFontChange;
                break;

            case '(':
                // G0 character set
                m_State = StateG0;
                break;

            case ')':
                // G1 character set
                m_State = StateG1;
                break;

            case 'd':
                m_State = StateAutoPage;
                break;

            default:
                m_State = StateStart;
                break;
            }
        }
        break;

    case StateG0:
        if (chChar == 'A' || chChar == 'B')
        {
            m_G0CharSet = CharSetUS;
        }
        else if (chChar == '0')
        {
            m_G0CharSet = CharSetGraphics;
        }
        m_State = StateStart;
        break;

    case StateG1:
        if (chChar == 'A' || chChar == 'B')
        {
            m_G1CharSet = CharSetUS;
        }
        else if (chChar == '0')
        {
            m_G1CharSet = CharSetGraphics;
        }
        m_State = StateStart;
        break;

    case StateFontChange:
        switch (chChar)
        {
        case '3':
            // Double Width double Height top half -> ignore, as we do not support double height
            SetFont(m_CurrentFontSelection, CCharGenerator::FontFlagsDoubleBoth);
            m_State = StateStart;
            break;
        case '4':
            // Double Width double Height bottom half -> ignore, as we do not support double height
            m_State = StateSkipTillCRLF;
            break;
        case '5':
            // Standard DEC font mode using currently selected VT100 font family
            SetFont(m_CurrentFontSelection, CCharGenerator::FontFlagsNone);
            m_State = StateStart;
            break;
        case '6':
            // Double width mode using currently selected VT100 font family
            SetFont(m_CurrentFontSelection, CCharGenerator::FontFlagsDoubleWidth);
            m_State = StateStart;
            break;
        case '8':
            // Screen test pattern -> ignore
            m_State = StateStart;
            break;
        default:
            m_State = StateStart;
            break;
        }
        break;

    case StateVT52Row:
        if (chChar >= 0x20)
        {
            m_nParam1 = static_cast<unsigned>(chChar - 0x20);
            m_State = StateVT52Col;
        }
        else
        {
            m_State = StateStart;
        }
        break;

    case StateVT52Col:
        if (chChar >= 0x20)
        {
            m_nParam2 = static_cast<unsigned>(chChar - 0x20);
            CursorMove(m_nParam1, m_nParam2);
        }
        m_State = StateStart;
        break;

    case StateBracket:
        switch (chChar)
        {
        case 'Z':
            BackTabulator();
            m_State = StateStart;
            break;
        case 'g':
        {
            if (m_pCharGen != nullptr)
            {
                CTConfig *config = CTConfig::Get();
                if (config != nullptr)
                {
                    const unsigned charWidth = m_pCharGen->GetCharWidth();
                    if (charWidth != 0)
                    {
                        const unsigned currentCol = m_nCursorX / charWidth;
                        config->SetTabStop(currentCol, false);
                    }
                }
            }
            m_State = StateStart;
            break;
        }
        case '?':
            m_State = StateQuestionMark;
            break;

        case 'A':
            CursorUp();
            m_State = StateStart;
            break;

        case 'B':
            CursorDown();
            m_State = StateStart;
            break;

        case 'C':
            CursorRight();
            m_State = StateStart;
            break;

        case 'D':
            CursorLeft();
            m_State = StateStart;
            break;

        case 'H':
        case 'f':
            CursorHome();
            m_State = StateStart;
            break;

        case 'J':
            ClearDisplayEnd();
            m_State = StateStart;
            break;

        case 'K':
            ClearLineEnd();
            m_State = StateStart;
            break;

        case 'L':
            InsertLines(1);
            m_State = StateStart;
            break;

        case 'M':
            DeleteLines(1);
            m_State = StateStart;
            break;

        case 'P':
            DeleteChars(1);
            m_State = StateStart;
            break;

        case 'm':
            SetStandoutMode(0);
            m_State = StateStart;
            break;

        default:
            if ('0' <= chChar && chChar <= '9')
            {
                m_nParam1 = chChar - '0';
                m_State = StateNumber1;
            }
            else
            {
                m_State = StateStart;
            }
            break;
        }
        break;

    case StateNumber1:
        switch (chChar)
        {
        case 'A':
            CursorUp();
            for (unsigned i = 1; i < m_nParam1; ++i)
            {
                CursorUp();
            }
            m_State = StateStart;
            break;

        case 'B':
            CursorDown();
            for (unsigned i = 1; i < m_nParam1; ++i)
            {
                CursorDown();
            }
            m_State = StateStart;
            break;

        case 'C':
            CursorRight();
            for (unsigned i = 1; i < m_nParam1; ++i)
            {
                CursorRight();
            }
            m_State = StateStart;
            break;

        case 'D':
            CursorLeft();
            for (unsigned i = 1; i < m_nParam1; ++i)
            {
                CursorLeft();
            }
            m_State = StateStart;
            break;

        case 'H':
        case 'f':
            CursorMove(m_nParam1, 1);
            m_State = StateStart;
            break;

        case ';':
            m_State = StateSemicolon;
            break;

        case 'L':
            InsertLines(m_nParam1);
            m_State = StateStart;
            break;

        case 'M':
            DeleteLines(m_nParam1);
            m_State = StateStart;
            break;

        case 'P':
            DeleteChars(m_nParam1);
            m_State = StateStart;
            break;

        case 'X':
            EraseChars(m_nParam1);
            m_State = StateStart;
            break;

        case 'J':
            if (m_nParam1 == 0)
            {
                ClearDisplayEnd();
            }
            else if (m_nParam1 == 2)
            {
                const unsigned savedX = m_nCursorX;
                const unsigned savedY = m_nCursorY;
                m_nCursorX = 0;
                m_nCursorY = 0;
                ClearDisplayEnd();
                m_nCursorX = savedX;
                m_nCursorY = savedY;
            }
            else
            {
                // Fallback for unsupported modes
                ClearDisplay();
            }
            m_State = StateStart;
            break;

        case 'h':
        case 'l':
            if (m_nParam1 == 4)
            {
                InsertMode(chChar == 'h');
            }
            m_State = StateStart;
            break;

        case 'm':
            SetStandoutMode(m_nParam1);
            m_State = StateStart;
            break;

        case 'g':
        {
            CTConfig *config = CTConfig::Get();
            if (config != nullptr)
            {
                if (m_nParam1 == 0)
                {
                    if (m_pCharGen != nullptr)
                    {
                        const unsigned charWidth = m_pCharGen->GetCharWidth();
                        if (charWidth != 0)
                        {
                            const unsigned currentCol = m_nCursorX / charWidth;
                            config->SetTabStop(currentCol, false);
                        }
                    }
                }
                else if (m_nParam1 == 3)
                {
                    for (unsigned col = 0; col < CTConfig::TabStopsMax; ++col)
                    {
                        config->SetTabStop(col, false);
                    }
                }
            }
            m_State = StateStart;
            break;
        }

        default:
            if ('0' <= chChar && chChar <= '9')
            {
                m_nParam1 *= 10;
                m_nParam1 += chChar - '0';

                if (m_nParam1 > 199)
                {
                    m_State = StateStart;
                }
            }
            else
            {
                m_State = StateStart;
            }
            break;
        }
        break;

    case StateSemicolon:
        if ('0' <= chChar && chChar <= '9')
        {
            m_nParam2 = chChar - '0';
            m_State = StateNumber2;
        }
        else if (chChar == 'H' || chChar == 'f')
        {
            CursorMove(m_nParam1, 1);
            m_State = StateStart;
        }
        else
        {
            m_State = StateStart;
        }
        break;

    case StateQuestionMark:
        if ('0' <= chChar && chChar <= '9')
        {
            m_nParam1 = chChar - '0';
            m_State = StateNumber3;
        }
        else
        {
            m_State = StateStart;
        }
        break;

    case StateNumber2:
        switch (chChar)
        {
        case 'H':
        case 'f':
            CursorMove(m_nParam1, m_nParam2);
            m_State = StateStart;
            break;

        case 'r':
            SetScrollRegion(m_nParam1, m_nParam2);
            m_State = StateStart;
            break;

        default:
            if ('0' <= chChar && chChar <= '9')
            {
                m_nParam2 *= 10;
                m_nParam2 += chChar - '0';

                if (m_nParam2 > 199)
                {
                    m_State = StateStart;
                }
            }
            else
            {
                m_State = StateStart;
            }
            break;
        }
        break;

    case StateNumber3:
        switch (chChar)
        {
        case 'h':
            if (m_nParam1 == 25)
            {
                SetCursorMode(TRUE);
            }
            m_State = StateStart;
            break;

        case 'l':
            if (m_nParam1 == 25)
            {
                SetCursorMode(FALSE);
            }
            else if (m_nParam1 == 2)
            {
                m_bVT52Mode = TRUE;
            }
            m_State = StateStart;
            break;

        default:
            if ('0' <= chChar && chChar <= '9')
            {
                m_nParam1 *= 10;
                m_nParam1 += chChar - '0';

                if (m_nParam1 > 99)
                {
                    m_State = StateStart;
                }
            }
            else
            {
                m_State = StateStart;
            }
            break;
        }
        break;

    case StateAutoPage:
        switch (chChar)
        {
        case '+':
            SetAutoPageMode(TRUE);
            m_State = StateStart;
            break;

        case '*':
            SetAutoPageMode(FALSE);
            m_State = StateStart;
            break;

        default:
            m_State = StateStart;
            break;
        }
        break;

    default:
        m_State = StateStart;
        break;
    }
}

void CTRenderer::CarriageReturn(void)
{
    m_nCursorX = 0;
}

void CTRenderer::ClearDisplay(void)
{
    m_nCursorX = 0;
    m_nCursorY = 0;
    ClearDisplayEnd();
}

void CTRenderer::ClearDisplayEnd(void)
{
    ClearLineEnd();

    unsigned nPosY = m_nCursorY + m_pCharGen->GetCharHeight();
    unsigned nOffset = nPosY * m_nWidth;

    switch (m_nDepth)
    {
    case 1:
    {
        nOffset /= 8;
        unsigned nSize = m_nSize - nOffset;
        for (u8 *pBuffer = m_pBuffer8 + nOffset; nSize--;)
        {
            *pBuffer++ = m_BackgroundColor ? 0xFF : 0;
        }
    }
    break;

    case 8:
    {
        unsigned nSize = m_nSize - nOffset;
        for (u8 *pBuffer = m_pBuffer8 + nOffset; nSize--;)
        {
            *pBuffer++ = (u8)m_BackgroundColor;
        }
    }
    break;

    case 16:
    {
        unsigned nSize = m_nSize / 2 - nOffset;
        for (u16 *pBuffer = m_pBuffer16 + nOffset; nSize--;)
        {
            *pBuffer++ = (u16)m_BackgroundColor;
        }
    }
    break;

    case 32:
    {
        unsigned nSize = m_nSize / 4 - nOffset;
        for (u32 *pBuffer = m_pBuffer32 + nOffset; nSize--;)
        {
            *pBuffer++ = (u32)m_BackgroundColor;
        }
    }
    break;
    }

    SetUpdateArea(m_nCursorY, m_nHeight - 1);
}

void CTRenderer::ClearLineEnd(void)
{
    for (unsigned nPosX = m_nCursorX; nPosX < m_nUsedWidth; nPosX += m_pCharGen->GetCharWidth())
    {
        EraseChar(nPosX, m_nCursorY);
    }

    for (unsigned nPosX = m_nUsedWidth; nPosX < m_nWidth; nPosX++)
    {
        for (unsigned nPosY = m_nCursorY;
             nPosY < m_nCursorY + m_pCharGen->GetCharHeight(); nPosY++)
        {
            SetRawPixel(nPosX, nPosY, m_BackgroundColor);
        }
    }
}

void CTRenderer::CursorDown(void)
{
    m_nCursorY += m_pCharGen->GetCharHeight();
    if (m_nCursorY >= m_nScrollEnd)
    {
        if (!m_bAutoPage)
        {
            Scroll();

            m_nCursorY -= m_pCharGen->GetCharHeight();
        }
        else
        {
            m_nCursorY = m_nScrollStart;
        }
    }
}

void CTRenderer::CursorHome(void)
{
    m_nCursorX = 0;
    m_nCursorY = m_nScrollStart;
}

void CTRenderer::CursorLeft(void)
{
    if (m_nCursorX > 0)
    {
        m_nCursorX -= m_pCharGen->GetCharWidth();
    }
    else
    {
        if (m_nCursorY > m_nScrollStart)
        {
            m_nCursorX = m_nUsedWidth - m_pCharGen->GetCharWidth();
            m_nCursorY -= m_pCharGen->GetCharHeight();
        }
    }
}

void CTRenderer::CursorMove(unsigned nRow, unsigned nColumn)
{
    unsigned nPosX = (nColumn - 1) * m_pCharGen->GetCharWidth();
    unsigned nPosY = (nRow - 1) * m_pCharGen->GetCharHeight();

    if (nPosX < m_nUsedWidth && nPosY < m_nUsedHeight)
    {
        m_nCursorX = nPosX;
        m_nCursorY = nPosY;
    }
}

void CTRenderer::CursorRight(void)
{
    m_nCursorX += m_pCharGen->GetCharWidth();
    if (m_nCursorX >= m_nUsedWidth)
    {
        NewLine();
    }
}

void CTRenderer::CursorUp(void)
{
    if (m_nCursorY > m_nScrollStart)
    {
        m_nCursorY -= m_pCharGen->GetCharHeight();
    }
}

void CTRenderer::DeleteChars(unsigned nCount) // TODO
{
    if (nCount == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charWidth == 0 || charHeight == 0)
    {
        return;
    }

    if (m_nCursorX >= m_nUsedWidth)
    {
        return;
    }

    unsigned pixelWidth = nCount * charWidth;
    const unsigned maxShift = m_nUsedWidth - m_nCursorX;
    if (pixelWidth > maxShift)
    {
        pixelWidth = maxShift;
    }

    if (pixelWidth == 0)
    {
        return;
    }

    const CDisplay::TRawColor bgColor = GetTextBackgroundColor();
    const unsigned startY = m_nCursorY;
    const unsigned endY = m_nCursorY + charHeight;
    const unsigned shiftEndX = m_nUsedWidth - pixelWidth;

    for (unsigned y = startY; y < endY; ++y)
    {
        for (unsigned x = m_nCursorX; x < shiftEndX; ++x)
        {
            SetRawPixel(x, y, GetRawPixel(x + pixelWidth, y));
        }

        for (unsigned x = shiftEndX; x < m_nUsedWidth; ++x)
        {
            SetRawPixel(x, y, bgColor);
        }
    }

    SetUpdateArea(startY, endY - 1);
}

void CTRenderer::DeleteLines(unsigned nCount) // TODO
{
    if (nCount == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    if (m_nCursorY < m_nScrollStart || m_nCursorY >= m_nScrollEnd)
    {
        return;
    }

    const unsigned maxLines = (m_nScrollEnd - m_nCursorY) / charHeight;
    if (maxLines == 0)
    {
        return;
    }

    if (nCount > maxLines)
    {
        nCount = maxLines;
    }

    bool smoothStarted = false;
    unsigned startTicks = 0;
    if (nCount == 1)
    {
        smoothStarted = BeginSmoothScrollAnimation(m_nCursorY, m_nScrollEnd - 1, FALSE) ? true : false;
    }

    if (!smoothStarted)
    {
        startTicks = CTimer::Get()->GetTicks();
    }

    const unsigned lineBytes = m_nPitch * charHeight;
    const unsigned startOffset = m_nCursorY * m_nPitch;
    const unsigned endOffset = m_nScrollEnd * m_nPitch;
    const unsigned deleteBytes = lineBytes * nCount;
    const unsigned moveBytes = endOffset - startOffset - deleteBytes;

    if (moveBytes > 0)
    {
        memmove(m_pBuffer8 + startOffset, m_pBuffer8 + startOffset + deleteBytes, moveBytes);
    }

    u8 *pClear = m_pBuffer8 + endOffset - deleteBytes;
    unsigned clearBytes = deleteBytes;
    switch (m_nDepth)
    {
    case 1:
    {
        const u8 fill = m_BackgroundColor ? 0xFF : 0x00;
        while (clearBytes--)
        {
            *pClear++ = fill;
        }
    }
    break;

    case 8:
        while (clearBytes--)
        {
            *pClear++ = (u8)m_BackgroundColor;
        }
        break;

    case 16:
    {
        u16 *p16 = reinterpret_cast<u16 *>(pClear);
        unsigned count = clearBytes / 2;
        while (count--)
        {
            *p16++ = (u16)m_BackgroundColor;
        }
    }
    break;

    case 32:
    {
        u32 *p32 = reinterpret_cast<u32 *>(pClear);
        unsigned count = clearBytes / 4;
        while (count--)
        {
            *p32++ = (u32)m_BackgroundColor;
        }
    }
    break;
    }

    SetUpdateArea(m_nCursorY, m_nScrollEnd - 1);

    if (!smoothStarted)
    {
        const unsigned endTicks = CTimer::Get()->GetTicks();
        m_ScrollNormalTicksAccum += static_cast<unsigned>(endTicks - startTicks);
        ++m_ScrollNormalCount;
    }
}

void CTRenderer::DisplayChar(char chChar)
{
    // TODO: Insert mode

    if (' ' <= (unsigned char)chChar)
    {
        ECharacterSet activeSet = m_bUseG1 ? m_G1CharSet : m_G0CharSet;
        bool bUseGraphics = (activeSet == CharSetGraphics) &&
                            (unsigned char)chChar >= 0x60 && (unsigned char)chChar <= 0x7E;

        CCharGenerator *pOriginalGen = nullptr;
        if (bUseGraphics && m_pGraphicsCharGen != nullptr)
        {
            pOriginalGen = m_pCharGen;
            m_pCharGen = m_pGraphicsCharGen;
        }

        DisplayChar(chChar, m_nCursorX, m_nCursorY, GetTextColor());

        if (pOriginalGen != nullptr)
        {
            m_pCharGen = pOriginalGen;
        }

        bool wrapAroundEnabled = true;
        CTConfig *config = CTConfig::Get();
        if (config != nullptr)
        {
            wrapAroundEnabled = config->GetWrapAroundEnabled();
        }

        if (wrapAroundEnabled)
        {
            CursorRight();
        }
        else
        {
            const unsigned charWidth = m_pCharGen->GetCharWidth();
            if (charWidth != 0 && m_nUsedWidth >= charWidth)
            {
                const unsigned lastColumnX = m_nUsedWidth - charWidth;
                if (m_nCursorX < lastColumnX)
                {
                    m_nCursorX += charWidth;
                }
                else
                {
                    m_nCursorX = lastColumnX;
                }
            }
        }
    }
}

void CTRenderer::EraseChars(unsigned nCount)
{
    if (nCount == 0)
    {
        return;
    }

    unsigned nEndX = m_nCursorX + nCount * m_pCharGen->GetCharWidth();
    if (nEndX > m_nUsedWidth)
    {
        nEndX = m_nUsedWidth;
    }

    for (unsigned nPosX = m_nCursorX; nPosX < nEndX; nPosX += m_pCharGen->GetCharWidth())
    {
        EraseChar(nPosX, m_nCursorY);
    }
}

CDisplay::TRawColor CTRenderer::GetTextBackgroundColor(void)
{
    return m_bReverseAttribute ? AdjustBrightness565(m_ForegroundColor, m_ReverseBackgroundScaleFactor) : m_BackgroundColor;
}

CDisplay::TRawColor CTRenderer::GetTextColor(void)
{
    if (m_bReverseAttribute)
    {
        return AdjustBrightness565(m_ForegroundColor, m_ReverseForegroundScaleFactor);
    }

    return m_ForegroundColor;
}

void CTRenderer::InsertLines(unsigned nCount) // TODO
{
    if (nCount == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    if (m_nCursorY < m_nScrollStart || m_nCursorY >= m_nScrollEnd)
    {
        return;
    }

    const unsigned maxLines = (m_nScrollEnd - m_nCursorY) / charHeight;
    if (maxLines == 0)
    {
        return;
    }

    if (nCount > maxLines)
    {
        nCount = maxLines;
    }

    bool smoothStarted = false;
    unsigned startTicks = 0;
    if (nCount == 1)
    {
        smoothStarted = BeginSmoothScrollAnimation(m_nCursorY, m_nScrollEnd - 1, TRUE) ? true : false;
    }

    if (!smoothStarted)
    {
        startTicks = CTimer::Get()->GetTicks();
    }

    const unsigned lineBytes = m_nPitch * charHeight;
    const unsigned startOffset = m_nCursorY * m_nPitch;
    const unsigned endOffset = m_nScrollEnd * m_nPitch;
    const unsigned insertBytes = lineBytes * nCount;
    const unsigned moveBytes = endOffset - startOffset - insertBytes;

    if (moveBytes > 0)
    {
        memmove(m_pBuffer8 + startOffset + insertBytes, m_pBuffer8 + startOffset, moveBytes);
    }

    u8 *pClear = m_pBuffer8 + startOffset;
    unsigned clearBytes = insertBytes;
    switch (m_nDepth)
    {
    case 1:
    {
        const u8 fill = m_BackgroundColor ? 0xFF : 0x00;
        while (clearBytes--)
        {
            *pClear++ = fill;
        }
    }
    break;

    case 8:
        while (clearBytes--)
        {
            *pClear++ = (u8)m_BackgroundColor;
        }
        break;

    case 16:
    {
        u16 *p16 = reinterpret_cast<u16 *>(pClear);
        unsigned count = clearBytes / 2;
        while (count--)
        {
            *p16++ = (u16)m_BackgroundColor;
        }
    }
    break;

    case 32:
    {
        u32 *p32 = reinterpret_cast<u32 *>(pClear);
        unsigned count = clearBytes / 4;
        while (count--)
        {
            *p32++ = (u32)m_BackgroundColor;
        }
    }
    break;
    }

    SetUpdateArea(m_nCursorY, m_nScrollEnd - 1);

    if (!smoothStarted)
    {
        const unsigned endTicks = CTimer::Get()->GetTicks();
        m_ScrollNormalTicksAccum += static_cast<unsigned>(endTicks - startTicks);
        ++m_ScrollNormalCount;
    }
}

void CTRenderer::InsertMode(boolean bBegin)
{
    m_bInsertOn = bBegin;
}

void CTRenderer::SetSmoothScrollEnabled(boolean bEnable)
{
    m_bSmoothScrollEnabled = bEnable;
    if (!m_bSmoothScrollEnabled)
    {
        m_bSmoothScrollActive = FALSE;
    }
}

void CTRenderer::NewLine(void)
{
    CarriageReturn();
    CursorDown();
}

void CTRenderer::ReverseScroll(void)
{
    if (m_nCursorY == m_nScrollStart)
    {
        InsertLines(1);
    }
}

void CTRenderer::SetAutoPageMode(boolean bEnable)
{
    m_bAutoPage = bEnable;
}

void CTRenderer::SetBrightnessScaling(float boldFactor,
                                      float reverseBackgroundFactor,
                                      float reverseForegroundFactor)
{
    if (boldFactor < 0.0f)
    {
        boldFactor = 0.0f;
    }

    if (reverseBackgroundFactor < 0.0f)
    {
        reverseBackgroundFactor = 0.0f;
    }

    if (reverseForegroundFactor < 0.0f)
    {
        reverseForegroundFactor = 0.0f;
    }

    m_BoldScaleFactor = boldFactor;
    m_ReverseBackgroundScaleFactor = reverseBackgroundFactor;
    m_ReverseForegroundScaleFactor = reverseForegroundFactor;
}

void CTRenderer::SetCursorMode(boolean bVisible)
{
    m_bCursorOn = bVisible;
}

void CTRenderer::SetVT52Mode(boolean bEnable)
{
    m_bVT52Mode = bEnable;
}

void CTRenderer::ForceHideCursor(void)
{
    m_SpinLock.Acquire();

    if (m_bCursorOn && m_bCursorVisible)
    {
        InvertCursor();
    }

    m_bCursorVisible = false;

    m_SpinLock.Release();
}

void CTRenderer::SetScrollRegion(unsigned nStartRow, unsigned nEndRow)
{
    unsigned nScrollStart = (nStartRow - 1) * m_pCharGen->GetCharHeight();
    unsigned nScrollEnd = nEndRow * m_pCharGen->GetCharHeight();

    if (nScrollStart < m_nUsedHeight && nScrollEnd > 0 && nScrollEnd <= m_nUsedHeight && nScrollStart < nScrollEnd)
    {
        m_nScrollStart = nScrollStart;
        m_nScrollEnd = nScrollEnd;
    }

    CursorHome();
}

// TODO: standout mode should be useable together with one other mode
void CTRenderer::SetStandoutMode(unsigned nMode)
{
    switch (nMode)
    {
    case 0:
        // reset all attributes
        m_bReverseAttribute = FALSE;
        m_bBlinkAttribute = FALSE;
        m_bBoldAttribute = FALSE;
        m_bDimAttribute = FALSE;
        m_bUnderlineAttribute = FALSE;
        m_ForegroundColor = m_DefaultForegroundColor;
        m_BackgroundColor = m_DefaultBackgroundColor;
        break;

    case 1: // bold font - change glyph rendering
        m_bBoldAttribute = TRUE;
        break;

    case 2: // dim / half-bright
        m_bDimAttribute = TRUE;
        break;

    case 4: // underlined - change glyph rendering
        m_bUnderlineAttribute = TRUE;

        break;

    case 5: // VT100, VT220 and VT320 support blink attribute
        m_bBlinkAttribute = TRUE;
        break;

    case 7: // reverse video
        m_bReverseAttribute = TRUE;
        break;

    case 27: // reverse video off
        m_bReverseAttribute = FALSE;
        break;

    default:
        break;
    }
}

void CTRenderer::Tabulator(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    if (charWidth == 0)
    {
        return;
    }

    const unsigned currentCol = m_nCursorX / charWidth;
    const unsigned cols = GetColumns();

    CTConfig *config = CTConfig::Get();
    if (config != nullptr && cols > 0)
    {
        for (unsigned col = currentCol + 1; col < cols; ++col)
        {
            if (config->IsTabStop(col))
            {
                m_nCursorX = col * charWidth;
                return;
            }
        }
    }

    unsigned nTabWidth = charWidth * 8;
    m_nCursorX = ((m_nCursorX + nTabWidth) / nTabWidth) * nTabWidth;
    if (m_nCursorX >= m_nUsedWidth)
    {
        NewLine();
    }
}

void CTRenderer::BackTabulator(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    if (charWidth == 0)
    {
        return;
    }

    const unsigned currentCol = m_nCursorX / charWidth;
    const unsigned cols = GetColumns();

    CTConfig *config = CTConfig::Get();
    if (config != nullptr && cols > 0)
    {
        for (int col = static_cast<int>(currentCol) - 1; col >= 0; --col)
        {
            if (config->IsTabStop(static_cast<unsigned>(col)))
            {
                m_nCursorX = static_cast<unsigned>(col) * charWidth;
                return;
            }
        }
    }

    const unsigned tabWidth = charWidth * 8;
    const unsigned currentPos = m_nCursorX;
    if (currentPos >= tabWidth)
    {
        m_nCursorX = ((currentPos - 1) / tabWidth) * tabWidth;
    }
    else
    {
        m_nCursorX = 0;
    }
}

void CTRenderer::SaveCursor(void)
{
    m_SpinLock.Acquire();
    m_SavedState.cursorX = m_nCursorX;
    m_SavedState.cursorY = m_nCursorY;
    m_SavedState.reverseAttribute = m_bReverseAttribute;
    m_SavedState.boldAttribute = m_bBoldAttribute;
    m_SavedState.underlineAttribute = m_bUnderlineAttribute;
    m_SavedState.blinkAttribute = m_bBlinkAttribute;
    m_SavedState.foreground = m_ForegroundColor;
    m_SavedState.background = m_BackgroundColor;
    m_SavedState.defaultForeground = m_DefaultForegroundColor;
    m_SavedState.defaultBackground = m_DefaultBackgroundColor;
    m_SavedState.fontFlags = m_FontFlags;
    
    // Some terminals save Origin Mode, Wrap Mode, and Character Set here too.
    // For now we just stick to visual attributes and position.
    m_SpinLock.Release();
}

void CTRenderer::RestoreCursor(void)
{
    m_SpinLock.Acquire();
    
    // Restore position, clamped to current screen dimensions
    if (m_SavedState.cursorX < m_nWidth)
    {
        m_nCursorX = m_SavedState.cursorX;
    }
    else
    {
        m_nCursorX = m_nWidth - (m_nWidth % m_pCharGen->GetCharWidth());
        if (m_nCursorX > 0) m_nCursorX -= m_pCharGen->GetCharWidth();
    }

    if (m_SavedState.cursorY < m_nHeight)
    {
        m_nCursorY = m_SavedState.cursorY;
    }
    else
    {
        m_nCursorY = m_nHeight - m_pCharGen->GetCharHeight();
    }

    // Restore attributes
    m_bReverseAttribute = m_SavedState.reverseAttribute;
    m_bBoldAttribute = m_SavedState.boldAttribute;
    m_bUnderlineAttribute = m_SavedState.underlineAttribute;
    m_bBlinkAttribute = m_SavedState.blinkAttribute;
    
    m_ForegroundColor = m_SavedState.foreground;
    m_BackgroundColor = m_SavedState.background;
    m_DefaultForegroundColor = m_SavedState.defaultForeground;
    m_DefaultBackgroundColor = m_SavedState.defaultBackground;
    
    // Note: We don't restore the font itself, as that might require loading resources, 
    // but we can restore flags if matched. To be safe, we usually only restore
    // attributes that don't change resource allocation.
    
    m_SpinLock.Release();
}

void CTRenderer::Scroll(void)
{
    unsigned nLines = m_pCharGen->GetCharHeight();

    const bool smoothStarted = BeginSmoothScrollAnimation(m_nScrollStart, m_nScrollEnd - 1, FALSE) ? true : false;
    unsigned startTicks = 0;
    if (!smoothStarted)
    {
        startTicks = CTimer::Get()->GetTicks();
    }

    u8 *pTo = m_pBuffer8 + m_nScrollStart * m_nPitch;
    u8 *pFrom = m_pBuffer8 + (m_nScrollStart + nLines) * m_nPitch;

    unsigned nSize = m_nPitch * (m_nScrollEnd - m_nScrollStart - nLines);
    if (nSize)
    {
        memcpy(pTo, pFrom, nSize);

        pTo += nSize;
    }

    nSize = m_nWidth * nLines;
    switch (m_nDepth)
    {
    case 1:
    {
        nSize /= 8;
        for (u8 *p = (u8 *)pTo; nSize--;)
        {
            *p++ = m_BackgroundColor ? 0xFF : 0;
        }
    }
    break;

    case 8:
    {
        for (u8 *p = (u8 *)pTo; nSize--;)
        {
            *p++ = (u8)m_BackgroundColor;
        }
    }
    break;

    case 16:
    {
        for (u16 *p = (u16 *)pTo; nSize--;)
        {
            *p++ = (u16)m_BackgroundColor;
        }
    }
    break;

    case 32:
    {
        for (u32 *p = (u32 *)pTo; nSize--;)
        {
            *p++ = (u32)m_BackgroundColor;
        }
    }
    break;
    }

    SetUpdateArea(0, m_nHeight - 1);

    if (!smoothStarted)
    {
        const unsigned endTicks = CTimer::Get()->GetTicks();
        m_ScrollNormalTicksAccum += static_cast<unsigned>(endTicks - startTicks);
        ++m_ScrollNormalCount;
    }
}

void CTRenderer::DisplayChar(char chChar, unsigned nPosX, unsigned nPosY,
                             CDisplay::TRawColor nColor)
{
    if (nColor != m_BackgroundColor)
    {
        if (m_bBoldAttribute)
        {
            nColor = AdjustBrightness565(nColor, m_BoldScaleFactor);
        }
        else if (m_bDimAttribute)
        {
            nColor = AdjustBrightness565(nColor, m_DimScaleFactor);
        }
    }

    for (unsigned y = 0; y < m_pCharGen->GetCharHeight(); y++)
    {
        CCharGenerator::TPixelLine Line = m_pCharGen->GetPixelLine(chChar, y);

        for (unsigned x = 0; x < m_pCharGen->GetCharWidth(); x++)
        {
            const bool isGlyphPixel = m_pCharGen->GetPixel(x, Line);
            const CDisplay::TRawColor pixelColor = isGlyphPixel ? nColor : GetTextBackgroundColor();
            SetRawPixel(nPosX + x, nPosY + y, pixelColor);
        }
    }

    if (m_bBoldAttribute)
    {
        // Overstrike once to simulate a thick stroke
        for (unsigned y = 0; y < m_pCharGen->GetCharHeight(); y++)
        {
            CCharGenerator::TPixelLine Line = m_pCharGen->GetPixelLine(chChar, y);

            for (unsigned x = 1; x < m_pCharGen->GetCharWidth(); x++)
            {
                if (m_pCharGen->GetPixel(x - 1, Line))
                {
                    SetRawPixel(nPosX + x, nPosY + y, nColor);
                }
            }
        }
    }

    if (m_bUnderlineAttribute)
    {
        const unsigned underlineRow = m_pCharGen->GetUnderline();
        if (underlineRow < m_pCharGen->GetCharHeight())
        {
            for (unsigned x = 0; x < m_pCharGen->GetCharWidth(); x++)
            {
                SetRawPixel(nPosX + x, nPosY + underlineRow, nColor);
            }
        }
    }

    SetUpdateArea(nPosY, nPosY + m_pCharGen->GetCharHeight() - 1);
}

void CTRenderer::EraseChar(unsigned nPosX, unsigned nPosY)
{
    for (unsigned y = 0; y < m_pCharGen->GetCharHeight(); y++)
    {
        for (unsigned x = 0; x < m_pCharGen->GetCharWidth(); x++)
        {
            SetRawPixel(nPosX + x, nPosY + y, m_BackgroundColor);
        }
    }

    SetUpdateArea(nPosY, nPosY + m_pCharGen->GetCharHeight() - 1);
}

void CTRenderer::InvertCursor(void)
{
    if (!m_bCursorOn)
    {
        return;
    }

    CDisplay::TRawColor *pPixelData = m_pCursorPixels;
    unsigned y0 = m_bCursorBlock ? 0 : m_pCharGen->GetUnderline();

    CDisplay::TRawColor invertMask = m_ForegroundColor ^ m_BackgroundColor;
    if (invertMask == 0)
    {
        switch (m_nDepth)
        {
        case 1:
            invertMask = 0x1;
            break;
        case 8:
            invertMask = 0xFF;
            break;
        case 16:
            invertMask = 0xFFFF;
            break;
        case 32:
            invertMask = 0xFFFFFFFF;
            break;
        default:
            invertMask = static_cast<CDisplay::TRawColor>(~0u);
            break;
        }
    }
    for (unsigned y = y0; y < m_pCharGen->GetCharHeight(); y++)
    {
        for (unsigned x = 0; x < m_pCharGen->GetCharWidth(); x++)
        {
            if (!m_bCursorVisible)
            {
                // Store the old pixel
                const CDisplay::TRawColor storedPixel = GetRawPixel(m_nCursorX + x, m_nCursorY + y);
                *pPixelData++ = storedPixel;

                // Plot the cursor by inverting the stored pixel
                SetRawPixel(m_nCursorX + x, m_nCursorY + y, storedPixel ^ invertMask);
            }
            else
            {
                // Restore the backingstore for the cursor colour
                SetRawPixel(m_nCursorX + x, m_nCursorY + y, *pPixelData++);
            }
        }
    }

    m_bCursorVisible = !m_bCursorVisible;

    SetUpdateArea(m_nCursorY + y0, m_nCursorY + m_pCharGen->GetCharHeight() - 1);
}

void CTRenderer::doRenderTest(void)
{
    static boolean once = true;
    if (!once)
    {
        return;
    }
    once = false;
    static const char kDefaultMsg[] = "ESC#5 VT100 default font";
    static const char kDoubleMsg[] = "ESC#6 VT100 double-width font";
    static const char kDoubleBothMsg1[] = "ESC#3 VT100 double-width+height font";
    static const char kDoubleBothMsg2[] = "ESC#4 VT100 double-width+height font\n";
    static const char kBoldMsg[] = "ESC#5 VT100 \x1B[1m bold \x1B[0m font\n";
    static const char kUnderlineMsg[] = "ESC#5 VT100 \x1B[4m underline \x1B[0m font\n";
    static const char kReverseMsg[] = "ESC#5 VT100 \x1B[7m\x1B[4m reverse \x1B[0m font\n";
    static const char kReverseMsg2[] = "\x1B[7m                                             \x1B[0m\n";
    static const char kClearScreen[] = "\x1B[2J\x1B[H";
    static const char kESC_3[] = "\x1B#3";
    static const char kESC_5[] = "\x1B#5";
    static const char kESC_6[] = "\x1B#6";

    static TFont font = CTFontConverter::Get()->GetFont(EFontSelection::VT100Font10x20);

    struct RendererState
    {
        const TFont *font;
        CCharGenerator::TFontFlags fontFlags;
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        CDisplay::TRawColor defaultForeground;
        CDisplay::TRawColor defaultBackground;
        unsigned cursorX;
        unsigned cursorY;
        bool cursorBlock;
        bool cursorOn;
        bool cursorVisible;
        bool blinking;
        unsigned blinkTicks;
    } savedState;

    m_SpinLock.Acquire();
    savedState.font = m_pFont;
    savedState.fontFlags = m_FontFlags;
    savedState.foreground = m_ForegroundColor;
    savedState.background = m_BackgroundColor;
    savedState.defaultForeground = m_DefaultForegroundColor;
    savedState.defaultBackground = m_DefaultBackgroundColor;
    savedState.cursorX = m_nCursorX;
    savedState.cursorY = m_nCursorY;
    savedState.cursorBlock = m_bCursorBlock;
    savedState.cursorOn = m_bCursorOn;
    savedState.cursorVisible = m_bCursorVisible;
    savedState.blinking = m_bBlinkingCursor;
    savedState.blinkTicks = m_nCursorBlinkPeriodTicks;
    m_SpinLock.Release();

    if (savedState.cursorVisible && savedState.cursorOn)
    {
        m_SpinLock.Acquire();
        InvertCursor();
        m_SpinLock.Release();
    }

    m_SpinLock.Acquire();
    m_bCursorOn = FALSE;
    m_bBlinkingCursor = FALSE;
    m_bCursorVisible = FALSE;
    m_SpinLock.Release();

    SetColors(kColorGreen, kColorBlack);
    SetCursorBlock(TRUE);
    SetBlinkingCursor(TRUE, 500);
    Write(kClearScreen, strlen(kClearScreen));

    // Default font
    SetFont(font, CCharGenerator::FontFlagsNone);
    Goto(2, 0);
    Write(kESC_5, strlen(kESC_5));
    Write(kDefaultMsg, strlen(kDefaultMsg));
    NewLine();

    // Double-width font
    SetFont(font, CCharGenerator::FontFlagsDoubleWidth);
    Goto(6, 0);
    Write(kESC_6, strlen(kESC_6));
    Write(kDoubleMsg, strlen(kDoubleMsg));
    NewLine();

    // Double-width + double-height font
    SetFont(font, CCharGenerator::FontFlagsDoubleBoth);
    Goto(10, 0);
    Write(kESC_3, strlen(kESC_3));
    Write(kDoubleBothMsg1, strlen(kDoubleBothMsg1));
    Write(kDoubleBothMsg2, strlen(kDoubleBothMsg2));

    // Bold font
    SetFont(font, CCharGenerator::FontFlagsNone);
    Goto(14, 0);
    Write(kESC_5, strlen(kESC_5));
    Write(kBoldMsg, strlen(kBoldMsg));

    // Underline font
    Goto(18, 0);
    Write(kUnderlineMsg, strlen(kUnderlineMsg));

    // Reverse video
    SetFont(font, CCharGenerator::FontFlagsDoubleBoth);
    Goto(22, 0);
    Write(kReverseMsg, strlen(kReverseMsg));
    Write(kReverseMsg2, strlen(kReverseMsg2));

    if (savedState.font != nullptr)
    {
        SetFont(*savedState.font, savedState.fontFlags);
    }

    const unsigned restoredCursorX = (savedState.cursorX < m_nWidth) ? savedState.cursorX : 0;
    const unsigned restoredCursorY = (savedState.cursorY < m_nHeight) ? savedState.cursorY : 0;

    m_SpinLock.Acquire();
    m_ForegroundColor = savedState.foreground;
    m_BackgroundColor = savedState.background;
    m_DefaultForegroundColor = savedState.defaultForeground;
    m_DefaultBackgroundColor = savedState.defaultBackground;
    m_bCursorBlock = savedState.cursorBlock;
    m_bBlinkingCursor = savedState.blinking;
    m_nCursorBlinkPeriodTicks = savedState.blinkTicks ? savedState.blinkTicks : 1;
    m_nCursorX = restoredCursorX;
    m_nCursorY = restoredCursorY;
    m_bCursorOn = savedState.cursorOn;
    m_bCursorVisible = FALSE;
    if (m_bBlinkingCursor)
    {
        m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;
    }
    else
    {
        m_nNextCursorBlink = CTimer::Get()->GetTicks();
    }
    m_SpinLock.Release();

    if (savedState.cursorOn && savedState.cursorVisible)
    {
        m_SpinLock.Acquire();
        InvertCursor();
        m_SpinLock.Release();
    }
}

void CTRenderer::SaveState(TRendererState &state)
{
    m_SpinLock.Acquire();
    state.font = m_pFont;
    state.fontFlags = m_FontFlags;
    state.foreground = m_ForegroundColor;
    state.background = m_BackgroundColor;
    state.defaultForeground = m_DefaultForegroundColor;
    state.defaultBackground = m_DefaultBackgroundColor;
    state.cursorX = m_nCursorX;
    state.cursorY = m_nCursorY;
    state.cursorOn = m_bCursorOn;
    state.cursorBlock = m_bCursorBlock;
    state.cursorVisible = m_bCursorVisible;
    state.blinking = m_bBlinkingCursor;
    state.blinkTicks = m_nCursorBlinkPeriodTicks;
    state.nextBlink = m_nNextCursorBlink;
    state.scrollStart = m_nScrollStart;
    state.scrollEnd = m_nScrollEnd;
    state.reverseAttribute = m_bReverseAttribute;
    state.boldAttribute = m_bBoldAttribute;
    state.underlineAttribute = m_bUnderlineAttribute;
    state.blinkAttribute = m_bBlinkAttribute;
    state.insertOn = m_bInsertOn;
    state.autoPage = m_bAutoPage;
    state.delayedUpdate = m_bDelayedUpdate;
    state.lastUpdateTicks = m_nLastUpdateTicks;
    state.parserState = static_cast<unsigned>(m_State);
    state.param1 = m_nParam1;
    state.param2 = m_nParam2;
    state.g0CharSet = static_cast<unsigned>(m_G0CharSet);
    state.g1CharSet = static_cast<unsigned>(m_G1CharSet);
    state.useG1 = m_bUseG1;
    m_SpinLock.Release();
}

void CTRenderer::RestoreState(const TRendererState &state)
{
    if (m_bCursorVisible && m_bCursorOn)
    {
        m_SpinLock.Acquire();
        InvertCursor();
        m_SpinLock.Release();
    }

    if (state.font != nullptr)
    {
        SetFont(*state.font, state.fontFlags);
    }

    const unsigned restoredCursorX = (state.cursorX < m_nWidth) ? state.cursorX : 0;
    const unsigned restoredCursorY = (state.cursorY < m_nHeight) ? state.cursorY : 0;

    m_SpinLock.Acquire();
    m_ForegroundColor = state.foreground;
    m_BackgroundColor = state.background;
    m_DefaultForegroundColor = state.defaultForeground;
    m_DefaultBackgroundColor = state.defaultBackground;
    m_nCursorX = restoredCursorX;
    m_nCursorY = restoredCursorY;
    m_bCursorOn = state.cursorOn;
    m_bCursorBlock = state.cursorBlock;
    m_bCursorVisible = state.cursorVisible;
    m_bBlinkingCursor = state.blinking;
    m_nCursorBlinkPeriodTicks = state.blinkTicks ? state.blinkTicks : 1;
    m_nNextCursorBlink = state.nextBlink ? state.nextBlink : CTimer::Get()->GetTicks();
    m_nScrollStart = state.scrollStart;
    m_nScrollEnd = state.scrollEnd;
    m_bReverseAttribute = state.reverseAttribute;
    m_bBoldAttribute = state.boldAttribute;
    m_bUnderlineAttribute = state.underlineAttribute;
    m_bBlinkAttribute = state.blinkAttribute;
    m_bInsertOn = state.insertOn;
    m_bAutoPage = state.autoPage;
    m_bDelayedUpdate = state.delayedUpdate;
    m_nLastUpdateTicks = state.lastUpdateTicks;
    m_State = static_cast<TState>(state.parserState);
    m_nParam1 = state.param1;
    m_nParam2 = state.param2;
    m_G0CharSet = static_cast<ECharacterSet>(state.g0CharSet);
    m_G1CharSet = static_cast<ECharacterSet>(state.g1CharSet);
    m_bUseG1 = state.useG1;
    m_SpinLock.Release();

    if (m_bCursorVisible && m_bCursorOn)
    {
        m_SpinLock.Acquire();
        InvertCursor();
        m_SpinLock.Release();
    }
}

size_t CTRenderer::GetBufferSize(void) const
{
    return m_nSize;
}

void CTRenderer::SaveScreenBuffer(void *buffer, size_t bufferSize)
{
    if (buffer == nullptr || m_pBuffer8 == nullptr)
    {
        return;
    }
    if (bufferSize < m_nSize)
    {
        return;
    }

    m_SpinLock.Acquire();
    memcpy(buffer, m_pBuffer8, m_nSize);
    m_SpinLock.Release();
}

void CTRenderer::RestoreScreenBuffer(const void *buffer, size_t bufferSize)
{
    if (buffer == nullptr || m_pBuffer8 == nullptr)
    {
        return;
    }
    if (bufferSize < m_nSize)
    {
        return;
    }

    m_SpinLock.Acquire();
    memcpy(m_pBuffer8, buffer, m_nSize);
    m_UpdateArea.y1 = 0;
    m_UpdateArea.y2 = m_nHeight ? (m_nHeight - 1) : 0;
    if (m_pFrameBuffer != nullptr)
    {
        CDisplay::TArea area;
        area.x1 = 0;
        area.y1 = 0;
        area.x2 = m_nWidth ? (m_nWidth - 1) : 0;
        area.y2 = m_nHeight ? (m_nHeight - 1) : 0;
        m_pFrameBuffer->SetArea(area, m_pBuffer8);
    }
    m_SpinLock.Release();
}
