#include "TShadowBuffer.h"

#include <string.h>

CShadowBuffer::CShadowBuffer(void)
{
    memset(m_LineAttributes, 0, sizeof(m_LineAttributes));
    memset(m_AltScreenLineAttributes, 0, sizeof(m_AltScreenLineAttributes));
    memset(m_ShadowCells, 0, sizeof(m_ShadowCells));
    memset(m_AltScreenShadowCells, 0, sizeof(m_AltScreenShadowCells));
}

void CShadowBuffer::ResetBuffer(TShadowCell cells[MaxTextRows][MaxTextColumns], const TStyle &defaultStyle)
{
    for (unsigned row = 0; row < MaxTextRows; ++row)
    {
        for (unsigned column = 0; column < MaxTextColumns; ++column)
        {
            cells[row][column].ch = ' ';
            cells[row][column].foreground = defaultStyle.foreground;
            cells[row][column].background = defaultStyle.background;
            cells[row][column].charSet = defaultStyle.charSet;
            cells[row][column].bold = defaultStyle.bold;
            cells[row][column].dim = defaultStyle.dim;
            cells[row][column].underline = defaultStyle.underline;
            cells[row][column].blink = defaultStyle.blink;
            cells[row][column].reverseVideo = defaultStyle.reverseVideo;
            cells[row][column].used = FALSE;
        }
    }
}

void CShadowBuffer::ResetBuffers(const TStyle &defaultStyle)
{
    ResetBuffer(m_ShadowCells, defaultStyle);
    ResetBuffer(m_AltScreenShadowCells, defaultStyle);
    ResetLineAttributes();
    memset(m_AltScreenLineAttributes, 0, sizeof(m_AltScreenLineAttributes));
}

void CShadowBuffer::ResetActiveBuffer(boolean altScreenActive, const TStyle &defaultStyle)
{
    ResetBuffer(altScreenActive ? m_AltScreenShadowCells : m_ShadowCells, defaultStyle);
}

void CShadowBuffer::ResetRow(boolean altScreenActive, unsigned row, const TStyle &style)
{
    if (row >= MaxTextRows)
    {
        return;
    }

    ClearCells(altScreenActive, row, 0, MaxTextColumns, style);
}

void CShadowBuffer::ClearCells(boolean altScreenActive,
                               unsigned row,
                               unsigned startColumn,
                               unsigned endColumn,
                               const TStyle &style)
{
    if (row >= MaxTextRows || startColumn >= MaxTextColumns)
    {
        return;
    }

    if (endColumn > MaxTextColumns)
    {
        endColumn = MaxTextColumns;
    }

    if (startColumn >= endColumn)
    {
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned column = startColumn; column < endColumn; ++column)
    {
        cells[row][column].ch = ' ';
        cells[row][column].foreground = style.foreground;
        cells[row][column].background = style.background;
        cells[row][column].charSet = style.charSet;
        cells[row][column].bold = style.bold;
        cells[row][column].dim = style.dim;
        cells[row][column].underline = style.underline;
        cells[row][column].blink = style.blink;
        cells[row][column].reverseVideo = style.reverseVideo;
        cells[row][column].used = FALSE;
    }
}

void CShadowBuffer::ShiftCellsLeft(boolean altScreenActive,
                                   unsigned row,
                                   unsigned startColumn,
                                   unsigned count,
                                   const TStyle &style)
{
    if (row >= MaxTextRows || startColumn >= MaxTextColumns || count == 0)
    {
        return;
    }

    if (count >= MaxTextColumns - startColumn)
    {
        ClearCells(altScreenActive, row, startColumn, MaxTextColumns, style);
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned column = startColumn; column + count < MaxTextColumns; ++column)
    {
        cells[row][column] = cells[row][column + count];
    }

    ClearCells(altScreenActive, row, MaxTextColumns - count, MaxTextColumns, style);
}

void CShadowBuffer::ShiftCellsRight(boolean altScreenActive,
                                    unsigned row,
                                    unsigned startColumn,
                                    unsigned count,
                                    const TStyle &style)
{
    if (row >= MaxTextRows || startColumn >= MaxTextColumns || count == 0)
    {
        return;
    }

    if (count >= MaxTextColumns - startColumn)
    {
        ClearCells(altScreenActive, row, startColumn, MaxTextColumns, style);
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned column = MaxTextColumns; column > startColumn + count; --column)
    {
        cells[row][column - 1] = cells[row][column - 1 - count];
    }

    ClearCells(altScreenActive, row, startColumn, startColumn + count, style);
}

void CShadowBuffer::ShiftRowsUp(boolean altScreenActive,
                                unsigned startRow,
                                unsigned endRow,
                                unsigned count,
                                const TStyle &style)
{
    if (startRow >= endRow || startRow >= MaxTextRows || count == 0)
    {
        return;
    }

    if (endRow > MaxTextRows)
    {
        endRow = MaxTextRows;
    }

    if (count >= endRow - startRow)
    {
        for (unsigned row = startRow; row < endRow; ++row)
        {
            ResetRow(altScreenActive, row, style);
        }
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned row = startRow; row + count < endRow; ++row)
    {
        memcpy(cells[row], cells[row + count], sizeof(cells[row]));
    }

    for (unsigned row = endRow - count; row < endRow; ++row)
    {
        ResetRow(altScreenActive, row, style);
    }
}

void CShadowBuffer::ShiftRowsDown(boolean altScreenActive,
                                  unsigned startRow,
                                  unsigned endRow,
                                  unsigned count,
                                  const TStyle &style)
{
    if (startRow >= endRow || startRow >= MaxTextRows || count == 0)
    {
        return;
    }

    if (endRow > MaxTextRows)
    {
        endRow = MaxTextRows;
    }

    if (count >= endRow - startRow)
    {
        for (unsigned row = startRow; row < endRow; ++row)
        {
            ResetRow(altScreenActive, row, style);
        }
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned row = endRow; row > startRow + count; --row)
    {
        memcpy(cells[row - 1], cells[row - 1 - count], sizeof(cells[row - 1]));
    }

    for (unsigned row = startRow; row < startRow + count; ++row)
    {
        ResetRow(altScreenActive, row, style);
    }
}

void CShadowBuffer::StoreCell(boolean altScreenActive,
                              unsigned row,
                              unsigned column,
                              char ch,
                              const TStyle &style)
{
    if (row >= MaxTextRows || column >= MaxTextColumns)
    {
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    cells[row][column].ch = ch;
    cells[row][column].foreground = style.foreground;
    cells[row][column].background = style.background;
    cells[row][column].charSet = style.charSet;
    cells[row][column].bold = style.bold;
    cells[row][column].dim = style.dim;
    cells[row][column].underline = style.underline;
    cells[row][column].blink = style.blink;
    cells[row][column].reverseVideo = style.reverseVideo;
    cells[row][column].used = TRUE;
}

CShadowBuffer::ELineAttribute CShadowBuffer::GetLineAttribute(unsigned row, unsigned rowCount) const
{
    if (rowCount == 0)
    {
        return LineAttributeNormal;
    }

    if (row >= rowCount)
    {
        row = rowCount - 1;
    }

    return m_LineAttributes[row];
}

void CShadowBuffer::SetLineAttribute(unsigned row, unsigned rowCount, ELineAttribute attribute)
{
    if (row >= rowCount)
    {
        return;
    }

    m_LineAttributes[row] = attribute;
}

void CShadowBuffer::ResetLineAttributes(void)
{
    for (unsigned row = 0; row < MaxTextRows; ++row)
    {
        m_LineAttributes[row] = LineAttributeNormal;
    }
}

void CShadowBuffer::ShiftLineAttributesUp(unsigned startRow, unsigned endRow, unsigned count)
{
    if (startRow >= endRow || count == 0)
    {
        return;
    }

    if (count >= endRow - startRow)
    {
        count = endRow - startRow;
    }

    for (unsigned row = startRow; row + count < endRow; ++row)
    {
        m_LineAttributes[row] = m_LineAttributes[row + count];
    }

    for (unsigned row = endRow - count; row < endRow; ++row)
    {
        m_LineAttributes[row] = LineAttributeNormal;
    }
}

void CShadowBuffer::ShiftLineAttributesDown(unsigned startRow, unsigned endRow, unsigned count)
{
    if (startRow >= endRow || count == 0)
    {
        return;
    }

    if (count >= endRow - startRow)
    {
        count = endRow - startRow;
    }

    for (unsigned row = endRow; row > startRow + count; --row)
    {
        m_LineAttributes[row - 1] = m_LineAttributes[row - 1 - count];
    }

    for (unsigned row = startRow; row < startRow + count; ++row)
    {
        m_LineAttributes[row] = LineAttributeNormal;
    }
}

void CShadowBuffer::CopyLineAttributesToAlternate(void)
{
    memcpy(m_AltScreenLineAttributes, m_LineAttributes, sizeof(m_LineAttributes));
}

void CShadowBuffer::RestoreLineAttributesFromAlternate(void)
{
    memcpy(m_LineAttributes, m_AltScreenLineAttributes, sizeof(m_LineAttributes));
}

const CShadowBuffer::ELineAttribute *CShadowBuffer::GetLineAttributes(void) const
{
    return m_LineAttributes;
}

void CShadowBuffer::RestoreLineAttributes(const ELineAttribute attributes[MaxTextRows])
{
    memcpy(m_LineAttributes, attributes, sizeof(m_LineAttributes));
}

boolean CShadowBuffer::RowHasBlink(boolean altScreenActive, unsigned row, unsigned rowCount) const
{
    if (row >= rowCount)
    {
        return FALSE;
    }

    const TShadowCell(*cells)[MaxTextColumns] = GetActiveCells(altScreenActive);
    for (unsigned column = 0; column < MaxTextColumns; ++column)
    {
        if (cells[row][column].used && cells[row][column].blink)
        {
            return TRUE;
        }
    }

    return FALSE;
}

CShadowBuffer::TShadowCell (*CShadowBuffer::GetActiveCells(boolean altScreenActive)) [MaxTextColumns]
{
    return altScreenActive ? m_AltScreenShadowCells : m_ShadowCells;
}

const CShadowBuffer::TShadowCell (*CShadowBuffer::GetActiveCells(boolean altScreenActive) const)[MaxTextColumns]
{
    return altScreenActive ? m_AltScreenShadowCells : m_ShadowCells;
}