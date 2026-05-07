//------------------------------------------------------------------------------
// Module:        CRendererProjector
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

#include <string.h>

CRendererProjector::CRendererProjector(CTRenderer &renderer,
                                                                             CShadowBuffer &shadowBuffer,
                                                                             CRendererSurface &surface)
        : m_Renderer(renderer),
            m_ShadowBuffer(shadowBuffer),
            m_Surface(surface)
{
}

const CCharGenerator *CRendererProjector::GetCharGeneratorForCell(unsigned charSet, ELineAttribute attribute) const
{
    return m_Renderer.GetProjectorCharGenerator(charSet,
                                                static_cast<CTRenderer::ELineAttribute>(attribute));
}

CRendererProjector::TProjectedCellStyle CRendererProjector::GetProjectedCellStyle(const TShadowCell &cell,
                                                                                  ELineAttribute attribute) const
{
    TProjectedCellStyle style{};
    style.charGen = m_Renderer.GetProjectorCharGenerator(cell.charSet,
                                                         static_cast<CTRenderer::ELineAttribute>(attribute));
    style.foreground = cell.reverseVideo
                           ? m_Renderer.AdjustBrightness565(cell.foreground, m_Renderer.m_ReverseForegroundScaleFactor)
                           : cell.foreground;
    style.background = cell.reverseVideo
                           ? m_Renderer.AdjustBrightness565(cell.foreground, m_Renderer.m_ReverseBackgroundScaleFactor)
                           : cell.background;
    style.bold = cell.bold;
    style.dim = cell.dim;
    style.underline = (cell.blink && !m_Renderer.m_bTextBlinkVisible) ? FALSE : cell.underline;
    return style;
}

void CRendererProjector::FillPixelRows(unsigned startY, unsigned endY, CDisplay::TRawColor color)
{
    m_Surface.FillRows(startY, endY, color);
}

void CRendererProjector::ScrollPixelRowsUp(unsigned startY, unsigned endY, unsigned deltaY)
{
    m_Surface.ScrollRowsUp(startY, endY, deltaY);
}

void CRendererProjector::ClearUnusedBottomArea(CDisplay::TRawColor background)
{
    if (m_Renderer.m_nUsedHeight >= m_Renderer.m_nHeight)
    {
        return;
    }

    FillPixelRows(m_Renderer.m_nUsedHeight, m_Renderer.m_nHeight, background);

    m_Renderer.SetUpdateArea(m_Renderer.m_nUsedHeight, m_Renderer.m_nHeight - 1);
}

void CRendererProjector::RenderShadowCell(unsigned row, unsigned column)
{
    const unsigned rowCount = m_Renderer.GetRowCount();
    const unsigned cellHeight = m_Renderer.GetBaseCharHeight();
    if (row >= rowCount || column >= CTRenderer::MaxTextColumns || cellHeight == 0)
    {
        return;
    }

    const unsigned nPosY = row * cellHeight;
    if (nPosY >= m_Renderer.m_nHeight)
    {
        return;
    }

    const unsigned visibleColumns = m_Renderer.GetColumnsForY(nPosY);
    const unsigned cellWidth = m_Renderer.GetCharCellWidthForY(nPosY);
    if (column >= visibleColumns || cellWidth == 0)
    {
        return;
    }

    const unsigned nPosX = column * cellWidth;
    if (nPosX >= m_Renderer.m_nWidth)
    {
        return;
    }

    const TShadowCell(*cells)[CTRenderer::MaxTextColumns] =
        reinterpret_cast<const TShadowCell(*)[CTRenderer::MaxTextColumns]>(m_ShadowBuffer.GetActiveCells(m_Renderer.m_bAltScreenActive));
    const TShadowCell &cell = cells[row][column];
    const ELineAttribute attribute = m_ShadowBuffer.GetLineAttribute(row, rowCount);
    const TProjectedCellStyle style = GetProjectedCellStyle(cell, attribute);
    const char renderChar = (cell.blink && !m_Renderer.m_bTextBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
    DisplayChar(renderChar, nPosX, nPosY, attribute, style);
}

void CRendererProjector::RenderShadowRow(unsigned row)
{
    const unsigned rowCount = m_Renderer.GetRowCount();
    const unsigned cellHeight = m_Renderer.GetBaseCharHeight();
    if (row >= rowCount || cellHeight == 0)
    {
        return;
    }

    const unsigned nPosY = row * cellHeight;
    if (nPosY >= m_Renderer.m_nHeight)
    {
        return;
    }

    const unsigned visibleColumns = m_Renderer.GetColumnsForY(nPosY);
    const unsigned cellWidth = m_Renderer.GetCharCellWidthForY(nPosY);
    if (visibleColumns == 0 || cellWidth == 0)
    {
        return;
    }

    const CDisplay::TRawColor savedBackground = m_Renderer.m_BackgroundColor;

    FillPixelRows(nPosY, nPosY + cellHeight, savedBackground);

    const TShadowCell(*cells)[CTRenderer::MaxTextColumns] =
        reinterpret_cast<const TShadowCell(*)[CTRenderer::MaxTextColumns]>(m_ShadowBuffer.GetActiveCells(m_Renderer.m_bAltScreenActive));
    const ELineAttribute attribute = m_ShadowBuffer.GetLineAttribute(row, rowCount);
    for (unsigned column = 0; column < visibleColumns && column < CTRenderer::MaxTextColumns; ++column)
    {
        const TShadowCell &cell = cells[row][column];

        const unsigned nPosX = column * cellWidth;
        if (nPosX >= m_Renderer.m_nWidth)
        {
            break;
        }

        const TProjectedCellStyle style = GetProjectedCellStyle(cell, attribute);
        const char renderChar = (cell.blink && !m_Renderer.m_bTextBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
        DisplayChar(renderChar, nPosX, nPosY, attribute, style);
    }

    if (row + 1 == rowCount)
    {
        ClearUnusedBottomArea(savedBackground);
    }

    m_Renderer.SetUpdateArea(nPosY, nPosY + cellHeight - 1);
}

void CRendererProjector::RenderShadowScreen(void)
{
    const unsigned rowCount = m_Renderer.GetRowCount();
    if (rowCount == 0)
    {
        ClearUnusedBottomArea(m_Renderer.m_BackgroundColor);
        return;
    }

    for (unsigned row = 0; row < rowCount; ++row)
    {
        RenderShadowRow(row);
    }
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

const CCharGenerator *CTRenderer::GetProjectorCharGenerator(unsigned charSet, ELineAttribute attribute) const
{
    const bool useGraphics = charSet == static_cast<unsigned>(CharSetGraphics);
    const CCharGenerator *charGen = useGraphics && m_pGraphicsCharGen != nullptr
                                        ? m_pGraphicsCharGen
                                        : m_pCharGen;

    if (attribute == LineAttributeDoubleHeightTop ||
        attribute == LineAttributeDoubleHeightBottom)
    {
        if (useGraphics && m_pGraphicsDoubleBothCharGen != nullptr)
        {
            charGen = m_pGraphicsDoubleBothCharGen;
        }
        else if (m_pDoubleBothCharGen != nullptr)
        {
            charGen = m_pDoubleBothCharGen;
        }
    }

    return charGen;
}

CDisplay::TRawColor CTRenderer::ApplyProjectedGlyphBrightness(CDisplay::TRawColor color,
                                                              boolean bold,
                                                              boolean dim) const
{
    if (bold)
    {
        return AdjustBrightness565(color, m_BoldScaleFactor);
    }

    if (dim)
    {
        return AdjustBrightness565(color, m_DimScaleFactor);
    }

    return color;
}

void CRendererProjector::DisplayChar(char chChar, unsigned nPosX, unsigned nPosY,
                                     CDisplay::TRawColor nColor)
{
    const ELineAttribute attribute = static_cast<ELineAttribute>(m_Renderer.GetLineAttributeForY(nPosY));
    TProjectedCellStyle style{};
    const unsigned activeCharSet = m_Renderer.m_bUseG1
                                       ? static_cast<unsigned>(m_Renderer.m_G1CharSet)
                                       : static_cast<unsigned>(m_Renderer.m_G0CharSet);
    style.charGen = m_Renderer.GetProjectorCharGenerator(activeCharSet,
                                                         static_cast<CTRenderer::ELineAttribute>(attribute));
    style.foreground = nColor;
    style.background = m_Renderer.GetTextBackgroundColor();
    style.bold = m_Renderer.m_bBoldAttribute;
    style.dim = m_Renderer.m_bDimAttribute;
    style.underline = m_Renderer.m_bUnderlineAttribute;

    DisplayChar(chChar, nPosX, nPosY, attribute, style);
}

void CRendererProjector::DisplayChar(char chChar,
                                     unsigned nPosX,
                                     unsigned nPosY,
                                     ELineAttribute attribute,
                                     const TProjectedCellStyle &style)
{
    const CCharGenerator *charGen = style.charGen;

    const unsigned cellWidth = m_Renderer.GetCharCellWidthForLineAttribute(static_cast<CTRenderer::ELineAttribute>(attribute));
    const unsigned cellHeight = m_Renderer.GetBaseCharHeight();

    if (charGen == nullptr || cellWidth == 0 || cellHeight == 0)
    {
        return;
    }

    CDisplay::TRawColor glyphColor = style.foreground;
    if (glyphColor != style.background)
    {
        glyphColor = m_Renderer.ApplyProjectedGlyphBrightness(glyphColor, style.bold, style.dim);
    }

    for (unsigned y = 0; y < cellHeight; y++)
    {
        for (unsigned x = 0; x < cellWidth; x++)
        {
            const bool isGlyphPixel = m_Renderer.SampleGlyphPixel(*charGen,
                                                                  chChar,
                                                                  static_cast<CTRenderer::ELineAttribute>(attribute),
                                                                  x,
                                                                  y);
            const CDisplay::TRawColor pixelColor = isGlyphPixel ? glyphColor : style.background;
            m_Surface.SetRawPixel(nPosX + x, nPosY + y, pixelColor);
        }
    }

    if (style.bold)
    {
        for (unsigned y = 0; y < cellHeight; y++)
        {
            for (unsigned x = 1; x < cellWidth; x++)
            {
                if (m_Renderer.SampleGlyphPixel(*charGen,
                                                chChar,
                                                static_cast<CTRenderer::ELineAttribute>(attribute),
                                                x - 1,
                                                y))
                {
                    m_Surface.SetRawPixel(nPosX + x, nPosY + y, glyphColor);
                }
            }
        }
    }

    if (style.underline)
    {
        const unsigned underlineRow = charGen->GetUnderline();
        if (underlineRow < cellHeight)
        {
            for (unsigned x = 0; x < cellWidth; x++)
            {
                m_Surface.SetRawPixel(nPosX + x, nPosY + underlineRow, glyphColor);
            }
        }
    }

    m_Renderer.SetUpdateArea(nPosY, nPosY + cellHeight - 1);
}

void CRendererProjector::EraseChar(unsigned nPosX, unsigned nPosY)
{
    const unsigned row = m_Renderer.GetRowIndexFromY(nPosY);
    const unsigned column = m_Renderer.GetColumnIndexFromX(nPosX, nPosY);
    if (row >= CTRenderer::MaxTextRows || column >= CTRenderer::MaxTextColumns)
    {
        return;
    }

    m_Renderer.ClearShadowCells(row, column, column + 1);
    RenderShadowCell(row, column);
}

void CRendererProjector::InvertCursor(void)
{
    if (!m_Renderer.m_bCursorOn)
    {
        return;
    }

    CDisplay::TRawColor *pPixelData = m_Renderer.m_pCursorPixels;
    const unsigned cursorPosX = m_Renderer.m_nCursorX;
    const unsigned cursorPosY = m_Renderer.m_nCursorY;
    const unsigned cursorWidth = m_Renderer.GetCharCellWidthForY(cursorPosY);
    const unsigned cursorHeight = m_Renderer.GetBaseCharHeight();
    const unsigned y0 = m_Renderer.m_bCursorBlock
                            ? 0
                            : (m_Renderer.m_pCharGen != nullptr ? m_Renderer.m_pCharGen->GetUnderline() : 0);
    CDisplay::TRawColor invertMask = m_Renderer.m_ForegroundColor ^ m_Renderer.m_BackgroundColor;
    if (invertMask == 0)
    {
        switch (m_Renderer.m_nDepth)
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

    for (unsigned y = y0; y < cursorHeight; y++)
    {
        for (unsigned x = 0; x < cursorWidth; x++)
        {
            if (!m_Renderer.m_bCursorVisible)
            {
                const CDisplay::TRawColor storedPixel = m_Surface.GetRawPixel(cursorPosX + x,
                                                                              cursorPosY + y,
                                                                              m_Renderer.m_BackgroundColor);
                *pPixelData++ = storedPixel;
                m_Surface.SetRawPixel(cursorPosX + x, cursorPosY + y, storedPixel ^ invertMask);
            }
            else
            {
                m_Surface.SetRawPixel(cursorPosX + x, cursorPosY + y, *pPixelData++);
            }
        }
    }

    m_Renderer.m_bCursorVisible = !m_Renderer.m_bCursorVisible;

    m_Renderer.SetUpdateArea(cursorPosY + y0, cursorPosY + cursorHeight - 1);
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