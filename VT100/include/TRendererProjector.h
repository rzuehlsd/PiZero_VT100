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

#pragma once

#include <circle/chargenerator.h>
#include <circle/display.h>
#include <circle/types.h>

#include "TShadowBuffer.h"

class CTRenderer;
class CRendererSurface;

/**
 * @file TRendererProjector.h
 * @brief Declares the framebuffer projection helper used by CTRenderer.
 * @details CRendererProjector consumes the renderer's shadow-state model and
 * converts affected cells, rows, or the full screen into raw framebuffer
 * pixels. It keeps the projection-specific logic out of CTRenderer while the
 * renderer remains the owner of parser state, terminal modes, and dirty-region
 * decisions.
 */

/**
 * @class CRendererProjector
 * @brief Redraws shadow-state changes onto the framebuffer.
 * @details The projector maps shadow cells and DEC line attributes to concrete
 * glyph generators, colors, and framebuffer writes. It also contains the raw
 * cursor inversion path and the optimized pixel-row helpers used when shadow
 * changes can be reflected more cheaply than a full rerender.
 */
class CRendererProjector
{
public:
    using ELineAttribute = CShadowBuffer::ELineAttribute;
    using TShadowCell = CShadowBuffer::TShadowCell;

    /// @brief Bundles the resolved glyph generator and projected cell colors.
    struct TProjectedCellStyle
    {
        const CCharGenerator *charGen;
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        boolean bold;
        boolean dim;
        boolean underline;
    };

    /// @brief Bind the projector to its owning renderer facade.
    /// @param renderer Renderer that owns terminal state and framebuffer access.
    /// @param shadowBuffer Shadow-state model used as the projection source.
    /// @param surface Framebuffer backend used for raw pixel mutations and flushes.
    explicit CRendererProjector(CTRenderer &renderer,
                                CShadowBuffer &shadowBuffer,
                                CRendererSurface &surface);

    /// @brief Fill a half-open range of pixel rows with one raw color.
    void FillPixelRows(unsigned startY, unsigned endY, CDisplay::TRawColor color);
    /// @brief Scroll a half-open range of pixel rows upward in-place.
    void ScrollPixelRowsUp(unsigned startY, unsigned endY, unsigned deltaY);
    /// @brief Clear the non-text remainder below the last usable text row.
    void ClearUnusedBottomArea(CDisplay::TRawColor background);
    /// @brief Project one shadow cell to the framebuffer.
    void RenderShadowCell(unsigned row, unsigned column);
    /// @brief Project one full shadow row to the framebuffer.
    void RenderShadowRow(unsigned row);
    /// @brief Rebuild the visible framebuffer contents from full shadow state.
    void RenderShadowScreen(void);
    /// @brief Draw one character directly at a concrete pixel position.
    void DisplayChar(char chChar, unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);
    /// @brief Clear one cell at an explicit pixel position via shadow-state rerender.
    void EraseChar(unsigned nPosX, unsigned nPosY);
    /// @brief Invert the currently active cursor area in the framebuffer.
    void InvertCursor(void);

private:
    /// @brief Resolve the correct glyph generator for a shadow cell.
    const CCharGenerator *GetCharGeneratorForCell(unsigned charSet, ELineAttribute attribute) const;
    /// @brief Compute projected colors and flags from one shadow cell snapshot.
    TProjectedCellStyle GetProjectedCellStyle(const TShadowCell &cell, ELineAttribute attribute) const;
    /// @brief Draw one character using a fully resolved projected style.
    void DisplayChar(char chChar,
                     unsigned nPosX,
                     unsigned nPosY,
                     ELineAttribute attribute,
                     const TProjectedCellStyle &style);

    CTRenderer &m_Renderer;
    CShadowBuffer &m_ShadowBuffer;
    CRendererSurface &m_Surface;
};