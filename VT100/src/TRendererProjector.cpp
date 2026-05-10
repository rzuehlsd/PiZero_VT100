//------------------------------------------------------------------------------
// Module:        CTRendererProjector
// Description:   Projects renderer shadow state onto the framebuffer.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-05-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-05-07     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#include "TRendererProjector.h"

#include "TRenderer.h"
#include "TRendererSurface.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <string.h>

LOGMODULE("TRendererProjector");

namespace
{
    constexpr unsigned kProjectorMinimumRefreshMs = 1;
    constexpr unsigned kDefaultRefreshHz = 60;
    constexpr unsigned kSmoothScrollRefreshHz = 200;
    constexpr unsigned kSmoothScrollMaxPixelsPerFrame = 4;
}

CTRendererProjector::CTRendererProjector(CShadowBuffer &shadowBuffer,
                                         CRendererSurface &surface)
    : m_ShadowBuffer(shadowBuffer),
      m_Surface(surface),
      m_pRenderer(nullptr),
      m_nRefreshDelayMs(1000 / kDefaultRefreshHz),
      m_nLastRenderedGeneration(static_cast<unsigned>(-1)),
      m_nLastSmoothPixelOffset(0),
      m_bDeferredFullRefreshAfterSmoothScroll(FALSE),
      m_bSmoothProfileCaptureActive(FALSE),
      m_bSmoothProfileCaptureCompleted(FALSE),
      m_nSmoothProfileSampleCount(0),
      m_nSmoothProfileDroppedSamples(0)
{
    SetName("RendererProjector");
    Suspend();
}

void CTRendererProjector::ResetSmoothScrollProfileCapture(void)
{
    m_bSmoothProfileCaptureActive = TRUE;
    m_nSmoothProfileSampleCount = 0;
    m_nSmoothProfileDroppedSamples = 0;
}

void CTRendererProjector::RecordSmoothScrollProfileSample(const TSmoothScrollProfileSample &sample)
{
    if (!m_bSmoothProfileCaptureActive)
    {
        return;
    }

    if (m_nSmoothProfileSampleCount < SmoothProfileCapacity)
    {
        m_SmoothProfileSamples[m_nSmoothProfileSampleCount++] = sample;
    }
    else
    {
        ++m_nSmoothProfileDroppedSamples;
    }
}

void CTRendererProjector::DumpSmoothScrollProfileCapture(void)
{
    if (!m_bSmoothProfileCaptureActive)
    {
        return;
    }

    unsigned totalUs = 0;
    unsigned shiftUs = 0;
    unsigned redrawUs = 0;
    unsigned flushUs = 0;
    for (unsigned index = 0; index < m_nSmoothProfileSampleCount; ++index)
    {
        totalUs += m_SmoothProfileSamples[index].totalUs;
        shiftUs += m_SmoothProfileSamples[index].shiftUs;
        redrawUs += m_SmoothProfileSamples[index].redrawUs;
        flushUs += m_SmoothProfileSamples[index].flushUs;
    }

    LOGNOTE("SmoothScrollProfile: samples=%u dropped=%u total=%uus shift=%uus redraw=%uus flush=%uus",
            m_nSmoothProfileSampleCount,
            m_nSmoothProfileDroppedSamples,
            totalUs,
            shiftUs,
            redrawUs,
            flushUs);

    for (unsigned index = 0; index < m_nSmoothProfileSampleCount; ++index)
    {
        const TSmoothScrollProfileSample &sample = m_SmoothProfileSamples[index];
        LOGNOTE("SmoothScrollProfile[%u]: dir=%s mode=%s offset=%u delta=%u rows=%u total=%uus shift=%uus redraw=%uus flush=%uus",
                index,
                sample.scrollDown ? "down" : "up",
                sample.incremental ? "incremental" : "full",
                sample.pixelOffset,
                sample.deltaPixels,
                sample.regionRows,
                sample.totalUs,
                sample.shiftUs,
                sample.redrawUs,
                sample.flushUs);
    }

    m_bSmoothProfileCaptureActive = FALSE;
    m_bSmoothProfileCaptureCompleted = TRUE;
}

void CTRendererProjector::AttachRenderer(CTRenderer *pRenderer)
{
    m_pRenderer = pRenderer;
}

boolean CTRendererProjector::Initialize(unsigned refreshHz)
{
    if (refreshHz == 0)
    {
        refreshHz = kDefaultRefreshHz;
    }

    m_nRefreshDelayMs = 1000 / refreshHz;
    if (m_nRefreshDelayMs < kProjectorMinimumRefreshMs)
    {
        m_nRefreshDelayMs = kProjectorMinimumRefreshMs;
    }

    Start();
    return TRUE;
}

void CTRendererProjector::Run(void)
{
    while (!IsSuspended())
    {
        if (m_pRenderer != nullptr)
        {
            m_pRenderer->Update();
        }

        TProjectorState state = m_ShadowBuffer.GetProjectorState();
        if (state.smoothScroll.active && !m_bSmoothProfileCaptureActive && !m_bSmoothProfileCaptureCompleted)
        {
            ResetSmoothScrollProfileCapture();
        }

        const boolean blinkChanged = AdvanceBlinkState(state);
        const boolean smoothWasActive = state.smoothScroll.active;
        const boolean smoothChanged = AdvanceSmoothScrollState(state);
        const boolean needsRender = state.fullRefreshPending || state.frameGeneration != m_nLastRenderedGeneration || blinkChanged || smoothChanged;

        if (needsRender && state.width != 0 && state.height != 0)
        {
            TSmoothScrollProfileSample sample{};
            const u64 renderStartUs = CTimer::GetClockTicks64();
            const unsigned rowCount = GetRowCount(state);
            const unsigned cellHeight = GetBaseCharHeight(state);
            const boolean smoothOnlyRender = !state.fullRefreshPending && smoothWasActive && rowCount != 0 && cellHeight != 0;
            sample.incremental = smoothOnlyRender;
            sample.scrollDown = state.smoothScroll.scrollDown;
            sample.pixelOffset = state.smoothScroll.pixelOffset;

            if (smoothOnlyRender && blinkChanged)
            {
                m_bDeferredFullRefreshAfterSmoothScroll = TRUE;
            }

            if (smoothOnlyRender)
            {
                unsigned smoothStartRow = state.smoothScroll.startRow < rowCount ? state.smoothScroll.startRow : (rowCount - 1);
                unsigned smoothEndRow = state.smoothScroll.endRow < rowCount ? state.smoothScroll.endRow : (rowCount - 1);
                sample.regionRows = smoothEndRow - smoothStartRow + 1;
                const unsigned regionStartY = smoothStartRow * cellHeight;
                const unsigned regionEndY = ((smoothEndRow + 1) * cellHeight <= state.height)
                                                ? ((smoothEndRow + 1) * cellHeight)
                                                : state.height;
                boolean cursorInRegion = FALSE;
                unsigned cursorRow = 0;

                if (state.cursorOn && state.cursorVisible)
                {
                    cursorRow = state.cursorY / cellHeight;
                    if (cursorRow >= rowCount)
                    {
                        cursorRow = rowCount - 1;
                    }

                    cursorInRegion = cursorRow >= smoothStartRow && cursorRow <= smoothEndRow;
                }

                const boolean hasSmoothSnapshot = m_ShadowBuffer.HasSmoothScrollSnapshot() && m_ShadowBuffer.GetSmoothScrollSnapshotAltScreen() == state.altScreenActive;

                if (hasSmoothSnapshot && state.smoothScroll.active && state.smoothScroll.pixelOffset > m_nLastSmoothPixelOffset)
                {
                    const unsigned delta = state.smoothScroll.pixelOffset - m_nLastSmoothPixelOffset;
                    sample.deltaPixels = delta;

                    if (state.smoothScroll.scrollDown)
                    {
                        const u64 shiftStartUs = CTimer::GetClockTicks64();
                        m_Surface.ScrollRowsDown(regionStartY, regionEndY, delta);
                        sample.shiftUs = static_cast<unsigned>(CTimer::GetClockTicks64() - shiftStartUs);
                        unsigned redrawEndY = regionStartY + delta + cellHeight;
                        if (redrawEndY > regionEndY)
                        {
                            redrawEndY = regionEndY;
                        }
                        const u64 redrawStartUs = CTimer::GetClockTicks64();
                        RenderSmoothScrollRegionBand(state, regionStartY, redrawEndY);
                        sample.redrawUs = static_cast<unsigned>(CTimer::GetClockTicks64() - redrawStartUs);
                    }
                    else
                    {
                        const u64 shiftStartUs = CTimer::GetClockTicks64();
                        m_Surface.ScrollRowsUp(regionStartY, regionEndY, delta);
                        sample.shiftUs = static_cast<unsigned>(CTimer::GetClockTicks64() - shiftStartUs);
                        unsigned redrawStartY = regionEndY > (delta + cellHeight) ? regionEndY - (delta + cellHeight) : regionStartY;
                        if (redrawStartY < regionStartY)
                        {
                            redrawStartY = regionStartY;
                        }
                        const u64 redrawStartUs = CTimer::GetClockTicks64();
                        RenderSmoothScrollRegionBand(state, redrawStartY, regionEndY);
                        sample.redrawUs = static_cast<unsigned>(CTimer::GetClockTicks64() - redrawStartUs);
                    }
                }
                else if (hasSmoothSnapshot && !state.smoothScroll.active && m_nLastSmoothPixelOffset != 0)
                {
                    const unsigned delta = cellHeight > m_nLastSmoothPixelOffset ? (cellHeight - m_nLastSmoothPixelOffset) : 0;
                    sample.deltaPixels = delta;

                    if (delta != 0)
                    {
                        const u64 shiftStartUs = CTimer::GetClockTicks64();
                        if (state.smoothScroll.scrollDown)
                        {
                            m_Surface.ScrollRowsDown(regionStartY, regionEndY, delta);
                        }
                        else
                        {
                            m_Surface.ScrollRowsUp(regionStartY, regionEndY, delta);
                        }
                        sample.shiftUs = static_cast<unsigned>(CTimer::GetClockTicks64() - shiftStartUs);
                    }

                    const u64 redrawStartUs = CTimer::GetClockTicks64();
                    if (state.smoothScroll.scrollDown)
                    {
                        const unsigned redrawEndRow = smoothStartRow < smoothEndRow ? (smoothStartRow + 1) : smoothStartRow;
                        for (unsigned row = smoothStartRow; row <= redrawEndRow; ++row)
                        {
                            RenderShadowRow(state, row);
                        }
                    }
                    else
                    {
                        const unsigned redrawStartRow = smoothEndRow > smoothStartRow ? (smoothEndRow - 1) : smoothEndRow;
                        for (unsigned row = redrawStartRow; row <= smoothEndRow; ++row)
                        {
                            RenderShadowRow(state, row);
                        }
                    }
                    sample.redrawUs = static_cast<unsigned>(CTimer::GetClockTicks64() - redrawStartUs);
                }
                else
                {
                    const u64 redrawStartUs = CTimer::GetClockTicks64();
                    if (state.smoothScroll.active && hasSmoothSnapshot)
                    {
                        RenderSmoothScrollRegion(state);
                    }
                    else
                    {
                        for (unsigned row = smoothStartRow; row <= smoothEndRow; ++row)
                        {
                            RenderShadowRow(state, row);
                        }
                    }
                    sample.redrawUs = static_cast<unsigned>(CTimer::GetClockTicks64() - redrawStartUs);
                }

                if (cursorInRegion)
                {
                    RenderCursor(state);
                }

                CDisplay::TArea area{};
                area.x1 = 0;
                area.x2 = state.width - 1;
                area.y1 = regionStartY;
                area.y2 = regionEndY - 1;

                const u64 flushStartUs = CTimer::GetClockTicks64();
                m_Surface.FlushArea(area, m_Surface.GetBuffer() + area.y1 * m_Surface.GetPitch());
                sample.flushUs = static_cast<unsigned>(CTimer::GetClockTicks64() - flushStartUs);
            }
            else
            {
                m_bDeferredFullRefreshAfterSmoothScroll = FALSE;
                sample.regionRows = rowCount;
                const u64 redrawStartUs = CTimer::GetClockTicks64();
                RenderShadowScreen(state);
                RenderCursor(state);
                sample.redrawUs = static_cast<unsigned>(CTimer::GetClockTicks64() - redrawStartUs);

                CDisplay::TArea area{};
                area.x1 = 0;
                area.x2 = state.width - 1;
                area.y1 = 0;
                area.y2 = state.height - 1;
                const u64 flushStartUs = CTimer::GetClockTicks64();
                m_Surface.FlushArea(area, m_Surface.GetBuffer());
                sample.flushUs = static_cast<unsigned>(CTimer::GetClockTicks64() - flushStartUs);
            }

            sample.totalUs = static_cast<unsigned>(CTimer::GetClockTicks64() - renderStartUs);
            if (m_bSmoothProfileCaptureActive && (smoothWasActive || state.smoothScroll.active))
            {
                RecordSmoothScrollProfileSample(sample);
            }

            m_nLastSmoothPixelOffset = state.smoothScroll.active ? state.smoothScroll.pixelOffset : 0;

            const boolean keepFullRefreshPending = smoothWasActive && !state.smoothScroll.active && m_bDeferredFullRefreshAfterSmoothScroll;
            if (keepFullRefreshPending)
            {
                state.fullRefreshPending = TRUE;
                m_bDeferredFullRefreshAfterSmoothScroll = FALSE;
            }
            else
            {
                state.fullRefreshPending = FALSE;
            }
            m_nLastRenderedGeneration = state.frameGeneration;
            m_ShadowBuffer.SetProjectorState(state);
            if (smoothWasActive && !state.smoothScroll.active)
            {
                m_ShadowBuffer.ClearSmoothScrollSnapshot();
                DumpSmoothScrollProfileCapture();
            }
        }

        unsigned sleepMs = m_nRefreshDelayMs;
        if (state.smoothScroll.active)
        {
            sleepMs = 1000 / kSmoothScrollRefreshHz;
            if (sleepMs < kProjectorMinimumRefreshMs)
            {
                sleepMs = kProjectorMinimumRefreshMs;
            }
        }

        CScheduler::Get()->MsSleep(sleepMs);
    }
}

const CCharGenerator *CTRendererProjector::GetCharGeneratorForCell(const TProjectorState &state,
                                                                   unsigned charSet,
                                                                   ELineAttribute attribute) const
{
    const boolean useGraphics = charSet != 0;
    const CCharGenerator *charGen = useGraphics && state.graphicsCharGen != nullptr
                                        ? state.graphicsCharGen
                                        : state.charGen;

    if (attribute == CShadowBuffer::LineAttributeDoubleHeightTop ||
        attribute == CShadowBuffer::LineAttributeDoubleHeightBottom)
    {
        if (useGraphics && state.graphicsDoubleBothCharGen != nullptr)
        {
            charGen = state.graphicsDoubleBothCharGen;
        }
        else if (state.doubleBothCharGen != nullptr)
        {
            charGen = state.doubleBothCharGen;
        }
    }

    return charGen;
}

CTRendererProjector::TProjectedCellStyle CTRendererProjector::GetProjectedCellStyle(const TProjectorState &state,
                                                                                    const TShadowCell &cell,
                                                                                    ELineAttribute attribute) const
{
    TProjectedCellStyle style{};
    style.charGen = GetCharGeneratorForCell(state, cell.charSet, attribute);
    style.foreground = cell.reverseVideo
                           ? AdjustBrightness565(cell.foreground, state.reverseForegroundScaleFactor)
                           : cell.foreground;
    style.background = cell.reverseVideo
                           ? AdjustBrightness565(cell.foreground, state.reverseBackgroundScaleFactor)
                           : cell.background;
    style.bold = cell.bold;
    style.dim = cell.dim;
    style.underline = (cell.blink && !state.textBlinkVisible) ? FALSE : cell.underline;
    return style;
}

unsigned CTRendererProjector::GetBaseCharWidth(const TProjectorState &state) const
{
    return state.charGen ? state.charGen->GetCharWidth() : 0;
}

unsigned CTRendererProjector::GetBaseCharHeight(const TProjectorState &state) const
{
    return state.charGen ? state.charGen->GetCharHeight() : 0;
}

unsigned CTRendererProjector::GetRowCount(const TProjectorState &state) const
{
    const unsigned charHeight = GetBaseCharHeight(state);
    if (charHeight == 0)
    {
        return 0;
    }

    unsigned rows = state.usedHeight / charHeight;
    if (rows > CShadowBuffer::MaxTextRows)
    {
        rows = CShadowBuffer::MaxTextRows;
    }

    return rows;
}

unsigned CTRendererProjector::GetCharCellWidthForLineAttribute(const TProjectorState &state,
                                                               ELineAttribute attribute) const
{
    const unsigned baseCharWidth = GetBaseCharWidth(state);
    if (baseCharWidth == 0)
    {
        return 0;
    }

    if (attribute == CShadowBuffer::LineAttributeDoubleWidth || attribute == CShadowBuffer::LineAttributeDoubleHeightTop || attribute == CShadowBuffer::LineAttributeDoubleHeightBottom)
    {
        return baseCharWidth * 2;
    }

    return baseCharWidth;
}

unsigned CTRendererProjector::GetColumnsForLineAttribute(const TProjectorState &state,
                                                         ELineAttribute attribute) const
{
    const unsigned charWidth = GetCharCellWidthForLineAttribute(state, attribute);
    if (charWidth == 0)
    {
        return 0;
    }

    return state.usedWidth / charWidth;
}

boolean CTRendererProjector::SampleGlyphPixel(const CCharGenerator &charGen,
                                              ELineAttribute attribute,
                                              unsigned nPosX,
                                              unsigned nPosY,
                                              unsigned baseCharHeight,
                                              char chChar) const
{
    const unsigned baseCharWidth = charGen.GetCharWidth();
    const unsigned glyphHeight = charGen.GetCharHeight();
    if (baseCharWidth == 0 || glyphHeight == 0)
    {
        return FALSE;
    }

    unsigned sourceX = nPosX;
    unsigned sourceY = nPosY;

    if (attribute == CShadowBuffer::LineAttributeDoubleHeightTop || attribute == CShadowBuffer::LineAttributeDoubleHeightBottom)
    {
        if (baseCharHeight == 0 || glyphHeight < baseCharHeight)
        {
            return FALSE;
        }

        if (attribute == CShadowBuffer::LineAttributeDoubleHeightBottom)
        {
            sourceY = nPosY + baseCharHeight;
        }
    }
    else if (attribute == CShadowBuffer::LineAttributeDoubleWidth)
    {
        sourceX >>= 1;
    }

    if (sourceX >= baseCharWidth || sourceY >= glyphHeight)
    {
        return FALSE;
    }

    return charGen.GetPixel(chChar, sourceX, sourceY);
}

void CTRendererProjector::FillPixelRows(unsigned, unsigned, CDisplay::TRawColor)
{
    RequestRefresh();
}

void CTRendererProjector::ScrollPixelRowsUp(unsigned, unsigned, unsigned)
{
    RequestRefresh();
}

void CTRendererProjector::ClearUnusedBottomArea(CDisplay::TRawColor)
{
    RequestRefresh();
}

void CTRendererProjector::RenderShadowCell(unsigned, unsigned)
{
    RequestRefresh();
}

void CTRendererProjector::RenderShadowRow(unsigned)
{
    RequestRefresh();
}

void CTRendererProjector::RenderShadowScreen(void)
{
    RequestRefresh();
}

void CTRendererProjector::DisplayChar(char, unsigned, unsigned, CDisplay::TRawColor)
{
    RequestRefresh();
}

void CTRendererProjector::EraseChar(unsigned, unsigned)
{
    RequestRefresh();
}

void CTRendererProjector::InvertCursor(void)
{
    RequestRefresh();
}

CDisplay::TRawColor CTRendererProjector::AdjustBrightness565(CDisplay::TRawColor color, float factor) const
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

CDisplay::TRawColor CTRendererProjector::ApplyProjectedGlyphBrightness(const TProjectorState &state,
                                                                       CDisplay::TRawColor color,
                                                                       boolean bold,
                                                                       boolean dim) const
{
    if (bold)
    {
        return AdjustBrightness565(color, state.boldScaleFactor);
    }

    if (dim)
    {
        return AdjustBrightness565(color, state.dimScaleFactor);
    }

    return color;
}

boolean CTRendererProjector::ShadowHasBlinkCells(boolean altScreenActive, unsigned rowCount) const
{
    for (unsigned row = 0; row < rowCount; ++row)
    {
        if (m_ShadowBuffer.RowHasBlink(altScreenActive, row, rowCount))
        {
            return TRUE;
        }
    }

    return FALSE;
}

boolean CTRendererProjector::AdvanceBlinkState(TProjectorState &state)
{
    const unsigned rowCount = GetRowCount(state);
    const boolean hasBlinkingText = ShadowHasBlinkCells(state.altScreenActive, rowCount);
    const boolean cursorCanBlink = state.cursorOn && state.blinkingCursor;
    if (!cursorCanBlink && !hasBlinkingText)
    {
        if (state.cursorOn && !state.blinkingCursor && !state.cursorVisible)
        {
            state.cursorVisible = TRUE;
            return TRUE;
        }
        return FALSE;
    }

    const unsigned currentTicks = CTimer::Get()->GetTicks();
    if ((int)(currentTicks - state.nextCursorBlink) < 0)
    {
        return FALSE;
    }

    if (hasBlinkingText)
    {
        state.textBlinkVisible = !state.textBlinkVisible;
    }

    if (cursorCanBlink)
    {
        state.cursorVisible = !state.cursorVisible;
    }
    else if (state.cursorOn)
    {
        state.cursorVisible = TRUE;
    }

    const unsigned blinkTicks = state.cursorBlinkPeriodTicks ? state.cursorBlinkPeriodTicks : 1;
    state.nextCursorBlink = currentTicks + blinkTicks;
    return TRUE;
}

boolean CTRendererProjector::AdvanceSmoothScrollState(TProjectorState &state)
{
    if (!state.smoothScroll.active)
    {
        return FALSE;
    }

    const unsigned cellHeight = GetBaseCharHeight(state);
    if (cellHeight < 2 || !m_ShadowBuffer.HasSmoothScrollSnapshot())
    {
        state.smoothScroll.active = FALSE;
        state.smoothScroll.pixelOffset = 0;
        return TRUE;
    }

    const u64 currentTimeUs = CTimer::GetClockTicks64();
    if (state.smoothScroll.startTimeUs == 0)
    {
        state.smoothScroll.startTimeUs = currentTimeUs;
        return FALSE;
    }

    const unsigned lineDurationUs = state.smoothScroll.lineDurationUs != 0 ? state.smoothScroll.lineDurationUs : 1U;
    const u64 elapsedUs = currentTimeUs - state.smoothScroll.startTimeUs;
    const unsigned maxOffset = cellHeight - 1;
    if (elapsedUs >= lineDurationUs && state.smoothScroll.pixelOffset >= maxOffset)
    {
        state.smoothScroll.active = FALSE;
        state.smoothScroll.pixelOffset = 0;
        return TRUE;
    }

    unsigned targetOffset = maxOffset;
    if (elapsedUs < lineDurationUs)
    {
        targetOffset = 1;
        if (maxOffset > 1)
        {
            targetOffset += static_cast<unsigned>((elapsedUs * maxOffset) / lineDurationUs);
            if (targetOffset > maxOffset)
            {
                targetOffset = maxOffset;
            }
        }
    }

    if (targetOffset <= state.smoothScroll.pixelOffset)
    {
        return FALSE;
    }

    unsigned newOffset = state.smoothScroll.pixelOffset + kSmoothScrollMaxPixelsPerFrame;
    if (newOffset > targetOffset)
    {
        newOffset = targetOffset;
    }
    if (newOffset > maxOffset)
    {
        newOffset = maxOffset;
    }

    state.smoothScroll.pixelOffset = newOffset;

    return TRUE;
}

void CTRendererProjector::DisplayChar(char chChar,
                                      unsigned nPosX,
                                      unsigned nPosY,
                                      const TProjectorState &state,
                                      ELineAttribute attribute,
                                      const TProjectedCellStyle &style)
{
    const CCharGenerator *charGen = style.charGen;
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    const unsigned cellHeight = GetBaseCharHeight(state);

    if (charGen == nullptr || cellWidth == 0 || cellHeight == 0)
    {
        return;
    }

    CDisplay::TRawColor glyphColor = style.foreground;
    if (glyphColor != style.background)
    {
        glyphColor = ApplyProjectedGlyphBrightness(state, glyphColor, style.bold, style.dim);
    }

    for (unsigned y = 0; y < cellHeight; ++y)
    {
        for (unsigned x = 0; x < cellWidth; ++x)
        {
            const boolean isGlyphPixel = SampleGlyphPixel(*charGen,
                                                          attribute,
                                                          x,
                                                          y,
                                                          cellHeight,
                                                          chChar);
            m_Surface.SetRawPixel(nPosX + x, nPosY + y, isGlyphPixel ? glyphColor : style.background);
        }
    }

    if (style.bold)
    {
        for (unsigned y = 0; y < cellHeight; ++y)
        {
            for (unsigned x = 1; x < cellWidth; ++x)
            {
                if (SampleGlyphPixel(*charGen, attribute, x - 1, y, cellHeight, chChar))
                {
                    m_Surface.SetRawPixel(nPosX + x, nPosY + y, glyphColor);
                }
            }
        }
    }

    if (style.underline)
    {
        unsigned underlineRow = charGen->GetUnderline();
        if (underlineRow >= cellHeight)
        {
            underlineRow = cellHeight - 1;
        }

        for (unsigned x = 0; x < cellWidth; ++x)
        {
            m_Surface.SetRawPixel(nPosX + x, nPosY + underlineRow, glyphColor);
        }
    }
}

void CTRendererProjector::DisplayCharSlice(char chChar,
                                           unsigned nPosX,
                                           unsigned nPosY,
                                           const TProjectorState &state,
                                           ELineAttribute attribute,
                                           const TProjectedCellStyle &style,
                                           unsigned sourcePixelY,
                                           unsigned destinationPixelY,
                                           unsigned sliceHeight)
{
    const CCharGenerator *charGen = style.charGen;
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (charGen == nullptr || cellWidth == 0 || cellHeight == 0 || sliceHeight == 0 || sourcePixelY >= cellHeight)
    {
        return;
    }

    if (sourcePixelY + sliceHeight > cellHeight)
    {
        sliceHeight = cellHeight - sourcePixelY;
    }

    CDisplay::TRawColor glyphColor = style.foreground;
    if (glyphColor != style.background)
    {
        glyphColor = ApplyProjectedGlyphBrightness(state, glyphColor, style.bold, style.dim);
    }

    for (unsigned y = 0; y < sliceHeight; ++y)
    {
        const unsigned glyphY = sourcePixelY + y;
        const unsigned drawY = nPosY + destinationPixelY + y;
        for (unsigned x = 0; x < cellWidth; ++x)
        {
            const boolean isGlyphPixel = SampleGlyphPixel(*charGen, attribute, x, glyphY, cellHeight, chChar);
            m_Surface.SetRawPixel(nPosX + x, drawY, isGlyphPixel ? glyphColor : style.background);
        }
    }

    if (style.bold)
    {
        for (unsigned y = 0; y < sliceHeight; ++y)
        {
            const unsigned glyphY = sourcePixelY + y;
            const unsigned drawY = nPosY + destinationPixelY + y;
            for (unsigned x = 1; x < cellWidth; ++x)
            {
                if (SampleGlyphPixel(*charGen, attribute, x - 1, glyphY, cellHeight, chChar))
                {
                    m_Surface.SetRawPixel(nPosX + x, drawY, glyphColor);
                }
            }
        }
    }

    if (style.underline)
    {
        unsigned underlineRow = charGen->GetUnderline();
        if (underlineRow >= cellHeight)
        {
            underlineRow = cellHeight - 1;
        }

        if (underlineRow >= sourcePixelY && underlineRow < sourcePixelY + sliceHeight)
        {
            const unsigned drawY = nPosY + destinationPixelY + (underlineRow - sourcePixelY);
            for (unsigned x = 0; x < cellWidth; ++x)
            {
                m_Surface.SetRawPixel(nPosX + x, drawY, glyphColor);
            }
        }
    }
}

void CTRendererProjector::ClearUnusedBottomArea(const TProjectorState &state, CDisplay::TRawColor background)
{
    if (state.usedHeight >= state.height)
    {
        return;
    }

    m_Surface.FillRows(state.usedHeight, state.height, background);
}

void CTRendererProjector::RenderShadowCell(const TProjectorState &state, unsigned row, unsigned column)
{
    const unsigned rowCount = GetRowCount(state);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (row >= rowCount || column >= CShadowBuffer::MaxTextColumns || cellHeight == 0)
    {
        return;
    }

    const ELineAttribute attribute = m_ShadowBuffer.GetLineAttribute(row, rowCount);
    const unsigned visibleColumns = GetColumnsForLineAttribute(state, attribute);
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    if (column >= visibleColumns || cellWidth == 0)
    {
        return;
    }

    const unsigned nPosY = row * cellHeight;
    const unsigned nPosX = column * cellWidth;
    if (nPosX >= state.width || nPosY >= state.height)
    {
        return;
    }

    const TShadowCell(*cells)[CShadowBuffer::MaxTextColumns] = m_ShadowBuffer.GetActiveCells(state.altScreenActive);
    const TShadowCell &cell = cells[row][column];
    const TProjectedCellStyle style = GetProjectedCellStyle(state, cell, attribute);
    const char renderChar = (cell.blink && !state.textBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
    DisplayChar(renderChar, nPosX, nPosY, state, attribute, style);
}

void CTRendererProjector::RenderShadowRow(const TProjectorState &state, unsigned row)
{
    const unsigned rowCount = GetRowCount(state);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (row >= rowCount || cellHeight == 0)
    {
        return;
    }

    const ELineAttribute attribute = m_ShadowBuffer.GetLineAttribute(row, rowCount);
    const unsigned visibleColumns = GetColumnsForLineAttribute(state, attribute);
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    const unsigned nPosY = row * cellHeight;
    if (visibleColumns == 0 || cellWidth == 0 || nPosY >= state.height)
    {
        return;
    }

    m_Surface.FillRows(nPosY, nPosY + cellHeight, state.background);

    const TShadowCell(*cells)[CShadowBuffer::MaxTextColumns] = m_ShadowBuffer.GetActiveCells(state.altScreenActive);
    for (unsigned column = 0; column < visibleColumns && column < CShadowBuffer::MaxTextColumns; ++column)
    {
        const unsigned nPosX = column * cellWidth;
        if (nPosX >= state.width)
        {
            break;
        }

        const TShadowCell &cell = cells[row][column];
        const TProjectedCellStyle style = GetProjectedCellStyle(state, cell, attribute);
        const char renderChar = (cell.blink && !state.textBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
        DisplayChar(renderChar, nPosX, nPosY, state, attribute, style);
    }
}

void CTRendererProjector::RenderShadowScreen(const TProjectorState &state)
{
    const unsigned rowCount = GetRowCount(state);
    if (rowCount == 0)
    {
        ClearUnusedBottomArea(state, state.background);
        return;
    }

    unsigned smoothStartRow = 0;
    unsigned smoothEndRow = 0;
    const boolean smoothActive = state.smoothScroll.active && m_ShadowBuffer.HasSmoothScrollSnapshot() && m_ShadowBuffer.GetSmoothScrollSnapshotAltScreen() == state.altScreenActive;
    if (smoothActive)
    {
        smoothStartRow = state.smoothScroll.startRow < rowCount ? state.smoothScroll.startRow : (rowCount - 1);
        smoothEndRow = state.smoothScroll.endRow < rowCount ? state.smoothScroll.endRow : (rowCount - 1);
    }

    for (unsigned row = 0; row < rowCount; ++row)
    {
        if (smoothActive && row >= smoothStartRow && row <= smoothEndRow)
        {
            continue;
        }

        RenderShadowRow(state, row);
    }

    if (smoothActive)
    {
        RenderSmoothScrollRegion(state);
    }

    ClearUnusedBottomArea(state, state.background);
}

void CTRendererProjector::RenderShadowRowSlice(const TProjectorState &state,
                                               unsigned sourceRow,
                                               unsigned destinationRow,
                                               unsigned sourcePixelY,
                                               unsigned destinationPixelY,
                                               unsigned sliceHeight,
                                               boolean useSnapshot)
{
    const unsigned rowCount = GetRowCount(state);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (sourceRow >= rowCount || destinationRow >= rowCount || cellHeight == 0 || sliceHeight == 0)
    {
        return;
    }

    const ELineAttribute attribute = useSnapshot
                                         ? m_ShadowBuffer.GetSmoothScrollSnapshotLineAttribute(sourceRow, rowCount)
                                         : m_ShadowBuffer.GetLineAttribute(sourceRow, rowCount);
    const unsigned visibleColumns = GetColumnsForLineAttribute(state, attribute);
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    const unsigned nPosY = destinationRow * cellHeight;
    if (visibleColumns == 0 || cellWidth == 0 || nPosY >= state.height)
    {
        return;
    }

    const TShadowCell(*cells)[CShadowBuffer::MaxTextColumns] = useSnapshot
                                                                   ? m_ShadowBuffer.GetSmoothScrollSnapshotCells()
                                                                   : m_ShadowBuffer.GetActiveCells(state.altScreenActive);
    for (unsigned column = 0; column < visibleColumns && column < CShadowBuffer::MaxTextColumns; ++column)
    {
        const unsigned nPosX = column * cellWidth;
        if (nPosX >= state.width)
        {
            break;
        }

        const TShadowCell &cell = cells[sourceRow][column];
        const TProjectedCellStyle style = GetProjectedCellStyle(state, cell, attribute);
        const char renderChar = (cell.blink && !state.textBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
        DisplayCharSlice(renderChar,
                         nPosX,
                         nPosY,
                         state,
                         attribute,
                         style,
                         sourcePixelY,
                         destinationPixelY,
                         sliceHeight);
    }
}

void CTRendererProjector::RenderSmoothScrollRegion(const TProjectorState &state)
{
    if (!m_ShadowBuffer.HasSmoothScrollSnapshot() || m_ShadowBuffer.GetSmoothScrollSnapshotAltScreen() != state.altScreenActive)
    {
        return;
    }

    const unsigned rowCount = GetRowCount(state);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (rowCount == 0 || cellHeight < 2)
    {
        return;
    }

    unsigned startRow = state.smoothScroll.startRow;
    unsigned endRow = state.smoothScroll.endRow;
    if (startRow >= rowCount)
    {
        startRow = rowCount - 1;
    }
    if (endRow >= rowCount)
    {
        endRow = rowCount - 1;
    }
    if (startRow > endRow)
    {
        return;
    }

    unsigned offset = state.smoothScroll.pixelOffset;
    if (offset == 0 || offset >= cellHeight)
    {
        return;
    }

    for (unsigned destinationRow = startRow; destinationRow <= endRow; ++destinationRow)
    {
        const unsigned rowTop = destinationRow * cellHeight;
        m_Surface.FillRows(rowTop, rowTop + cellHeight, state.background);

        if (state.smoothScroll.scrollDown)
        {
            if (destinationRow == startRow)
            {
                RenderShadowRowSlice(state, destinationRow, destinationRow, 0, 0, offset, FALSE);
            }
            else
            {
                RenderShadowRowSlice(state, destinationRow - 1, destinationRow, cellHeight - offset, 0, offset, TRUE);
            }

            RenderShadowRowSlice(state, destinationRow, destinationRow, 0, offset, cellHeight - offset, TRUE);
        }
        else
        {
            RenderShadowRowSlice(state, destinationRow, destinationRow, offset, 0, cellHeight - offset, TRUE);

            const boolean useSnapshot = destinationRow < endRow;
            const unsigned sourceRow = useSnapshot ? (destinationRow + 1) : destinationRow;
            RenderShadowRowSlice(state,
                                 sourceRow,
                                 destinationRow,
                                 0,
                                 cellHeight - offset,
                                 offset,
                                 useSnapshot ? TRUE : FALSE);
        }
    }
}

void CTRendererProjector::RenderSmoothScrollRegionBand(const TProjectorState &state,
                                                       unsigned startY,
                                                       unsigned endY)
{
    if (!m_ShadowBuffer.HasSmoothScrollSnapshot() || m_ShadowBuffer.GetSmoothScrollSnapshotAltScreen() != state.altScreenActive)
    {
        return;
    }

    const unsigned rowCount = GetRowCount(state);
    const unsigned cellHeight = GetBaseCharHeight(state);
    if (rowCount == 0 || cellHeight < 2 || startY >= endY || startY >= state.height)
    {
        return;
    }

    if (endY > state.height)
    {
        endY = state.height;
    }

    unsigned startRow = state.smoothScroll.startRow;
    unsigned endRow = state.smoothScroll.endRow;
    if (startRow >= rowCount)
    {
        startRow = rowCount - 1;
    }
    if (endRow >= rowCount)
    {
        endRow = rowCount - 1;
    }
    if (startRow > endRow)
    {
        return;
    }

    const unsigned regionStartY = startRow * cellHeight;
    const unsigned regionEndY = ((endRow + 1) * cellHeight <= state.height)
                                    ? ((endRow + 1) * cellHeight)
                                    : state.height;
    if (startY < regionStartY)
    {
        startY = regionStartY;
    }
    if (endY > regionEndY)
    {
        endY = regionEndY;
    }
    if (startY >= endY)
    {
        return;
    }

    m_Surface.FillRows(startY, endY, state.background);

    const unsigned offset = state.smoothScroll.pixelOffset;
    if (offset == 0 || offset >= cellHeight)
    {
        return;
    }

    auto renderClippedSlice = [&](unsigned sourceRow,
                                  unsigned destinationRow,
                                  unsigned sourcePixelY,
                                  unsigned destinationPixelY,
                                  unsigned sliceHeight,
                                  boolean useSnapshot)
    {
        const unsigned rowTop = destinationRow * cellHeight;
        const unsigned sliceStartY = rowTop + destinationPixelY;
        const unsigned sliceEndY = sliceStartY + sliceHeight;
        if (sliceHeight == 0 || sliceEndY <= startY || sliceStartY >= endY)
        {
            return;
        }

        const unsigned clippedStartY = sliceStartY < startY ? startY : sliceStartY;
        const unsigned clippedEndY = sliceEndY > endY ? endY : sliceEndY;
        const unsigned clipOffset = clippedStartY - sliceStartY;
        RenderShadowRowSlice(state,
                             sourceRow,
                             destinationRow,
                             sourcePixelY + clipOffset,
                             destinationPixelY + clipOffset,
                             clippedEndY - clippedStartY,
                             useSnapshot);
    };

    for (unsigned destinationRow = startRow; destinationRow <= endRow; ++destinationRow)
    {
        if (state.smoothScroll.scrollDown)
        {
            if (destinationRow == startRow)
            {
                renderClippedSlice(destinationRow, destinationRow, 0, 0, offset, FALSE);
            }
            else
            {
                renderClippedSlice(destinationRow - 1, destinationRow, cellHeight - offset, 0, offset, TRUE);
            }

            renderClippedSlice(destinationRow, destinationRow, 0, offset, cellHeight - offset, TRUE);
        }
        else
        {
            renderClippedSlice(destinationRow, destinationRow, offset, 0, cellHeight - offset, TRUE);

            const boolean useSnapshot = destinationRow < endRow;
            const unsigned sourceRow = useSnapshot ? (destinationRow + 1) : destinationRow;
            renderClippedSlice(sourceRow,
                               destinationRow,
                               0,
                               cellHeight - offset,
                               offset,
                               useSnapshot ? TRUE : FALSE);
        }
    }
}

void CTRendererProjector::RenderCursor(const TProjectorState &state)
{
    if (!state.cursorOn || !state.cursorVisible)
    {
        return;
    }

    const unsigned cellHeight = GetBaseCharHeight(state);
    const unsigned rowCount = GetRowCount(state);
    if (cellHeight == 0 || rowCount == 0)
    {
        return;
    }

    unsigned cursorRow = state.cursorY / cellHeight;
    if (cursorRow >= rowCount)
    {
        cursorRow = rowCount - 1;
    }

    const ELineAttribute attribute = m_ShadowBuffer.GetLineAttribute(cursorRow, rowCount);
    const unsigned cellWidth = GetCharCellWidthForLineAttribute(state, attribute);
    const unsigned visibleColumns = GetColumnsForLineAttribute(state, attribute);
    if (cellWidth == 0 || visibleColumns == 0)
    {
        return;
    }

    unsigned cursorColumn = state.cursorX / cellWidth;
    if (cursorColumn >= visibleColumns)
    {
        cursorColumn = visibleColumns - 1;
    }

    const unsigned nPosX = cursorColumn * cellWidth;
    const unsigned nPosY = cursorRow * cellHeight;
    const TShadowCell(*cells)[CShadowBuffer::MaxTextColumns] = m_ShadowBuffer.GetActiveCells(state.altScreenActive);
    const TShadowCell &cell = cells[cursorRow][cursorColumn];

    if (state.cursorBlock)
    {
        TProjectedCellStyle style = GetProjectedCellStyle(state, cell, attribute);
        const CDisplay::TRawColor savedForeground = style.foreground;
        style.foreground = style.background;
        style.background = savedForeground;
        const char renderChar = (cell.blink && !state.textBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
        DisplayChar(renderChar, nPosX, nPosY, state, attribute, style);
        return;
    }

    const CCharGenerator *charGen = GetCharGeneratorForCell(state, cell.charSet, attribute);
    unsigned underlineRow = (charGen != nullptr) ? charGen->GetUnderline() : (cellHeight - 1);
    if (underlineRow >= cellHeight)
    {
        underlineRow = cellHeight - 1;
    }

    for (unsigned x = 0; x < cellWidth; ++x)
    {
        m_Surface.SetRawPixel(nPosX + x, nPosY + underlineRow, state.foreground);
    }
}

void CTRendererProjector::RequestRefresh(void)
{
    m_ShadowBuffer.MarkFullRefresh();
}

void CTRenderer::SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }

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

CDisplay::TRawColor CTRenderer::GetRawPixel(unsigned nPosX, unsigned nPosY)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return m_BackgroundColor;
    }

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

CDisplay::TColor CTRenderer::AdjustBrightness(CDisplay::TColor color, float factor) const
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

CDisplay::TRawColor CTRenderer::AdjustBrightness565(CDisplay::TRawColor color, float factor) const
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

void CTRenderer::FillPixelRows(unsigned startY, unsigned endY, CDisplay::TRawColor color)
{
    m_pProjector->FillPixelRows(startY, endY, color);
}

void CTRenderer::ScrollPixelRowsUp(unsigned startY, unsigned endY, unsigned deltaY)
{
    m_pProjector->ScrollPixelRowsUp(startY, endY, deltaY);
}

void CTRenderer::ClearUnusedBottomArea(CDisplay::TRawColor background)
{
    m_pProjector->ClearUnusedBottomArea(background);
}

void CTRenderer::RenderShadowCell(unsigned row, unsigned column)
{
    m_pProjector->RenderShadowCell(row, column);
}

void CTRenderer::RenderShadowRow(unsigned row)
{
    m_pProjector->RenderShadowRow(row);
}

void CTRenderer::RenderShadowScreen(void)
{
    m_pProjector->RenderShadowScreen();
}

void CTRenderer::DisplayChar(char chChar, unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    m_pProjector->DisplayChar(chChar, nPosX, nPosY, nColor);
}

void CTRenderer::EraseChar(unsigned nPosX, unsigned nPosY)
{
    m_pProjector->EraseChar(nPosX, nPosY);
}

void CTRenderer::InvertCursor(void)
{
    m_pProjector->InvertCursor();
}