#include "TRenderer.h"

#include <string.h>

void CTRenderer::FillPixelRows(unsigned startY, unsigned endY, CDisplay::TRawColor color)
{
    if (startY >= endY || startY >= m_nHeight)
    {
        return;
    }

    if (endY > m_nHeight)
    {
        endY = m_nHeight;
    }

    const unsigned rowCount = endY - startY;
    if (rowCount == 0)
    {
        return;
    }

    switch (m_nDepth)
    {
    case 8:
        memset(m_pBuffer8 + startY * m_nWidth, static_cast<int>(static_cast<u8>(color)), static_cast<size_t>(rowCount) * m_nWidth);
        break;

    case 16:
    {
        u16 *pRow = m_pBuffer16 + startY * m_nWidth;
        const size_t pixelCount = static_cast<size_t>(rowCount) * m_nWidth;
        for (size_t index = 0; index < pixelCount; ++index)
        {
            pRow[index] = static_cast<u16>(color);
        }
        break;
    }

    case 32:
    {
        u32 *pRow = m_pBuffer32 + startY * m_nWidth;
        const size_t pixelCount = static_cast<size_t>(rowCount) * m_nWidth;
        for (size_t index = 0; index < pixelCount; ++index)
        {
            pRow[index] = color;
        }
        break;
    }

    default:
        for (unsigned y = startY; y < endY; ++y)
        {
            for (unsigned x = 0; x < m_nWidth; ++x)
            {
                SetRawPixel(x, y, color);
            }
        }
        break;
    }
}

void CTRenderer::ScrollPixelRowsUp(unsigned startY, unsigned endY, unsigned deltaY)
{
    if (startY >= endY || deltaY == 0 || startY >= m_nHeight)
    {
        return;
    }

    if (endY > m_nHeight)
    {
        endY = m_nHeight;
    }

    if (startY + deltaY >= endY)
    {
        return;
    }

    const size_t bytesToMove = static_cast<size_t>(endY - startY - deltaY) * m_nPitch;
    memmove(m_pBuffer8 + startY * m_nPitch,
            m_pBuffer8 + (startY + deltaY) * m_nPitch,
            bytesToMove);
}

void CTRenderer::ClearUnusedBottomArea(CDisplay::TRawColor background)
{
    if (m_nUsedHeight >= m_nHeight)
    {
        return;
    }

    FillPixelRows(m_nUsedHeight, m_nHeight, background);

    SetUpdateArea(m_nUsedHeight, m_nHeight - 1);
}

void CTRenderer::RenderShadowCell(unsigned row, unsigned column)
{
    const unsigned rowCount = GetRowCount();
    const unsigned cellHeight = GetBaseCharHeight();
    if (row >= rowCount || column >= MaxTextColumns || cellHeight == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned nPosY = row * cellHeight;
    if (nPosY >= m_nHeight)
    {
        return;
    }

    const unsigned visibleColumns = GetColumnsForY(nPosY);
    const unsigned cellWidth = GetCharCellWidthForY(nPosY);
    if (column >= visibleColumns || cellWidth == 0)
    {
        return;
    }

    const unsigned nPosX = column * cellWidth;
    if (nPosX >= m_nWidth)
    {
        return;
    }

    const CDisplay::TRawColor savedForeground = m_ForegroundColor;
    const CDisplay::TRawColor savedBackground = m_BackgroundColor;
    const boolean savedBold = m_bBoldAttribute;
    const boolean savedDim = m_bDimAttribute;
    const boolean savedUnderline = m_bUnderlineAttribute;
    const boolean savedBlink = m_bBlinkAttribute;
    const boolean savedReverse = m_bReverseAttribute;
    CCharGenerator *savedCharGen = m_pCharGen;

    const TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    const TShadowCell &cell = cells[row][column];

    m_ForegroundColor = cell.foreground;
    m_BackgroundColor = cell.background;
    m_bBoldAttribute = cell.bold;
    m_bDimAttribute = cell.dim;
    m_bUnderlineAttribute = (cell.blink && !m_bTextBlinkVisible) ? FALSE : cell.underline;
    m_bBlinkAttribute = cell.blink;
    m_bReverseAttribute = cell.reverseVideo;

    const bool useGraphics = cell.charSet == static_cast<unsigned>(CharSetGraphics) &&
                             static_cast<unsigned char>(cell.ch) >= 0x60 &&
                             static_cast<unsigned char>(cell.ch) <= 0x7E;
    if (useGraphics && m_pGraphicsCharGen != nullptr)
    {
        m_pCharGen = m_pGraphicsCharGen;
    }
    else
    {
        m_pCharGen = savedCharGen;
    }

    const char renderChar = (cell.blink && !m_bTextBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
    DisplayChar(renderChar, nPosX, nPosY, GetTextColor());

    m_ForegroundColor = savedForeground;
    m_BackgroundColor = savedBackground;
    m_bBoldAttribute = savedBold;
    m_bDimAttribute = savedDim;
    m_bUnderlineAttribute = savedUnderline;
    m_bBlinkAttribute = savedBlink;
    m_bReverseAttribute = savedReverse;
    m_pCharGen = savedCharGen;
}

void CTRenderer::RenderShadowRow(unsigned row)
{
    const unsigned rowCount = GetRowCount();
    const unsigned cellHeight = GetBaseCharHeight();
    if (row >= rowCount || cellHeight == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned nPosY = row * cellHeight;
    if (nPosY >= m_nHeight)
    {
        return;
    }

    const unsigned visibleColumns = GetColumnsForY(nPosY);
    const unsigned cellWidth = GetCharCellWidthForY(nPosY);
    if (visibleColumns == 0 || cellWidth == 0)
    {
        return;
    }

    const CDisplay::TRawColor savedForeground = m_ForegroundColor;
    const CDisplay::TRawColor savedBackground = m_BackgroundColor;
    const boolean savedBold = m_bBoldAttribute;
    const boolean savedDim = m_bDimAttribute;
    const boolean savedUnderline = m_bUnderlineAttribute;
    const boolean savedBlink = m_bBlinkAttribute;
    const boolean savedReverse = m_bReverseAttribute;
    CCharGenerator *savedCharGen = m_pCharGen;

    FillPixelRows(nPosY, nPosY + cellHeight, savedBackground);

    const TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned column = 0; column < visibleColumns && column < MaxTextColumns; ++column)
    {
        const TShadowCell &cell = cells[row][column];

        m_ForegroundColor = cell.foreground;
        m_BackgroundColor = cell.background;
        m_bBoldAttribute = cell.bold;
        m_bDimAttribute = cell.dim;
        m_bUnderlineAttribute = (cell.blink && !m_bTextBlinkVisible) ? FALSE : cell.underline;
        m_bBlinkAttribute = cell.blink;
        m_bReverseAttribute = cell.reverseVideo;

        const bool useGraphics = cell.charSet == static_cast<unsigned>(CharSetGraphics) &&
                                 static_cast<unsigned char>(cell.ch) >= 0x60 &&
                                 static_cast<unsigned char>(cell.ch) <= 0x7E;
        if (useGraphics && m_pGraphicsCharGen != nullptr)
        {
            m_pCharGen = m_pGraphicsCharGen;
        }
        else
        {
            m_pCharGen = savedCharGen;
        }

        const unsigned nPosX = column * cellWidth;
        if (nPosX >= m_nWidth)
        {
            break;
        }

        const char renderChar = (cell.blink && !m_bTextBlinkVisible) ? ' ' : (cell.used ? cell.ch : ' ');
        DisplayChar(renderChar, nPosX, nPosY, GetTextColor());
    }

    m_ForegroundColor = savedForeground;
    m_BackgroundColor = savedBackground;
    m_bBoldAttribute = savedBold;
    m_bDimAttribute = savedDim;
    m_bUnderlineAttribute = savedUnderline;
    m_bBlinkAttribute = savedBlink;
    m_bReverseAttribute = savedReverse;
    m_pCharGen = savedCharGen;

    if (row + 1 == rowCount)
    {
        ClearUnusedBottomArea(savedBackground);
    }

    SetUpdateArea(nPosY, nPosY + cellHeight - 1);
}

void CTRenderer::RenderShadowScreen(void)
{
    const unsigned rowCount = GetRowCount();
    if (rowCount == 0)
    {
        ClearUnusedBottomArea(m_BackgroundColor);
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

void CTRenderer::DisplayChar(char chChar, unsigned nPosX, unsigned nPosY,
                             CDisplay::TRawColor nColor)
{
    const ELineAttribute attribute = GetLineAttributeForY(nPosY);
    const CCharGenerator *charGen = m_pCharGen;

    if (attribute == LineAttributeDoubleHeightTop || attribute == LineAttributeDoubleHeightBottom)
    {
        if (m_pCharGen == m_pGraphicsCharGen && m_pGraphicsDoubleBothCharGen != nullptr)
        {
            charGen = m_pGraphicsDoubleBothCharGen;
        }
        else if (m_pDoubleBothCharGen != nullptr)
        {
            charGen = m_pDoubleBothCharGen;
        }
    }

    const unsigned cellWidth = GetCharCellWidthForLineAttribute(attribute);
    const unsigned cellHeight = GetBaseCharHeight();

    if (charGen == nullptr || cellWidth == 0 || cellHeight == 0)
    {
        return;
    }

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

    for (unsigned y = 0; y < cellHeight; y++)
    {
        for (unsigned x = 0; x < cellWidth; x++)
        {
            const bool isGlyphPixel = SampleGlyphPixel(*charGen, chChar, attribute, x, y);
            const CDisplay::TRawColor pixelColor = isGlyphPixel ? nColor : GetTextBackgroundColor();
            SetRawPixel(nPosX + x, nPosY + y, pixelColor);
        }
    }

    if (m_bBoldAttribute)
    {
        for (unsigned y = 0; y < cellHeight; y++)
        {
            for (unsigned x = 1; x < cellWidth; x++)
            {
                if (SampleGlyphPixel(*charGen, chChar, attribute, x - 1, y))
                {
                    SetRawPixel(nPosX + x, nPosY + y, nColor);
                }
            }
        }
    }

    if (m_bUnderlineAttribute)
    {
        const unsigned underlineRow = charGen->GetUnderline();
        if (underlineRow < cellHeight)
        {
            for (unsigned x = 0; x < cellWidth; x++)
            {
                SetRawPixel(nPosX + x, nPosY + underlineRow, nColor);
            }
        }
    }

    SetUpdateArea(nPosY, nPosY + cellHeight - 1);
}

void CTRenderer::EraseChar(unsigned nPosX, unsigned nPosY)
{
    const unsigned row = GetRowIndexFromY(nPosY);
    const unsigned column = GetColumnIndexFromX(nPosX, nPosY);
    if (row >= MaxTextRows || column >= MaxTextColumns)
    {
        return;
    }

    ClearShadowCells(row, column, column + 1);
    RenderShadowCell(row, column);
}

void CTRenderer::InvertCursor(void)
{
    if (!m_bCursorOn)
    {
        return;
    }

    CDisplay::TRawColor *pPixelData = m_pCursorPixels;
    const unsigned cursorWidth = GetCharCellWidthForY(m_nCursorY);
    const unsigned cursorHeight = GetBaseCharHeight();
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
    for (unsigned y = y0; y < cursorHeight; y++)
    {
        for (unsigned x = 0; x < cursorWidth; x++)
        {
            if (!m_bCursorVisible)
            {
                const CDisplay::TRawColor storedPixel = GetRawPixel(m_nCursorX + x, m_nCursorY + y);
                *pPixelData++ = storedPixel;
                SetRawPixel(m_nCursorX + x, m_nCursorY + y, storedPixel ^ invertMask);
            }
            else
            {
                SetRawPixel(m_nCursorX + x, m_nCursorY + y, *pPixelData++);
            }
        }
    }

    m_bCursorVisible = !m_bCursorVisible;

    SetUpdateArea(m_nCursorY + y0, m_nCursorY + cursorHeight - 1);
}