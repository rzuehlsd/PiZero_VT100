//------------------------------------------------------------------------------
// Module:        CShadowBuffer
// Description:   Stores the authoritative terminal shadow state for CTRenderer.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-05-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-05-07     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/display.h>
#include <circle/types.h>

/**
 * @file TShadowBuffer.h
 * @brief Declares the renderer shadow-state storage used by CTRenderer.
 * @details CShadowBuffer owns the authoritative text-cell model for both the
 * normal and alternate screens. It stores characters, per-cell style flags,
 * and DEC line-size attributes so framebuffer output can be rebuilt from state
 * rather than from already projected pixels.
 */

/**
 * @class CShadowBuffer
 * @brief Holds terminal text, style, and line-size state outside the framebuffer.
 * @details The class provides row, cell, and screen mutation helpers that match
 * VT100 operations such as clear, insert/delete, scroll, and alternate-screen
 * switching. CTRenderer uses this storage as its primary terminal model, while
 * the projector consumes it to redraw affected framebuffer regions.
 */
class CShadowBuffer
{
public:
    /// @brief Enumerates the supported DEC line-size attributes per row.
    enum ELineAttribute
    {
        LineAttributeNormal,
        LineAttributeDoubleWidth,
        LineAttributeDoubleHeightTop,
        LineAttributeDoubleHeightBottom
    };

    static constexpr unsigned MaxTextRows = 64;
    static constexpr unsigned MaxTextColumns = 160;

    /// @brief Stores one terminal cell with the character plus projected style state.
    struct TShadowCell
    {
        char ch;
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        unsigned charSet;
        boolean bold;
        boolean dim;
        boolean underline;
        boolean blink;
        boolean reverseVideo;
        boolean used;
    };

    /// @brief Describes the style used when clearing or storing shadow cells.
    struct TStyle
    {
        CDisplay::TRawColor foreground;
        CDisplay::TRawColor background;
        unsigned charSet;
        boolean bold;
        boolean dim;
        boolean underline;
        boolean blink;
        boolean reverseVideo;
    };

    /// @brief Construct an empty shadow buffer with cleared line attributes.
    CShadowBuffer(void);

    /// @brief Reset both normal and alternate buffers to a default style.
    /// @param defaultStyle Style applied to each cleared shadow cell.
    void ResetBuffers(const TStyle &defaultStyle);
    /// @brief Reset only the currently active screen buffer.
    /// @param altScreenActive TRUE to operate on the alternate screen buffer.
    /// @param defaultStyle Style applied to each cleared shadow cell.
    void ResetActiveBuffer(boolean altScreenActive, const TStyle &defaultStyle);
    /// @brief Reset one logical row in the active buffer.
    /// @param altScreenActive TRUE to operate on the alternate screen buffer.
    /// @param row Zero-based row index.
    /// @param style Style applied to the cleared cells.
    void ResetRow(boolean altScreenActive, unsigned row, const TStyle &style);
    /// @brief Clear a half-open range of cells within one row.
    /// @param altScreenActive TRUE to operate on the alternate screen buffer.
    /// @param row Zero-based row index.
    /// @param startColumn First cleared column.
    /// @param endColumn One-past-last cleared column.
    /// @param style Style applied to the cleared cells.
    void ClearCells(boolean altScreenActive,
                    unsigned row,
                    unsigned startColumn,
                    unsigned endColumn,
                    const TStyle &style);
    /// @brief Shift a row fragment left and clear the newly exposed tail cells.
    void ShiftCellsLeft(boolean altScreenActive,
                        unsigned row,
                        unsigned startColumn,
                        unsigned count,
                        const TStyle &style);
    /// @brief Shift a row fragment right and clear the newly exposed leading cells.
    void ShiftCellsRight(boolean altScreenActive,
                         unsigned row,
                         unsigned startColumn,
                         unsigned count,
                         const TStyle &style);
    /// @brief Scroll a row range upward in shadow storage.
    void ShiftRowsUp(boolean altScreenActive,
                     unsigned startRow,
                     unsigned endRow,
                     unsigned count,
                     const TStyle &style);
    /// @brief Scroll a row range downward in shadow storage.
    void ShiftRowsDown(boolean altScreenActive,
                       unsigned startRow,
                       unsigned endRow,
                       unsigned count,
                       const TStyle &style);
    /// @brief Store one printable cell with the supplied style snapshot.
    void StoreCell(boolean altScreenActive,
                   unsigned row,
                   unsigned column,
                   char ch,
                   const TStyle &style);

    /// @brief Query the DEC line-size attribute for one row.
    ELineAttribute GetLineAttribute(unsigned row, unsigned rowCount) const;
    /// @brief Set the DEC line-size attribute for one row.
    void SetLineAttribute(unsigned row, unsigned rowCount, ELineAttribute attribute);
    /// @brief Reset all line-size attributes to normal width/height.
    void ResetLineAttributes(void);
    /// @brief Shift line attributes upward together with the corresponding row scroll.
    void ShiftLineAttributesUp(unsigned startRow, unsigned endRow, unsigned count);
    /// @brief Shift line attributes downward together with the corresponding row scroll.
    void ShiftLineAttributesDown(unsigned startRow, unsigned endRow, unsigned count);
    /// @brief Snapshot line attributes for later alternate-screen restore.
    void CopyLineAttributesToAlternate(void);
    /// @brief Restore line attributes previously saved for the alternate screen.
    void RestoreLineAttributesFromAlternate(void);
    /// @brief Access the current line-attribute array.
    const ELineAttribute *GetLineAttributes(void) const;
    /// @brief Restore line attributes from a caller-provided snapshot.
    void RestoreLineAttributes(const ELineAttribute attributes[MaxTextRows]);

    /// @brief Check whether one row currently contains any blinking cells.
    boolean RowHasBlink(boolean altScreenActive, unsigned row, unsigned rowCount) const;

    /// @brief Access the active shadow cell matrix.
    TShadowCell (*GetActiveCells(boolean altScreenActive))[MaxTextColumns];
    /// @brief Access the active shadow cell matrix as const data.
    const TShadowCell (*GetActiveCells(boolean altScreenActive) const)[MaxTextColumns];

private:
    /// @brief Reset one concrete shadow-cell matrix.
    void ResetBuffer(TShadowCell cells[MaxTextRows][MaxTextColumns], const TStyle &defaultStyle);

    ELineAttribute m_LineAttributes[MaxTextRows];
    ELineAttribute m_AltScreenLineAttributes[MaxTextRows];
    TShadowCell m_ShadowCells[MaxTextRows][MaxTextColumns];
    TShadowCell m_AltScreenShadowCells[MaxTextRows][MaxTextColumns];
};