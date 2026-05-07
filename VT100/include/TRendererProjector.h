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

#pragma once

#include <circle/chargenerator.h>
#include <circle/sched/task.h>
#include <circle/display.h>
#include <circle/types.h>

#include "TShadowBuffer.h"

class CRendererSurface;

/**
 * @file TRendererProjector.h
 * @brief Declares the framebuffer projection task used by CTRenderer.
 * @details CTRendererProjector consumes only the shared shadow/model state and
 * periodically projects it onto the framebuffer backend. CTRenderer remains
 * responsible for VT100 parsing and model mutation, while this task owns the
 * framebuffer refresh cadence.
 */

/**
 * @class CTRendererProjector
 * @brief Periodically redraws shadow-state changes onto the framebuffer.
 * @details The projector renders from the shared shadow/model snapshot at a
 * configured refresh cadence. Legacy renderer entry points still exist, but
 * they only request a refresh; actual projection happens inside the task.
 */
class CTRendererProjector : public CTask
{
public:
    using ELineAttribute = CShadowBuffer::ELineAttribute;
    using TShadowCell = CShadowBuffer::TShadowCell;
    using TProjectorState = CShadowBuffer::TProjectorState;

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

    /// @brief Bind the projector to the shared model and framebuffer backend.
    /// @param shadowBuffer Shadow-state model used as the projection source.
    /// @param surface Framebuffer backend used for raw pixel mutations and flushes.
    explicit CTRendererProjector(CShadowBuffer &shadowBuffer,
                                CRendererSurface &surface);

    /// @brief Start the periodic framebuffer refresh task.
    boolean Initialize(unsigned refreshHz = 60);

    /// @brief Task main loop.
    void Run(void) override;

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
    /// @brief Request a cursor redraw on the next projector tick.
    void InvertCursor(void);

private:
    /// @brief Resolve the correct glyph generator for a shadow cell.
    const CCharGenerator *GetCharGeneratorForCell(const TProjectorState &state,
                                                  unsigned charSet,
                                                  ELineAttribute attribute) const;
    /// @brief Compute projected colors and flags from one shadow cell snapshot.
    TProjectedCellStyle GetProjectedCellStyle(const TProjectorState &state,
                                              const TShadowCell &cell,
                                              ELineAttribute attribute) const;
    /// @brief Draw one character using a fully resolved projected style.
    void DisplayChar(char chChar,
                     unsigned nPosX,
                     unsigned nPosY,
                     const TProjectorState &state,
                     ELineAttribute attribute,
                     const TProjectedCellStyle &style);

    unsigned GetBaseCharWidth(const TProjectorState &state) const;
    unsigned GetBaseCharHeight(const TProjectorState &state) const;
    unsigned GetRowCount(const TProjectorState &state) const;
    unsigned GetCharCellWidthForLineAttribute(const TProjectorState &state, ELineAttribute attribute) const;
    unsigned GetColumnsForLineAttribute(const TProjectorState &state, ELineAttribute attribute) const;
    boolean SampleGlyphPixel(const CCharGenerator &charGen,
                             ELineAttribute attribute,
                             unsigned nPosX,
                             unsigned nPosY,
                             unsigned baseCharHeight,
                             char chChar) const;
    CDisplay::TRawColor AdjustBrightness565(CDisplay::TRawColor color, float factor) const;
    CDisplay::TRawColor ApplyProjectedGlyphBrightness(const TProjectorState &state,
                                                      CDisplay::TRawColor color,
                                                      boolean bold,
                                                      boolean dim) const;
    boolean ShadowHasBlinkCells(boolean altScreenActive, unsigned rowCount) const;
    boolean AdvanceBlinkState(TProjectorState &state);
    void RenderCursor(const TProjectorState &state);
    void RenderShadowCell(const TProjectorState &state, unsigned row, unsigned column);
    void RenderShadowRow(const TProjectorState &state, unsigned row);
    void RenderShadowScreen(const TProjectorState &state);
    void ClearUnusedBottomArea(const TProjectorState &state, CDisplay::TRawColor background);
    void RequestRefresh(void);

    CShadowBuffer &m_ShadowBuffer;
    CRendererSurface &m_Surface;
    unsigned m_nRefreshDelayMs;
    unsigned m_nLastRenderedGeneration;
};