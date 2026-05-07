#pragma once

#include <circle/chargenerator.h>
#include <circle/display.h>
#include <circle/types.h>

#include "TShadowBuffer.h"

class CTRenderer;

class CRendererProjector
{
public:
    using ELineAttribute = CShadowBuffer::ELineAttribute;
    using TShadowCell = CShadowBuffer::TShadowCell;

    struct TProjectedCellStyle
    {
        const CCharGenerator *charGen;
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        boolean bold;
        boolean dim;
        boolean underline;
    };

    explicit CRendererProjector(CTRenderer &renderer);

    void FillPixelRows(unsigned startY, unsigned endY, CDisplay::TRawColor color);
    void ScrollPixelRowsUp(unsigned startY, unsigned endY, unsigned deltaY);
    void ClearUnusedBottomArea(CDisplay::TRawColor background);
    void RenderShadowCell(unsigned row, unsigned column);
    void RenderShadowRow(unsigned row);
    void RenderShadowScreen(void);
    void DisplayChar(char chChar, unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);
    void EraseChar(unsigned nPosX, unsigned nPosY);
    void InvertCursor(void);

private:
    const CCharGenerator *GetCharGeneratorForCell(unsigned charSet, ELineAttribute attribute) const;
    TProjectedCellStyle GetProjectedCellStyle(const TShadowCell &cell, ELineAttribute attribute) const;
    void DisplayChar(char chChar,
                     unsigned nPosX,
                     unsigned nPosY,
                     ELineAttribute attribute,
                     const TProjectedCellStyle &style);

    CTRenderer &m_Renderer;
};