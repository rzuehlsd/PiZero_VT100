#pragma once

#include <circle/display.h>
#include <circle/types.h>

class CShadowBuffer
{
public:
    enum ELineAttribute
    {
        LineAttributeNormal,
        LineAttributeDoubleWidth,
        LineAttributeDoubleHeightTop,
        LineAttributeDoubleHeightBottom
    };

    static constexpr unsigned MaxTextRows = 64;
    static constexpr unsigned MaxTextColumns = 160;

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

    CShadowBuffer(void);

    void ResetBuffers(const TStyle &defaultStyle);
    void ResetActiveBuffer(boolean altScreenActive, const TStyle &defaultStyle);
    void ResetRow(boolean altScreenActive, unsigned row, const TStyle &style);
    void ClearCells(boolean altScreenActive,
                    unsigned row,
                    unsigned startColumn,
                    unsigned endColumn,
                    const TStyle &style);
    void ShiftCellsLeft(boolean altScreenActive,
                        unsigned row,
                        unsigned startColumn,
                        unsigned count,
                        const TStyle &style);
    void ShiftCellsRight(boolean altScreenActive,
                         unsigned row,
                         unsigned startColumn,
                         unsigned count,
                         const TStyle &style);
    void ShiftRowsUp(boolean altScreenActive,
                     unsigned startRow,
                     unsigned endRow,
                     unsigned count,
                     const TStyle &style);
    void ShiftRowsDown(boolean altScreenActive,
                       unsigned startRow,
                       unsigned endRow,
                       unsigned count,
                       const TStyle &style);
    void StoreCell(boolean altScreenActive,
                   unsigned row,
                   unsigned column,
                   char ch,
                   const TStyle &style);

    ELineAttribute GetLineAttribute(unsigned row, unsigned rowCount) const;
    void SetLineAttribute(unsigned row, unsigned rowCount, ELineAttribute attribute);
    void ResetLineAttributes(void);
    void ShiftLineAttributesUp(unsigned startRow, unsigned endRow, unsigned count);
    void ShiftLineAttributesDown(unsigned startRow, unsigned endRow, unsigned count);
    void CopyLineAttributesToAlternate(void);
    void RestoreLineAttributesFromAlternate(void);
    const ELineAttribute *GetLineAttributes(void) const;
    void RestoreLineAttributes(const ELineAttribute attributes[MaxTextRows]);

    boolean RowHasBlink(boolean altScreenActive, unsigned row, unsigned rowCount) const;

    TShadowCell (*GetActiveCells(boolean altScreenActive))[MaxTextColumns];
    const TShadowCell (*GetActiveCells(boolean altScreenActive) const)[MaxTextColumns];

private:
    void ResetBuffer(TShadowCell cells[MaxTextRows][MaxTextColumns], const TStyle &defaultStyle);

    ELineAttribute m_LineAttributes[MaxTextRows];
    ELineAttribute m_AltScreenLineAttributes[MaxTextRows];
    TShadowCell m_ShadowCells[MaxTextRows][MaxTextColumns];
    TShadowCell m_AltScreenShadowCells[MaxTextRows][MaxTextColumns];
};