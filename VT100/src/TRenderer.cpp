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
#include "kernel.h"

LOGMODULE("TRenderer");

namespace
{
    static void SendHostReply(const char *data, size_t length)
    {
        if (data == nullptr || length == 0)
        {
            return;
        }

        CKernel *kernel = CKernel::Get();
        if (kernel == nullptr)
        {
            return;
        }

        kernel->SendHostOutput(data, length);
    }

    static void SendPrimaryDA(void)
    {
        // Identify as VT100 class (primary DA). Many apps (incl. vttest) expect this.
        static const char Reply[] = "\x1B[?1;0c";
        SendHostReply(Reply, sizeof Reply - 1);
    }
}

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
      m_pDoubleBothCharGen(nullptr),
      m_pGraphicsDoubleBothCharGen(nullptr),
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
      m_nColumnModeColumns(0),
      m_nDepth(0),
      m_State(StateStart),
      m_nScrollStart(0),
      m_nScrollEnd(0),
      m_nCursorX(0),
      m_nCursorY(0),
      m_bWrapPending(FALSE),
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
      m_bOriginMode(FALSE),
      m_bWrapAroundMode(TRUE),
      m_bNewLineMode(FALSE), // VT100 default: LF is IND unless ANSI New Line Mode is enabled.
      m_bAltScreenActive(FALSE),
      m_bAltScreenSavedValid(FALSE),
      m_pAltScreenSnapshot(nullptr),
      m_nAltScreenSnapshotSize(0),
      m_bAutoPage(FALSE),
      m_bCSIPrivate(FALSE),
      m_nCSIParamCount(0),
      m_nCSIParamValue(0),
      m_bCSIHaveValue(FALSE),
      m_bCSILastWasSeparator(FALSE),
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
    memset(&m_AltScreenSavedState, 0, sizeof(m_AltScreenSavedState));
    memset(m_CSIParams, 0, sizeof(m_CSIParams));
    ResetShadowBuffer(m_ShadowCells);
    ResetShadowBuffer(m_AltScreenShadowCells);

    SetName("Renderer");
    Suspend();
}

namespace
{
    inline unsigned CSIParamOrDefault(const unsigned *params, unsigned count, unsigned index, unsigned defaultValue)
    {
        if (index >= count)
        {
            return defaultValue;
        }
        if (params[index] == 0)
        {
            return defaultValue;
        }
        return params[index];
    }
}

unsigned CTRenderer::GetBaseCharWidth(void) const
{
    return m_pCharGen ? m_pCharGen->GetCharWidth() : 0;
}

unsigned CTRenderer::GetBaseCharHeight(void) const
{
    return m_pCharGen ? m_pCharGen->GetCharHeight() : 0;
}

unsigned CTRenderer::GetRowCount(void) const
{
    const unsigned charHeight = GetBaseCharHeight();
    if (charHeight == 0)
    {
        return 0;
    }

    unsigned rows = m_nUsedHeight / charHeight;
    if (rows > MaxTextRows)
    {
        rows = MaxTextRows;
    }

    return rows;
}

unsigned CTRenderer::GetRowIndexFromY(unsigned nPosY) const
{
    const unsigned charHeight = GetBaseCharHeight();
    if (charHeight == 0)
    {
        return 0;
    }

    unsigned row = nPosY / charHeight;
    const unsigned rowCount = GetRowCount();
    if (rowCount == 0)
    {
        return 0;
    }
    if (row >= rowCount)
    {
        row = rowCount - 1;
    }
    return row;
}

unsigned CTRenderer::GetColumnIndexFromX(unsigned nPosX, unsigned nPosY) const
{
    const unsigned charWidth = GetCharCellWidthForY(nPosY);
    if (charWidth == 0)
    {
        return 0;
    }

    unsigned column = nPosX / charWidth;
    const unsigned columns = GetColumnsForY(nPosY);
    if (columns == 0)
    {
        return 0;
    }
    if (column >= columns)
    {
        column = columns - 1;
    }
    return column;
}

CTRenderer::ELineAttribute CTRenderer::GetLineAttributeForRow(unsigned row) const
{
    const unsigned rowCount = GetRowCount();
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

CTRenderer::ELineAttribute CTRenderer::GetLineAttributeForY(unsigned nPosY) const
{
    return GetLineAttributeForRow(GetRowIndexFromY(nPosY));
}

void CTRenderer::SetLineAttributeForRow(unsigned row, ELineAttribute attribute)
{
    const unsigned rowCount = GetRowCount();
    if (row >= rowCount)
    {
        return;
    }

    m_LineAttributes[row] = attribute;
}

void CTRenderer::ResetLineAttributes(void)
{
    for (unsigned row = 0; row < MaxTextRows; ++row)
    {
        m_LineAttributes[row] = LineAttributeNormal;
    }
}

CTRenderer::TShadowCell (*CTRenderer::GetActiveShadowCells(void)) [MaxTextColumns]
{
    return m_bAltScreenActive ? m_AltScreenShadowCells : m_ShadowCells;
}

const CTRenderer::TShadowCell (*CTRenderer::GetActiveShadowCells(void) const)[MaxTextColumns]
{
    return m_bAltScreenActive ? m_AltScreenShadowCells : m_ShadowCells;
}

void CTRenderer::ResetShadowBuffer(TShadowCell cells[MaxTextRows][MaxTextColumns])
{
    for (unsigned row = 0; row < MaxTextRows; ++row)
    {
        for (unsigned column = 0; column < MaxTextColumns; ++column)
        {
            cells[row][column].ch = ' ';
            cells[row][column].foreground = m_DefaultForegroundColor;
            cells[row][column].background = m_DefaultBackgroundColor;
            cells[row][column].charSet = static_cast<unsigned>(CharSetUS);
            cells[row][column].bold = FALSE;
            cells[row][column].dim = FALSE;
            cells[row][column].underline = FALSE;
            cells[row][column].blink = FALSE;
            cells[row][column].reverseVideo = FALSE;
            cells[row][column].used = FALSE;
        }
    }
}

void CTRenderer::ResetShadowRow(unsigned row)
{
    if (row >= MaxTextRows)
    {
        return;
    }

    ClearShadowCells(row, 0, MaxTextColumns);
}

void CTRenderer::ClearShadowCells(unsigned row, unsigned startColumn, unsigned endColumn)
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

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    const unsigned activeCharSet = static_cast<unsigned>(m_bUseG1 ? m_G1CharSet : m_G0CharSet);
    const CDisplay::TRawColor foreground = GetTextColor();
    const CDisplay::TRawColor background = GetTextBackgroundColor();

    for (unsigned column = startColumn; column < endColumn; ++column)
    {
        cells[row][column].ch = ' ';
        cells[row][column].foreground = foreground;
        cells[row][column].background = background;
        cells[row][column].charSet = activeCharSet;
        cells[row][column].bold = m_bBoldAttribute;
        cells[row][column].dim = m_bDimAttribute;
        cells[row][column].underline = m_bUnderlineAttribute;
        cells[row][column].blink = m_bBlinkAttribute;
        cells[row][column].reverseVideo = m_bReverseAttribute;
        cells[row][column].used = FALSE;
    }
}

void CTRenderer::ShiftShadowCellsLeft(unsigned row, unsigned startColumn, unsigned count)
{
    if (row >= MaxTextRows || startColumn >= MaxTextColumns || count == 0)
    {
        return;
    }

    if (count >= MaxTextColumns - startColumn)
    {
        ClearShadowCells(row, startColumn, MaxTextColumns);
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned column = startColumn; column + count < MaxTextColumns; ++column)
    {
        cells[row][column] = cells[row][column + count];
    }

    ClearShadowCells(row, MaxTextColumns - count, MaxTextColumns);
}

void CTRenderer::ShiftShadowCellsRight(unsigned row, unsigned startColumn, unsigned count)
{
    if (row >= MaxTextRows || startColumn >= MaxTextColumns || count == 0)
    {
        return;
    }

    if (count >= MaxTextColumns - startColumn)
    {
        ClearShadowCells(row, startColumn, MaxTextColumns);
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned column = MaxTextColumns; column > startColumn + count; --column)
    {
        cells[row][column - 1] = cells[row][column - 1 - count];
    }

    ClearShadowCells(row, startColumn, startColumn + count);
}

void CTRenderer::ShiftShadowRowsUp(unsigned startRow, unsigned endRow, unsigned count)
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
            ResetShadowRow(row);
        }
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned row = startRow; row + count < endRow; ++row)
    {
        memcpy(cells[row], cells[row + count], sizeof(cells[row]));
    }

    for (unsigned row = endRow - count; row < endRow; ++row)
    {
        ResetShadowRow(row);
    }
}

void CTRenderer::ShiftShadowRowsDown(unsigned startRow, unsigned endRow, unsigned count)
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
            ResetShadowRow(row);
        }
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned row = endRow; row > startRow + count; --row)
    {
        memcpy(cells[row - 1], cells[row - 1 - count], sizeof(cells[row - 1]));
    }

    for (unsigned row = startRow; row < startRow + count; ++row)
    {
        ResetShadowRow(row);
    }
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

    for (unsigned y = nPosY; y < nPosY + cellHeight && y < m_nHeight; ++y)
    {
        for (unsigned x = 0; x < m_nWidth; ++x)
        {
            SetRawPixel(x, y, savedBackground);
        }
    }

    const TShadowCell (*cells)[MaxTextColumns] = GetActiveShadowCells();
    for (unsigned column = 0; column < visibleColumns && column < MaxTextColumns; ++column)
    {
        const TShadowCell &cell = cells[row][column];

        m_ForegroundColor = cell.foreground;
        m_BackgroundColor = cell.background;
        m_bBoldAttribute = cell.bold;
        m_bDimAttribute = cell.dim;
        m_bUnderlineAttribute = cell.underline;
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

        DisplayChar(cell.used ? cell.ch : ' ', nPosX, nPosY, GetTextColor());
    }

    m_ForegroundColor = savedForeground;
    m_BackgroundColor = savedBackground;
    m_bBoldAttribute = savedBold;
    m_bDimAttribute = savedDim;
    m_bUnderlineAttribute = savedUnderline;
    m_bBlinkAttribute = savedBlink;
    m_bReverseAttribute = savedReverse;
    m_pCharGen = savedCharGen;
    SetUpdateArea(nPosY, nPosY + cellHeight - 1);
}

void CTRenderer::StoreShadowCellAt(unsigned nPosX,
                                   unsigned nPosY,
                                   char chChar,
                                   CDisplay::TRawColor foreground,
                                   CDisplay::TRawColor background,
                                   unsigned charSet)
{
    const unsigned row = GetRowIndexFromY(nPosY);
    const unsigned column = GetColumnIndexFromX(nPosX, nPosY);
    if (row >= MaxTextRows || column >= MaxTextColumns)
    {
        return;
    }

    TShadowCell(*cells)[MaxTextColumns] = GetActiveShadowCells();
    cells[row][column].ch = chChar;
    cells[row][column].foreground = foreground;
    cells[row][column].background = background;
    cells[row][column].charSet = charSet;
    cells[row][column].bold = m_bBoldAttribute;
    cells[row][column].dim = m_bDimAttribute;
    cells[row][column].underline = m_bUnderlineAttribute;
    cells[row][column].blink = m_bBlinkAttribute;
    cells[row][column].reverseVideo = m_bReverseAttribute;
    cells[row][column].used = TRUE;
}

void CTRenderer::ShiftLineAttributesUp(unsigned startRow, unsigned endRow, unsigned count)
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

void CTRenderer::ShiftLineAttributesDown(unsigned startRow, unsigned endRow, unsigned count)
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

boolean CTRenderer::IsDoubleWidthLineAttribute(ELineAttribute attribute) const
{
    return attribute == LineAttributeDoubleWidth || attribute == LineAttributeDoubleHeightTop || attribute == LineAttributeDoubleHeightBottom;
}

unsigned CTRenderer::GetCharCellWidthForLineAttribute(ELineAttribute attribute) const
{
    const unsigned baseCharWidth = GetBaseCharWidth();
    if (baseCharWidth == 0)
    {
        return 0;
    }

    return IsDoubleWidthLineAttribute(attribute) ? baseCharWidth * 2 : baseCharWidth;
}

unsigned CTRenderer::GetCharCellWidthForY(unsigned nPosY) const
{
    return GetCharCellWidthForLineAttribute(GetLineAttributeForY(nPosY));
}

unsigned CTRenderer::GetColumnsForY(unsigned nPosY) const
{
    const unsigned charWidth = GetCharCellWidthForY(nPosY);
    if (charWidth == 0)
    {
        return 0;
    }

    return m_nUsedWidth / charWidth;
}

void CTRenderer::RecomputeCursorXForCurrentLine(ELineAttribute previousAttribute)
{
    const unsigned previousCharWidth = GetCharCellWidthForLineAttribute(previousAttribute);
    const unsigned currentCharWidth = GetCharCellWidthForY(m_nCursorY);
    if (previousCharWidth == 0 || currentCharWidth == 0)
    {
        return;
    }

    const unsigned logicalColumn = m_nCursorX / previousCharWidth;
    m_nCursorX = logicalColumn * currentCharWidth;
    ClampCursorToLineWidth();
}

void CTRenderer::ApplyColumnMode(unsigned nColumns, boolean clearScreen)
{
    if (m_pCharGen == nullptr)
    {
        m_nColumnModeColumns = nColumns;
        return;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    if (charWidth == 0)
    {
        m_nColumnModeColumns = nColumns;
        return;
    }

    const unsigned physicalColumns = m_nWidth / charWidth;
    unsigned effectiveColumns = nColumns;
    if (effectiveColumns == 0 || effectiveColumns > physicalColumns)
    {
        effectiveColumns = physicalColumns;
    }

    m_nColumnModeColumns = effectiveColumns;

    unsigned columns = physicalColumns;
    if (m_nColumnModeColumns != 0 && columns > m_nColumnModeColumns)
    {
        columns = m_nColumnModeColumns;
    }

    m_nUsedWidth = columns * charWidth;

    if (clearScreen)
    {
        // DECCOLM starts a fresh full-screen page on VT100-class terminals.
        // Reapply the baseline state that affects cursor motion and wrapping
        // before starting the fresh full-screen page.
        m_State = StateStart;
        m_nParam1 = 0;
        m_nParam2 = 0;
        m_bCSIPrivate = FALSE;
        m_nCSIParamCount = 0;
        m_nCSIParamValue = 0;
        m_bCSIHaveValue = FALSE;
        m_bCSILastWasSeparator = FALSE;

        m_bVT52Mode = FALSE;
        m_bOriginMode = FALSE;
        m_bInsertOn = FALSE;
        m_bNewLineMode = FALSE;
        m_bAutoPage = FALSE;
        SetWrapAroundMode(TRUE);

        m_G0CharSet = CharSetUS;
        m_G1CharSet = CharSetGraphics;
        m_bUseG1 = FALSE;

        SetStandoutMode(0);
        SetCursorMode(TRUE);

        if (m_pCharGen != nullptr)
        {
            SetFont(m_CurrentFontSelection, CCharGenerator::FontFlagsNone);
        }

        ResetLineAttributes();
        m_bWrapPending = FALSE;
        SetScrollRegion(1, 0);
        ClearDisplay();
    }
    else
    {
        ClampCursorToLineWidth();
        // Reapply the terminal baseline that affects cursor motion and wrapping
        // before starting the fresh full-screen page.
    }
}

void CTRenderer::ClampCursorToLineWidth(void)
{
    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    if (charWidth == 0 || m_nUsedWidth < charWidth)
    {
        m_nCursorX = 0;
        return;
    }

    const unsigned lastColumnX = m_nUsedWidth - charWidth;
    if (m_nCursorX > lastColumnX)
    {
        m_nCursorX = lastColumnX;
    }

    m_nCursorX = (m_nCursorX / charWidth) * charWidth;
}

boolean CTRenderer::SampleGlyphPixel(const CCharGenerator &charGen,
                                     char chChar,
                                     ELineAttribute attribute,
                                     unsigned nPosX,
                                     unsigned nPosY) const
{
    const unsigned baseCharWidth = charGen.GetCharWidth();
    const unsigned baseCharHeight = charGen.GetCharHeight();
    if (baseCharWidth == 0 || baseCharHeight == 0)
    {
        return FALSE;
    }

    unsigned sourceX = nPosX;
    unsigned sourceY = nPosY;

    if (attribute == LineAttributeDoubleHeightTop || attribute == LineAttributeDoubleHeightBottom)
    {
        const unsigned halfHeight = GetBaseCharHeight();
        if (halfHeight == 0 || baseCharHeight < halfHeight)
        {
            return FALSE;
        }

        if (attribute == LineAttributeDoubleHeightTop)
        {
            sourceY = nPosY;
        }
        else
        {
            sourceY = nPosY + halfHeight;
        }
    }
    else if (IsDoubleWidthLineAttribute(attribute))
    {
        sourceX >>= 1;
    }

    if (sourceX >= baseCharWidth || sourceY >= baseCharHeight)
    {
        return FALSE;
    }

    return charGen.GetPixel(chChar, sourceX, sourceY);
}

CTRenderer::~CTRenderer(void)
{
    CDeviceNameService::Get()->RemoveDevice(DevicePrefix, m_nDisplayIndex + 1, FALSE);

    ResetLineAttributes();
    memset(m_AltScreenLineAttributes, 0, sizeof(m_AltScreenLineAttributes));
    delete[] m_pBuffer8;
    m_pBuffer8 = nullptr;

    delete[] m_pCursorPixels;
    m_pCursorPixels = nullptr;

    delete[] m_pSmoothScrollSnapshot;
    m_pSmoothScrollSnapshot = nullptr;

    delete[] m_pSmoothScrollCompose;
    m_pSmoothScrollCompose = nullptr;

    delete[] m_pAltScreenSnapshot;
    m_pAltScreenSnapshot = nullptr;
    m_nAltScreenSnapshotSize = 0;

    delete m_pCharGen;
    m_pCharGen = nullptr;

    delete m_pGraphicsCharGen;
    m_pGraphicsCharGen = nullptr;

    delete m_pDoubleBothCharGen;
    m_pDoubleBothCharGen = nullptr;

    delete m_pGraphicsDoubleBothCharGen;
    m_pGraphicsDoubleBothCharGen = nullptr;

    delete m_pFrameBuffer;
    m_pFrameBuffer = nullptr;
}

void CTRenderer::EnterAlternateScreen(void)
{
    if (m_pBuffer8 == nullptr)
    {
        return;
    }

    // Already in alt screen.
    if (m_bAltScreenActive)
    {
        return;
    }

    // Ensure snapshot buffer exists and is sized for the current framebuffer.
    if (m_pAltScreenSnapshot == nullptr || m_nAltScreenSnapshotSize < m_nSize)
    {
        delete[] m_pAltScreenSnapshot;
        m_pAltScreenSnapshot = new u8[m_nSize];
        m_nAltScreenSnapshotSize = m_pAltScreenSnapshot ? m_nSize : 0;
    }

    if (m_pAltScreenSnapshot == nullptr || m_nAltScreenSnapshotSize < m_nSize)
    {
        // Cannot enter alt screen without a snapshot.
        return;
    }

    // Save the visible framebuffer.
    memcpy(m_pAltScreenSnapshot, m_pBuffer8, m_nSize);
    memcpy(m_AltScreenLineAttributes, m_LineAttributes, sizeof(m_LineAttributes));

    // Save key terminal state so we can restore a sane session on exit.
    m_AltScreenSavedState.cursorX = m_nCursorX;
    m_AltScreenSavedState.cursorY = m_nCursorY;
    m_AltScreenSavedState.scrollStart = m_nScrollStart;
    m_AltScreenSavedState.scrollEnd = m_nScrollEnd;
    m_AltScreenSavedState.vt52Mode = m_bVT52Mode;
    m_AltScreenSavedState.originMode = m_bOriginMode;
    m_AltScreenSavedState.wrapAroundMode = m_bWrapAroundMode;
    m_AltScreenSavedState.insertOn = m_bInsertOn;
    m_AltScreenSavedState.autoPage = m_bAutoPage;
    m_AltScreenSavedState.reverseAttribute = m_bReverseAttribute;
    m_AltScreenSavedState.boldAttribute = m_bBoldAttribute;
    m_AltScreenSavedState.dimAttribute = m_bDimAttribute;
    m_AltScreenSavedState.underlineAttribute = m_bUnderlineAttribute;
    m_AltScreenSavedState.blinkAttribute = m_bBlinkAttribute;
    m_AltScreenSavedState.foreground = m_ForegroundColor;
    m_AltScreenSavedState.background = m_BackgroundColor;
    m_AltScreenSavedState.defaultForeground = m_DefaultForegroundColor;
    m_AltScreenSavedState.defaultBackground = m_DefaultBackgroundColor;
    m_AltScreenSavedState.g0CharSet = static_cast<unsigned>(m_G0CharSet);
    m_AltScreenSavedState.g1CharSet = static_cast<unsigned>(m_G1CharSet);
    m_AltScreenSavedState.useG1 = m_bUseG1;
    m_bAltScreenSavedValid = TRUE;

    // Switch to a clean screen for full-screen apps.
    m_bAltScreenActive = TRUE;
    ResetLineAttributes();
    SetScrollRegion(1, 0);
    CursorHome();
    ClearDisplay();
    SetUpdateArea(0, m_nHeight ? (m_nHeight - 1) : 0);
}

void CTRenderer::LeaveAlternateScreen(void)
{
    if (!m_bAltScreenActive)
    {
        return;
    }

    if (m_pBuffer8 != nullptr && m_pAltScreenSnapshot != nullptr && m_nAltScreenSnapshotSize >= m_nSize)
    {
        memcpy(m_pBuffer8, m_pAltScreenSnapshot, m_nSize);
    }

    // Restore saved state (best effort).
    if (m_bAltScreenSavedValid && m_pCharGen != nullptr)
    {
        memcpy(m_LineAttributes, m_AltScreenLineAttributes, sizeof(m_LineAttributes));
        const unsigned charWidth = m_pCharGen->GetCharWidth();
        const unsigned charHeight = m_pCharGen->GetCharHeight();

        m_bVT52Mode = m_AltScreenSavedState.vt52Mode;
        m_bOriginMode = m_AltScreenSavedState.originMode;
        SetWrapAroundMode(m_AltScreenSavedState.wrapAroundMode);
        m_bInsertOn = m_AltScreenSavedState.insertOn;
        m_bAutoPage = m_AltScreenSavedState.autoPage;
        m_G0CharSet = static_cast<ECharacterSet>(m_AltScreenSavedState.g0CharSet);
        m_G1CharSet = static_cast<ECharacterSet>(m_AltScreenSavedState.g1CharSet);
        m_bUseG1 = m_AltScreenSavedState.useG1;

        m_bReverseAttribute = m_AltScreenSavedState.reverseAttribute;
        m_bBoldAttribute = m_AltScreenSavedState.boldAttribute;
        m_bDimAttribute = m_AltScreenSavedState.dimAttribute;
        m_bUnderlineAttribute = m_AltScreenSavedState.underlineAttribute;
        m_bBlinkAttribute = m_AltScreenSavedState.blinkAttribute;
        m_ForegroundColor = m_AltScreenSavedState.foreground;
        m_BackgroundColor = m_AltScreenSavedState.background;
        m_DefaultForegroundColor = m_AltScreenSavedState.defaultForeground;
        m_DefaultBackgroundColor = m_AltScreenSavedState.defaultBackground;

        // Clamp scroll region and cursor to current cell grid.
        if (m_nUsedHeight != 0)
        {
            const unsigned maxEnd = m_nUsedHeight;
            const unsigned start = (m_AltScreenSavedState.scrollStart <= maxEnd) ? m_AltScreenSavedState.scrollStart : 0;
            const unsigned end = (m_AltScreenSavedState.scrollEnd <= maxEnd) ? m_AltScreenSavedState.scrollEnd : maxEnd;
            m_nScrollStart = (start <= end) ? start : 0;
            m_nScrollEnd = (end > m_nScrollStart) ? end : maxEnd;
        }

        if (charWidth != 0 && charHeight != 0)
        {
            const unsigned lastColumnX = (m_nUsedWidth >= charWidth) ? (m_nUsedWidth - charWidth) : 0U;
            const unsigned lastRowY = (m_nUsedHeight >= charHeight) ? (m_nUsedHeight - charHeight) : 0U;

            m_nCursorX = (m_AltScreenSavedState.cursorX <= lastColumnX) ? m_AltScreenSavedState.cursorX : lastColumnX;
            m_nCursorY = (m_AltScreenSavedState.cursorY <= lastRowY) ? m_AltScreenSavedState.cursorY : lastRowY;

            m_nCursorX = (m_nCursorX / charWidth) * charWidth;
            m_nCursorY = (m_nCursorY / charHeight) * charHeight;
            ClampCursorToLineWidth();
        }
    }

    m_bAltScreenActive = FALSE;
    SetUpdateArea(0, m_nHeight ? (m_nHeight - 1) : 0);
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
    return ApplyFont(font, FontFlags, FALSE);
}

bool CTRenderer::SetFont(const TFont &rFont, CCharGenerator::TFontFlags FontFlags)
{
    return ApplyFont(rFont, FontFlags, FALSE);
}

bool CTRenderer::ApplyFont(const TFont &rFont,
                           CCharGenerator::TFontFlags FontFlags,
                           boolean preservePixelCursor)
{
    m_SpinLock.Acquire();

    const bool cursorWasVisible = m_bCursorVisible;
    const bool blinkingWasEnabled = m_bBlinkingCursor;
    const unsigned previousCursorX = m_nCursorX;
    const unsigned previousCursorY = m_nCursorY;

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

    delete m_pDoubleBothCharGen;
    m_pDoubleBothCharGen = nullptr;

    delete m_pGraphicsDoubleBothCharGen;
    m_pGraphicsDoubleBothCharGen = nullptr;

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
    m_pDoubleBothCharGen = new CCharGenerator(rFont, CCharGenerator::FontFlagsDoubleBoth);
    m_pGraphicsDoubleBothCharGen = new CCharGenerator(gfxFont, CCharGenerator::FontFlagsDoubleBoth);

    if (m_pGraphicsCharGen == nullptr || m_pDoubleBothCharGen == nullptr || m_pGraphicsDoubleBothCharGen == nullptr)
    {
        delete m_pCharGen;
        m_pCharGen = nullptr;
        delete m_pGraphicsCharGen;
        m_pGraphicsCharGen = nullptr;
        delete m_pDoubleBothCharGen;
        m_pDoubleBothCharGen = nullptr;
        delete m_pGraphicsDoubleBothCharGen;
        m_pGraphicsDoubleBothCharGen = nullptr;
        if (cursorWasVisible)
        {
            m_bCursorVisible = false;
        }
        m_bBlinkingCursor = blinkingWasEnabled;
        m_SpinLock.Release();
        return FALSE;
    }

    delete[] m_pCursorPixels;
    m_pCursorPixels = nullptr;

    const unsigned cursorPixelCount = (m_pCharGen->GetCharWidth() * 2) * m_pCharGen->GetCharHeight();
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

    ApplyColumnMode(m_nColumnModeColumns, FALSE);
    m_nUsedHeight = m_nHeight / m_pCharGen->GetCharHeight() * m_pCharGen->GetCharHeight();
    m_nScrollEnd = m_nUsedHeight;

    if (preservePixelCursor)
    {
        const unsigned maxCursorX = (m_nUsedWidth >= m_pCharGen->GetCharWidth())
                                        ? (m_nUsedWidth - m_pCharGen->GetCharWidth())
                                        : 0;
        const unsigned maxCursorY = (m_nUsedHeight >= m_pCharGen->GetCharHeight())
                                        ? (m_nUsedHeight - m_pCharGen->GetCharHeight())
                                        : 0;

        m_nCursorX = previousCursorX <= maxCursorX ? previousCursorX : maxCursorX;
        m_nCursorY = previousCursorY <= maxCursorY ? previousCursorY : maxCursorY;
    }
    else
    {
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

    const unsigned cols = GetColumns();
    const unsigned rows = GetRows();
    const unsigned charWidth = m_pCharGen->GetCharWidth();
    const unsigned charHeight = m_pCharGen->GetCharHeight();

    if (cols != 0 && charWidth != 0)
    {
        const unsigned targetCol = (nColumn < cols) ? nColumn : (cols - 1);
        m_nCursorX = targetCol * charWidth;
    }
    else
    {
        m_nCursorX = 0;
    }

    if (rows != 0 && charHeight != 0)
    {
        const unsigned targetRow = (nRow < rows) ? nRow : (rows - 1);
        m_nCursorY = targetRow * charHeight;
    }
    else
    {
        m_nCursorY = 0;
    }

    if (cursorWasVisible && m_bCursorOn)
    {
        InvertCursor();
        if (m_bBlinkingCursor)
        {
            m_nNextCursorBlink = CTimer::Get()->GetTicks() + m_nCursorBlinkPeriodTicks;
        }
    }

    m_bWrapPending = FALSE;

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
    return GetColumnsForY(m_nCursorY);
}

unsigned CTRenderer::GetRows(void) const
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

    // Use cell-aligned height to avoid reporting a row that would be partially visible.
    return m_nUsedHeight / charHeight;
}

unsigned CTRenderer::GetCursorColumn(void) const
{
    m_SpinLock.Acquire();
    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    unsigned column = (charWidth != 0) ? (m_nCursorX / charWidth) : 0;
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
    m_bCSIPrivate = FALSE;
    m_nCSIParamCount = 0;
    m_nCSIParamValue = 0;
    m_bCSIHaveValue = FALSE;
    m_bCSILastWasSeparator = FALSE;
    m_SpinLock.Release();
}

void CTRenderer::BeginCSI(void)
{
    m_State = StateCSI;
    m_bCSIPrivate = FALSE;
    m_nCSIParamCount = 0;
    m_nCSIParamValue = 0;
    m_bCSIHaveValue = FALSE;
    m_bCSILastWasSeparator = FALSE;
}

void CTRenderer::CSIAddParam(unsigned value)
{
    if (m_nCSIParamCount >= CSIParamMax)
    {
        return;
    }
    m_CSIParams[m_nCSIParamCount++] = value;
}

void CTRenderer::FinalizeCSIParams(void)
{
    if (m_bCSIHaveValue)
    {
        CSIAddParam(m_nCSIParamValue);
    }
    else if (m_bCSILastWasSeparator)
    {
        CSIAddParam(0);
    }
    m_nCSIParamValue = 0;
    m_bCSIHaveValue = FALSE;
    m_bCSILastWasSeparator = FALSE;
}

void CTRenderer::InsertChars(unsigned nCount)
{
    if (nCount == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
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

    for (unsigned y = startY; y < endY; ++y)
    {
        for (int x = static_cast<int>(m_nUsedWidth) - 1; x >= static_cast<int>(m_nCursorX + pixelWidth); --x)
        {
            SetRawPixel(static_cast<unsigned>(x), y, GetRawPixel(static_cast<unsigned>(x) - pixelWidth, y));
        }

        const unsigned clearEnd = m_nCursorX + pixelWidth;
        for (unsigned x = m_nCursorX; x < clearEnd && x < m_nUsedWidth; ++x)
        {
            SetRawPixel(x, y, bgColor);
        }
    }

    const unsigned row = GetRowIndexFromY(m_nCursorY);
    const unsigned startColumn = GetColumnIndexFromX(m_nCursorX, m_nCursorY);
    ShiftShadowCellsRight(row, startColumn, pixelWidth / charWidth);

    SetUpdateArea(startY, endY - 1);
}

void CTRenderer::ResetTerminalState(boolean clearScreen)
{
    // NOTE: This is called from the write path while the renderer spinlock is held.
    // Do not call ResetParserState() here (it acquires the same spinlock).
    m_State = StateStart;
    m_nParam1 = 0;
    m_nParam2 = 0;
    m_bCSIPrivate = FALSE;
    m_nCSIParamCount = 0;
    m_nCSIParamValue = 0;
    m_bCSIHaveValue = FALSE;
    m_bCSILastWasSeparator = FALSE;

    m_bVT52Mode = FALSE;
    m_bOriginMode = FALSE;
    m_bInsertOn = FALSE;
    m_bNewLineMode = FALSE;
    m_bAutoPage = FALSE;
    SetWrapAroundMode(TRUE);

    m_G0CharSet = CharSetUS;
    m_G1CharSet = CharSetGraphics;
    m_bUseG1 = FALSE;

    SetStandoutMode(0);
    SetScrollRegion(1, 0);
    SetCursorMode(TRUE);

    if (m_pCharGen != nullptr)
    {
        SetFont(m_CurrentFontSelection, CCharGenerator::FontFlagsNone);
    }

    ResetLineAttributes();

    CursorHome();
    if (clearScreen)
    {
        ClearDisplay();
    }
}

void CTRenderer::ScreenAlignmentTest(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    ResetLineAttributes();

    const unsigned savedX = m_nCursorX;
    const unsigned savedY = m_nCursorY;

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charWidth == 0 || charHeight == 0)
    {
        return;
    }

    const unsigned cols = m_nUsedWidth / charWidth;
    const unsigned rows = m_nUsedHeight / charHeight;
    if (cols == 0 || rows == 0)
    {
        return;
    }

    const CDisplay::TRawColor color = GetTextColor();
    for (unsigned row = 0; row < rows; ++row)
    {
        for (unsigned col = 0; col < cols; ++col)
        {
            DisplayChar('E', col * charWidth, row * charHeight, color);
        }
    }

    m_nCursorX = savedX;
    m_nCursorY = savedY;
}

inline void CTRenderer::SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
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

inline CDisplay::TRawColor CTRenderer::GetRawPixel(unsigned nPosX, unsigned nPosY)
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
    if (m_State != StateStart && chChar == '\x1b')
    {
        m_State = StateEscape;
        m_nParam1 = 0;
        m_nParam2 = 0;
        m_bCSIPrivate = FALSE;
        m_nCSIParamCount = 0;
        m_nCSIParamValue = 0;
        m_bCSIHaveValue = FALSE;
        m_bCSILastWasSeparator = FALSE;
        return;
    }

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
            // VT100: LF is IND (index) -> move down, keep column.
            if (m_bNewLineMode)
            {
                NewLine();
            }
            else
            {
                IndexDown();
            }
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

        case '\x9B':
            // 8-bit C1 CSI (equivalent to ESC '[')
            BeginCSI();
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
                BeginCSI();
                break;

            case 'Z':
                // DECID (identify terminal)
                SendPrimaryDA();
                m_State = StateStart;
                break;

            case 'c':
                // RIS (Reset to Initial State)
                ResetTerminalState(TRUE);
                m_State = StateStart;
                break;

            case 'D':
                // IND
                IndexDown();
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

            case '=':
            case '>':
                // DECKPAM/DECKPNM (application/numeric keypad) - ignore.
                m_State = StateStart;
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
        {
            const unsigned row = GetRowIndexFromY(m_nCursorY);
            const ELineAttribute previousAttribute = GetLineAttributeForRow(row);
            SetLineAttributeForRow(row, LineAttributeDoubleHeightTop);
            RenderShadowRow(row);
            RecomputeCursorXForCurrentLine(previousAttribute);
            m_State = StateStart;
            break;
        }
        case '4':
        {
            const unsigned row = GetRowIndexFromY(m_nCursorY);
            const ELineAttribute previousAttribute = GetLineAttributeForRow(row);
            SetLineAttributeForRow(row, LineAttributeDoubleHeightBottom);
            RenderShadowRow(row);
            RecomputeCursorXForCurrentLine(previousAttribute);
            m_State = StateStart;
            break;
        }
        case '5':
        {
            const unsigned row = GetRowIndexFromY(m_nCursorY);
            const ELineAttribute previousAttribute = GetLineAttributeForRow(row);
            SetLineAttributeForRow(row, LineAttributeNormal);
            RenderShadowRow(row);
            RecomputeCursorXForCurrentLine(previousAttribute);
            m_State = StateStart;
            break;
        }
        case '6':
        {
            const unsigned row = GetRowIndexFromY(m_nCursorY);
            const ELineAttribute previousAttribute = GetLineAttributeForRow(row);
            SetLineAttributeForRow(row, LineAttributeDoubleWidth);
            RenderShadowRow(row);
            RecomputeCursorXForCurrentLine(previousAttribute);
            m_State = StateStart;
            break;
        }
        case '8':
            // DECALN: Screen alignment test pattern
            ScreenAlignmentTest();
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

    case StateCSI:
        switch (chChar)
        {
        case '\b':
            CursorLeft();
            break;

        case '\t':
            Tabulator();
            break;

        case '\v':
        case '\f':
        case '\n':
            if (m_bNewLineMode)
            {
                NewLine();
            }
            else
            {
                IndexDown();
            }
            break;

        case '\r':
            CarriageReturn();
            break;

        case '\x0E':
            m_bUseG1 = TRUE;
            break;

        case '\x0F':
            m_bUseG1 = FALSE;
            break;

        default:
            break;
        }

        if (static_cast<unsigned char>(chChar) < 0x20U && chChar != '\x1B')
        {
            break;
        }

        if (chChar == '?')
        {
            m_bCSIPrivate = TRUE;
            break;
        }

        if ('0' <= chChar && chChar <= '9')
        {
            m_bCSIHaveValue = TRUE;
            m_bCSILastWasSeparator = FALSE;
            m_nCSIParamValue *= 10;
            m_nCSIParamValue += static_cast<unsigned>(chChar - '0');
            if (m_nCSIParamValue > 9999)
            {
                // avoid pathological input
                m_State = StateStart;
                m_bCSIPrivate = FALSE;
                m_nCSIParamCount = 0;
                m_nCSIParamValue = 0;
                m_bCSIHaveValue = FALSE;
                m_bCSILastWasSeparator = FALSE;
            }
            break;
        }

        if (chChar == ';')
        {
            if (m_bCSIHaveValue)
            {
                CSIAddParam(m_nCSIParamValue);
            }
            else
            {
                CSIAddParam(0);
            }
            m_nCSIParamValue = 0;
            m_bCSIHaveValue = FALSE;
            m_bCSILastWasSeparator = TRUE;
            break;
        }

        FinalizeCSIParams();

        switch (chChar)
        {
        case 'A':
        {
            const unsigned n = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1);
            for (unsigned i = 0; i < n; ++i)
            {
                CursorUp();
            }
            break;
        }
        case 'B':
        {
            const unsigned n = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1);
            for (unsigned i = 0; i < n; ++i)
            {
                CursorDown();
            }
            break;
        }
        case 'C':
        {
            const unsigned n = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1);
            for (unsigned i = 0; i < n; ++i)
            {
                CursorRight();
            }
            break;
        }
        case 'D':
        {
            const unsigned n = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1);
            for (unsigned i = 0; i < n; ++i)
            {
                CursorLeft();
            }
            break;
        }
        case 'H':
        case 'f':
        {
            const unsigned row = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1);
            const unsigned col = CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 1, 1);
            CursorMove(row, col);
            break;
        }
        case 'J':
        {
            const unsigned mode = (m_nCSIParamCount > 0) ? m_CSIParams[0] : 0;
            if (mode == 0)
            {
                ClearDisplayEnd();
            }
            else if (mode == 1)
            {
                ClearDisplayStart();
            }
            else if (mode == 2)
            {
                const unsigned savedX = m_nCursorX;
                const unsigned savedY = m_nCursorY;
                ClearDisplay();
                m_nCursorX = savedX;
                m_nCursorY = savedY;
            }
            else
            {
                ClearDisplay();
            }
            break;
        }
        case 'K':
        {
            const unsigned mode = (m_nCSIParamCount > 0) ? m_CSIParams[0] : 0;
            if (mode == 0)
            {
                ClearLineEnd();
            }
            else if (mode == 1)
            {
                ClearLineStart();
            }
            else if (mode == 2)
            {
                ClearLine();
            }
            else
            {
                ClearLineEnd();
            }
            break;
        }
        case 'L':
            InsertLines(CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1));
            break;

        case 'M':
            DeleteLines(CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1));
            break;

        case 'P':
            DeleteChars(CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1));
            break;

        case 'X':
            EraseChars(CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1));
            break;

        case '@':
            // ICH: insert blank chars
            InsertChars(CSIParamOrDefault(m_CSIParams, m_nCSIParamCount, 0, 1));
            break;

        case 'r':
        {
            const unsigned top = (m_nCSIParamCount > 0) ? m_CSIParams[0] : 1;
            const unsigned bottom = (m_nCSIParamCount > 1) ? m_CSIParams[1] : 0;
            SetScrollRegion(top, bottom);
            break;
        }
        case 's':
            SaveCursor();
            break;

        case 'u':
            RestoreCursor();
            break;

        case 'c':
            SendPrimaryDA();
            break;

        case 'n':
        {
            const unsigned code = (m_nCSIParamCount > 0) ? m_CSIParams[0] : 0;
            if (code == 5)
            {
                static const char Reply[] = "\x1B[0n";
                SendHostReply(Reply, sizeof Reply - 1);
            }
            else if (code == 6)
            {
                unsigned row = 1;
                unsigned col = 1;

                if (m_pCharGen != nullptr)
                {
                    const unsigned charWidth = m_pCharGen->GetCharWidth();
                    const unsigned charHeight = m_pCharGen->GetCharHeight();
                    if (charWidth != 0)
                    {
                        col = (m_nCursorX / charWidth) + 1U;
                    }
                    if (charHeight != 0)
                    {
                        const unsigned baseY = (m_bOriginMode != FALSE) ? m_nScrollStart : 0U;
                        const unsigned effectiveY = (m_nCursorY >= baseY) ? (m_nCursorY - baseY) : m_nCursorY;
                        row = (effectiveY / charHeight) + 1U;
                    }
                }

                CString reply;
                reply.Format("\x1B[%u;%uR", row, col);
                SendHostReply(reply.c_str(), reply.GetLength());
            }
            break;
        }

        case 'g':
        {
            const unsigned mode = (m_nCSIParamCount > 0) ? m_CSIParams[0] : 0;
            CTConfig *config = CTConfig::Get();
            if (config != nullptr)
            {
                if (mode == 0)
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
                else if (mode == 3)
                {
                    for (unsigned col = 0; col < CTConfig::TabStopsMax; ++col)
                    {
                        config->SetTabStop(col, false);
                    }
                }
            }
            break;
        }

        case 'Z':
            BackTabulator();
            break;

        case 'm':
        {
            if (m_nCSIParamCount == 0)
            {
                SetStandoutMode(0);
            }
            else
            {
                for (unsigned i = 0; i < m_nCSIParamCount; ++i)
                {
                    SetStandoutMode(m_CSIParams[i]);
                }
            }
            break;
        }

        case 'h':
        case 'l':
        {
            const bool enable = (chChar == 'h');

            const unsigned count = (m_nCSIParamCount != 0) ? m_nCSIParamCount : 1U;
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned mode = (m_nCSIParamCount != 0) ? m_CSIParams[i] : 0U;

                if (m_bCSIPrivate)
                {
                    if (mode == 25)
                    {
                        SetCursorMode(enable ? TRUE : FALSE);
                    }
                    else if (mode == 2)
                    {
                        // VT52 mode toggle is historically mapped here in this project.
                        // Only `?2l` is used to enter VT52; `ESC <` exits.
                        if (!enable)
                        {
                            m_bVT52Mode = TRUE;
                        }
                    }
                    else if (mode == 6)
                    {
                        m_bOriginMode = enable ? TRUE : FALSE;
                        CursorHome();
                    }
                    else if (mode == 7)
                    {
                        SetWrapAroundMode(enable ? TRUE : FALSE);
                    }
                    else if (mode == 3)
                    {
                        ApplyColumnMode(enable ? 132U : 80U, TRUE);
                    }
                    else if (mode == 1)
                    {
                        // DECCKM cursor key mode: ignore (keyboard handles sequences).
                    }
                    else if (mode == 2004)
                    {
                        // Bracketed paste mode: ignore.
                    }
                    else if (mode == 47 || mode == 1047 || mode == 1049)
                    {
                        ClearDisplay();
                        if (enable)
                        {
                            EnterAlternateScreen();
                        }
                        else
                        {
                            LeaveAlternateScreen();
                        }
                    }
                }
                else
                {
                    if (mode == 4)
                    {
                        InsertMode(enable ? TRUE : FALSE);
                    }
                    else if (mode == 20)
                    {
                        // ANSI New Line Mode (LNM): LF == CR+LF when enabled.
                        m_bNewLineMode = enable ? TRUE : FALSE;
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        // Reset CSI parser
        m_State = StateStart;
        m_bCSIPrivate = FALSE;
        m_nCSIParamCount = 0;
        m_nCSIParamValue = 0;
        m_bCSIHaveValue = FALSE;
        m_bCSILastWasSeparator = FALSE;
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

        case ';':
            // CUP with missing first parameter (e.g. ESC[;10H).
            m_nParam1 = 1;
            m_State = StateSemicolon;
            break;

        case 'c':
            // Primary device attributes
            SendPrimaryDA();
            m_State = StateStart;
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

        case 's':
            SaveCursor();
            m_State = StateStart;
            break;

        case 'u':
            RestoreCursor();
            m_State = StateStart;
            break;

        case 'r':
            // Reset scroll region (ESC[r defaults to full screen).
            SetScrollRegion(1, 0);
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
            else if (m_nParam1 == 1)
            {
                ClearDisplayStart();
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

        case 'K':
            if (m_nParam1 == 0)
            {
                ClearLineEnd();
            }
            else if (m_nParam1 == 1)
            {
                ClearLineStart();
            }
            else if (m_nParam1 == 2)
            {
                ClearLine();
            }
            else
            {
                ClearLineEnd();
            }
            m_State = StateStart;
            break;

        case 'h':
        case 'l':
            if (m_nParam1 == 4)
            {
                InsertMode(chChar == 'h');
            }
            else if (m_nParam1 == 20)
            {
                // ANSI New Line Mode (LNM): LF == CR+LF when enabled.
                m_bNewLineMode = (chChar == 'h') ? TRUE : FALSE;
            }
            m_State = StateStart;
            break;

        case 'm':
            SetStandoutMode(m_nParam1);
            m_State = StateStart;
            break;

        case 'c':
            // Primary device attributes
            SendPrimaryDA();
            m_State = StateStart;
            break;

        case 'n':
        {
            // Device status report (DSR)
            // 5 -> "OK", 6 -> cursor position report
            if (m_nParam1 == 5)
            {
                static const char Reply[] = "\x1B[0n";
                SendHostReply(Reply, sizeof Reply - 1);
            }
            else if (m_nParam1 == 6)
            {
                unsigned row = 1;
                unsigned col = 1;

                if (m_pCharGen != nullptr)
                {
                    const unsigned charWidth = m_pCharGen->GetCharWidth();
                    const unsigned charHeight = m_pCharGen->GetCharHeight();
                    if (charWidth != 0)
                    {
                        col = (m_nCursorX / charWidth) + 1U;
                    }
                    if (charHeight != 0)
                    {
                        row = (m_nCursorY / charHeight) + 1U;
                    }
                }

                CString reply;
                reply.Format("\x1B[%u;%uR", row, col);
                SendHostReply(reply.c_str(), reply.GetLength());
            }

            m_State = StateStart;
            break;
        }

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
            else if (m_nParam1 == 3)
            {
                ApplyColumnMode(132U, TRUE);
            }
            else if (m_nParam1 == 6)
            {
                m_bOriginMode = TRUE;
                CursorHome();
            }
            else if (m_nParam1 == 7)
            {
                // DECAWM: auto wrap mode
                SetWrapAroundMode(TRUE);
            }
            else if (m_nParam1 == 2004)
            {
                // Bracketed paste mode (xterm/zsh): ignore.
            }
            else if (m_nParam1 == 47 || m_nParam1 == 1047 || m_nParam1 == 1049)
            {
                EnterAlternateScreen();
            }
            m_State = StateStart;
            break;

        case 'l':
            if (m_nParam1 == 25)
            {
                SetCursorMode(FALSE);
            }
            else if (m_nParam1 == 3)
            {
                ApplyColumnMode(80U, TRUE);
            }
            else if (m_nParam1 == 2)
            {
                m_bVT52Mode = TRUE;
            }
            else if (m_nParam1 == 6)
            {
                m_bOriginMode = FALSE;
                CursorHome();
            }
            else if (m_nParam1 == 7)
            {
                // DECAWM: auto wrap mode
                SetWrapAroundMode(FALSE);
            }
            else if (m_nParam1 == 2004)
            {
                // Bracketed paste mode (xterm/zsh): ignore.
            }
            else if (m_nParam1 == 47 || m_nParam1 == 1047 || m_nParam1 == 1049)
            {
                LeaveAlternateScreen();
            }
            m_State = StateStart;
            break;

        default:
            if ('0' <= chChar && chChar <= '9')
            {
                m_nParam1 *= 10;
                m_nParam1 += chChar - '0';

                if (m_nParam1 > 9999)
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
    m_bWrapPending = FALSE;
}

void CTRenderer::ClearDisplay(void)
{
    m_nCursorX = 0;
    m_nCursorY = 0;
    m_bWrapPending = FALSE;
    ResetLineAttributes();
    ResetShadowBuffer(GetActiveShadowCells());
    ClearDisplayEnd();
}

void CTRenderer::ClearDisplayStart(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned savedX = m_nCursorX;
    const unsigned savedY = m_nCursorY;

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    // Clear full lines above the cursor.
    for (unsigned y = 0; y < savedY; y += charHeight)
    {
        m_nCursorX = 0;
        m_nCursorY = y;
        ClearLineEnd();
        SetLineAttributeForRow(GetRowIndexFromY(y), LineAttributeNormal);
        ResetShadowRow(GetRowIndexFromY(y));
    }

    // Clear from start of line to cursor on the cursor line.
    m_nCursorY = savedY;
    m_nCursorX = savedX;
    ClearLineStart();

    m_nCursorX = savedX;
    m_nCursorY = savedY;
}

void CTRenderer::ClearLineStart(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    if (charWidth == 0)
    {
        return;
    }

    if (m_nUsedWidth < charWidth)
    {
        return;
    }

    unsigned endX = m_nCursorX;
    const unsigned maxX = m_nUsedWidth - charWidth;
    if (endX > maxX)
    {
        endX = maxX;
    }

    for (unsigned nPosX = 0; nPosX <= endX; nPosX += charWidth)
    {
        EraseChar(nPosX, m_nCursorY);
    }

    const unsigned row = GetRowIndexFromY(m_nCursorY);
    const unsigned endColumn = GetColumnIndexFromX(endX, m_nCursorY) + 1;
    ClearShadowCells(row, 0, endColumn);
}

void CTRenderer::ClearLine(void)
{
    const unsigned savedX = m_nCursorX;
    m_nCursorX = 0;
    ClearLineEnd();
    ResetShadowRow(GetRowIndexFromY(m_nCursorY));
    m_nCursorX = savedX;
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

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight != 0)
    {
        const unsigned startRow = GetRowIndexFromY(m_nCursorY + charHeight);
        const unsigned rowCount = GetRowCount();
        for (unsigned row = startRow; row < rowCount; ++row)
        {
            SetLineAttributeForRow(row, LineAttributeNormal);
            ResetShadowRow(row);
        }
    }

    const unsigned cursorRow = GetRowIndexFromY(m_nCursorY);
    ClearShadowCells(cursorRow, GetColumnIndexFromX(m_nCursorX, m_nCursorY), GetColumnsForY(m_nCursorY));

    SetUpdateArea(m_nCursorY, m_nHeight - 1);
}

void CTRenderer::ClearLineEnd(void)
{
    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    if (charWidth == 0)
    {
        return;
    }

    for (unsigned nPosX = m_nCursorX; nPosX < m_nUsedWidth; nPosX += charWidth)
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

    const unsigned row = GetRowIndexFromY(m_nCursorY);
    ClearShadowCells(row, GetColumnIndexFromX(m_nCursorX, m_nCursorY), GetColumnsForY(m_nCursorY));
}

void CTRenderer::CursorDown(void)
{
    m_bWrapPending = FALSE;
    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    const unsigned lastRowY = (m_nUsedHeight >= charHeight) ? (m_nUsedHeight - charHeight) : 0U;
    if (m_nCursorY + charHeight <= lastRowY)
    {
        m_nCursorY += charHeight;
    }

    ClampCursorToLineWidth();
}

void CTRenderer::CursorHome(void)
{
    m_nCursorX = 0;
    m_nCursorY = m_bOriginMode ? m_nScrollStart : 0;
    m_bWrapPending = FALSE;
}

void CTRenderer::CursorLeft(void)
{
    m_bWrapPending = FALSE;
    const unsigned row = GetRowIndexFromY(m_nCursorY);
    const ELineAttribute attribute = GetLineAttributeForY(m_nCursorY);
    const bool traceDwdhRow = row == 11 || row == 12;
    if (traceDwdhRow)
    {
        LOGNOTE("DWDH CursorLeft before: row=%u x=%u attr=%u", row + 1, m_nCursorX, static_cast<unsigned>(attribute));
    }

    if (m_nCursorX > 0)
    {
        const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
        if (m_nCursorX >= charWidth)
        {
            m_nCursorX -= charWidth;
        }
        else
        {
            m_nCursorX = 0;
        }
    }

    if (traceDwdhRow)
    {
        LOGNOTE("DWDH CursorLeft after: row=%u x=%u attr=%u", row + 1, m_nCursorX, static_cast<unsigned>(GetLineAttributeForY(m_nCursorY)));
    }
}

void CTRenderer::CursorMove(unsigned nRow, unsigned nColumn)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    m_bWrapPending = FALSE;

    if (nRow == 0)
    {
        nRow = 1;
    }
    if (nColumn == 0)
    {
        nColumn = 1;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    const unsigned baseY = m_bOriginMode ? m_nScrollStart : 0;
    unsigned row = nRow;
    if (m_bOriginMode)
    {
        const unsigned scrollHeight = (m_nScrollEnd > m_nScrollStart) ? (m_nScrollEnd - m_nScrollStart) : 0;
        const unsigned maxRows = (scrollHeight / charHeight);
        if (maxRows != 0 && row > maxRows)
        {
            row = maxRows;
        }
    }
    else
    {
        const unsigned maxRows = m_nUsedHeight / charHeight;
        if (maxRows != 0 && row > maxRows)
        {
            row = maxRows;
        }
    }

    const unsigned nPosY = baseY + ((row - 1) * charHeight);
    const unsigned charWidth = GetCharCellWidthForY(nPosY);
    const unsigned maxColumns = GetColumnsForY(nPosY);
    if (charWidth == 0 || maxColumns == 0)
    {
        return;
    }
    if (nColumn > maxColumns)
    {
        nColumn = maxColumns;
    }

    const unsigned nPosX = (nColumn - 1) * charWidth;

    // Cursor positions are clamped to the visible grid and remain cell-aligned.
    m_nCursorX = nPosX;
    m_nCursorY = nPosY;

    const unsigned targetRow = GetRowIndexFromY(m_nCursorY);
    if (targetRow == 11 || targetRow == 12)
    {
        LOGNOTE("DWDH CursorMove: row=%u col=%u x=%u attr=%u", targetRow + 1, nColumn, m_nCursorX, static_cast<unsigned>(GetLineAttributeForY(m_nCursorY)));
    }
}

void CTRenderer::CursorRight(void)
{
    m_bWrapPending = FALSE;

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    if (charWidth == 0 || m_nUsedWidth < charWidth)
    {
        return;
    }

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

void CTRenderer::CursorUp(void)
{
    m_bWrapPending = FALSE;
    if (m_nCursorY > m_nScrollStart)
    {
        m_nCursorY -= m_pCharGen->GetCharHeight();
        ClampCursorToLineWidth();
    }
}

void CTRenderer::DeleteChars(unsigned nCount) // TODO
{
    if (nCount == 0 || m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
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

    const unsigned row = GetRowIndexFromY(m_nCursorY);
    const unsigned startColumn = GetColumnIndexFromX(m_nCursorX, m_nCursorY);
    ShiftShadowCellsLeft(row, startColumn, pixelWidth / charWidth);

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
    const unsigned startRow = GetRowIndexFromY(m_nCursorY);
    const unsigned endRow = m_nScrollEnd / charHeight;
    ShiftLineAttributesUp(startRow, endRow, nCount);
    ShiftShadowRowsUp(startRow, endRow, nCount);

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
        if (m_bInsertOn)
        {
            InsertChars(1);
        }

        const bool wrapAroundEnabled = (m_bWrapAroundMode != FALSE);

        // VT100 DECAWM semantics: when a char is printed in the last column,
        // the terminal sets a pending wrap state, but does not move to the next
        // line until the *next* printable character arrives.
        if (wrapAroundEnabled && m_bWrapPending)
        {
            m_bWrapPending = FALSE;
            NewLine();
        }
        else if (!wrapAroundEnabled)
        {
            m_bWrapPending = FALSE;
        }

        ECharacterSet activeSet = m_bUseG1 ? m_G1CharSet : m_G0CharSet;
        bool bUseGraphics = (activeSet == CharSetGraphics) &&
                            (unsigned char)chChar >= 0x60 && (unsigned char)chChar <= 0x7E;

        CCharGenerator *pOriginalGen = nullptr;
        if (bUseGraphics && m_pGraphicsCharGen != nullptr)
        {
            pOriginalGen = m_pCharGen;
            m_pCharGen = m_pGraphicsCharGen;
        }

        const unsigned activeCharSet = static_cast<unsigned>(activeSet);
        const CDisplay::TRawColor foreground = GetTextColor();
        const CDisplay::TRawColor background = GetTextBackgroundColor();
        const unsigned row = GetRowIndexFromY(m_nCursorY);
        const ELineAttribute attribute = GetLineAttributeForY(m_nCursorY);
        if ((row == 11 || row == 12) && chChar == '*')
        {
            LOGNOTE("DWDH write: row=%u x=%u attr=%u charset=%u", row + 1, m_nCursorX, static_cast<unsigned>(attribute), activeCharSet);
        }
        DisplayChar(chChar, m_nCursorX, m_nCursorY, foreground);
        StoreShadowCellAt(m_nCursorX, m_nCursorY, chChar, foreground, background, activeCharSet);

        if (pOriginalGen != nullptr)
        {
            m_pCharGen = pOriginalGen;
        }

        const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
        if (charWidth != 0 && m_nUsedWidth >= charWidth)
        {
            const unsigned lastColumnX = m_nUsedWidth - charWidth;
            if (wrapAroundEnabled)
            {
                if (m_nCursorX < lastColumnX)
                {
                    m_nCursorX += charWidth;
                }
                else
                {
                    m_nCursorX = lastColumnX;
                    m_bWrapPending = TRUE;
                }
            }
            else
            {
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

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    unsigned nEndX = m_nCursorX + nCount * charWidth;
    if (nEndX > m_nUsedWidth)
    {
        nEndX = m_nUsedWidth;
    }

    for (unsigned nPosX = m_nCursorX; nPosX < nEndX; nPosX += charWidth)
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
    const unsigned startRow = GetRowIndexFromY(m_nCursorY);
    const unsigned endRow = m_nScrollEnd / charHeight;
    ShiftLineAttributesDown(startRow, endRow, nCount);
    ShiftShadowRowsDown(startRow, endRow, nCount);

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
    IndexDown();
}

void CTRenderer::IndexDown(void)
{
    m_bWrapPending = FALSE;

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    m_nCursorY += charHeight;
    if (m_nCursorY >= m_nScrollEnd)
    {
        if (!m_bAutoPage)
        {
            Scroll();
            m_nCursorY -= charHeight;
        }
        else
        {
            m_nCursorY = m_nScrollStart;
        }
    }

    ClampCursorToLineWidth();
}

void CTRenderer::ReverseScroll(void)
{
    m_bWrapPending = FALSE;

    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    if (m_nCursorY > m_nScrollStart)
    {
        m_nCursorY -= charHeight;
        ClampCursorToLineWidth();
        return;
    }

    if (m_nCursorY == m_nScrollStart)
    {
        InsertLines(1);
        ClampCursorToLineWidth();
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

void CTRenderer::SetWrapAroundMode(boolean bEnable)
{
    m_bWrapAroundMode = bEnable;
    m_bWrapPending = FALSE;
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
    if (m_pCharGen == nullptr)
    {
        return;
    }

    m_bWrapPending = FALSE;

    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charHeight == 0)
    {
        return;
    }

    const unsigned totalRows = m_nUsedHeight / charHeight;
    if (nStartRow == 0)
    {
        nStartRow = 1;
    }
    if (nEndRow == 0)
    {
        nEndRow = totalRows;
    }

    // VT100-style clamping: callers may send large values (e.g. 999) to mean
    // "bottom of screen". Do not reject such sequences.
    if (totalRows != 0)
    {
        if (nStartRow > totalRows)
        {
            nStartRow = totalRows;
        }
        if (nEndRow > totalRows)
        {
            nEndRow = totalRows;
        }
    }

    // If the region becomes invalid, reset to full screen.
    if (nStartRow >= nEndRow)
    {
        nStartRow = 1;
        nEndRow = totalRows;
    }

    unsigned nScrollStart = (nStartRow - 1) * charHeight;
    unsigned nScrollEnd = nEndRow * charHeight;

    if (nScrollStart < m_nUsedHeight && nScrollEnd > 0 && nScrollEnd <= m_nUsedHeight && nScrollStart < nScrollEnd)
    {
        m_nScrollStart = nScrollStart;
        m_nScrollEnd = nScrollEnd;
    }

    CursorHome();
}

// Intensity modes are mutually exclusive; the remaining mono attributes may be combined.
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
        m_bDimAttribute = FALSE;
        break;

    case 2: // dim / half-bright
        m_bBoldAttribute = FALSE;
        m_bDimAttribute = TRUE;
        break;

    case 22: // normal intensity
        m_bBoldAttribute = FALSE;
        m_bDimAttribute = FALSE;
        break;

    case 4: // underlined - change glyph rendering
        m_bUnderlineAttribute = TRUE;

        break;

    case 24: // underline off
        m_bUnderlineAttribute = FALSE;
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

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
    if (charWidth == 0)
    {
        return;
    }

    const unsigned currentCol = m_nCursorX / charWidth;
    const unsigned cols = GetColumns();
    const unsigned row = GetRowIndexFromY(m_nCursorY);
    if (row == 11 || row == 12)
    {
        LOGNOTE("DWDH Tab before: row=%u x=%u col=%u attr=%u", row + 1, m_nCursorX, currentCol + 1, static_cast<unsigned>(GetLineAttributeForY(m_nCursorY)));
    }

    CTConfig *config = CTConfig::Get();
    if (config != nullptr && cols > 0)
    {
        for (unsigned col = currentCol + 1; col < cols; ++col)
        {
            if (config->IsTabStop(col))
            {
                m_nCursorX = col * charWidth;
                if (row == 11 || row == 12)
                {
                    LOGNOTE("DWDH Tab after-stop: row=%u x=%u col=%u attr=%u", row + 1, m_nCursorX, col + 1, static_cast<unsigned>(GetLineAttributeForY(m_nCursorY)));
                }
                return;
            }
        }
    }

    unsigned nTabWidth = charWidth * 8;
    m_nCursorX = ((m_nCursorX + nTabWidth) / nTabWidth) * nTabWidth;
    if (m_nCursorX >= m_nUsedWidth)
    {
        m_nCursorX = (m_nUsedWidth >= charWidth) ? (m_nUsedWidth - charWidth) : 0;
    }

    if (row == 11 || row == 12)
    {
        LOGNOTE("DWDH Tab after-fallback: row=%u x=%u col=%u attr=%u", row + 1, m_nCursorX, (charWidth != 0 ? (m_nCursorX / charWidth) + 1 : 0), static_cast<unsigned>(GetLineAttributeForY(m_nCursorY)));
    }
}

void CTRenderer::BackTabulator(void)
{
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = GetCharCellWidthForY(m_nCursorY);
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
    // NOTE: Called from the write path while the renderer spinlock is held.
    // Do not acquire m_SpinLock here.
    m_SavedState.cursorX = m_nCursorX;
    m_SavedState.cursorY = m_nCursorY;
    m_SavedState.vt52Mode = m_bVT52Mode;
    m_SavedState.originMode = m_bOriginMode;
    m_SavedState.wrapAroundMode = m_bWrapAroundMode;
    m_SavedState.g0CharSet = static_cast<unsigned>(m_G0CharSet);
    m_SavedState.g1CharSet = static_cast<unsigned>(m_G1CharSet);
    m_SavedState.useG1 = m_bUseG1;
    m_SavedState.insertOn = m_bInsertOn;
    m_SavedState.autoPage = m_bAutoPage;
    m_SavedState.reverseAttribute = m_bReverseAttribute;
    m_SavedState.boldAttribute = m_bBoldAttribute;
    m_SavedState.dimAttribute = m_bDimAttribute;
    m_SavedState.underlineAttribute = m_bUnderlineAttribute;
    m_SavedState.blinkAttribute = m_bBlinkAttribute;
    m_SavedState.foreground = m_ForegroundColor;
    m_SavedState.background = m_BackgroundColor;
    m_SavedState.defaultForeground = m_DefaultForegroundColor;
    m_SavedState.defaultBackground = m_DefaultBackgroundColor;
    m_SavedState.fontFlags = m_FontFlags;

    // Clear transient wrap state so restore behaves deterministically.
    m_bWrapPending = FALSE;
}

void CTRenderer::RestoreCursor(void)
{
    // NOTE: Called from the write path while the renderer spinlock is held.
    // Do not acquire m_SpinLock here.
    if (m_pCharGen == nullptr)
    {
        return;
    }

    const unsigned charWidth = m_pCharGen->GetCharWidth();
    const unsigned charHeight = m_pCharGen->GetCharHeight();
    if (charWidth == 0 || charHeight == 0)
    {
        return;
    }

    // Restore modes (DECSC/DECRC compatibility for vttest)
    m_bVT52Mode = m_SavedState.vt52Mode;
    m_bOriginMode = m_SavedState.originMode;
    SetWrapAroundMode(m_SavedState.wrapAroundMode);
    m_bInsertOn = m_SavedState.insertOn;
    m_bAutoPage = m_SavedState.autoPage;
    m_G0CharSet = static_cast<ECharacterSet>(m_SavedState.g0CharSet);
    m_G1CharSet = static_cast<ECharacterSet>(m_SavedState.g1CharSet);
    m_bUseG1 = m_SavedState.useG1;

    // Restore position, clamped to current screen dimensions
    const unsigned lastColumnX = (m_nUsedWidth >= charWidth) ? (m_nUsedWidth - charWidth) : 0U;
    const unsigned lastRowY = (m_nUsedHeight >= charHeight) ? (m_nUsedHeight - charHeight) : 0U;
    m_nCursorX = (m_SavedState.cursorX <= lastColumnX) ? m_SavedState.cursorX : lastColumnX;
    m_nCursorY = (m_SavedState.cursorY <= lastRowY) ? m_SavedState.cursorY : lastRowY;

    // Ensure cell alignment
    m_nCursorX = (charWidth != 0) ? ((m_nCursorX / charWidth) * charWidth) : m_nCursorX;
    m_nCursorY = (charHeight != 0) ? ((m_nCursorY / charHeight) * charHeight) : m_nCursorY;

    // Restore attributes
    m_bReverseAttribute = m_SavedState.reverseAttribute;
    m_bBoldAttribute = m_SavedState.boldAttribute;
    m_bDimAttribute = m_SavedState.dimAttribute;
    m_bUnderlineAttribute = m_SavedState.underlineAttribute;
    m_bBlinkAttribute = m_SavedState.blinkAttribute;

    m_ForegroundColor = m_SavedState.foreground;
    m_BackgroundColor = m_SavedState.background;
    m_DefaultForegroundColor = m_SavedState.defaultForeground;
    m_DefaultBackgroundColor = m_SavedState.defaultBackground;

    // Note: We don't restore the font itself, as that might require loading resources,
    // but we can restore flags if matched. To be safe, we usually only restore
    // attributes that don't change resource allocation.

    m_bWrapPending = FALSE;
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
    ShiftLineAttributesUp(m_nScrollStart / nLines, m_nScrollEnd / nLines, 1);
    ShiftShadowRowsUp(m_nScrollStart / nLines, m_nScrollEnd / nLines, 1);

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
        // Overstrike once to simulate a thick stroke
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
    const unsigned cellWidth = GetCharCellWidthForY(nPosY);
    const unsigned cellHeight = GetBaseCharHeight();
    for (unsigned y = 0; y < cellHeight; y++)
    {
        for (unsigned x = 0; x < cellWidth; x++)
        {
            SetRawPixel(nPosX + x, nPosY + y, m_BackgroundColor);
        }
    }

    SetUpdateArea(nPosY, nPosY + cellHeight - 1);
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

    SetUpdateArea(m_nCursorY + y0, m_nCursorY + cursorHeight - 1);
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
    state.dimAttribute = m_bDimAttribute;
    state.underlineAttribute = m_bUnderlineAttribute;
    state.blinkAttribute = m_bBlinkAttribute;
    state.insertOn = m_bInsertOn;
    state.autoPage = m_bAutoPage;
    state.vt52Mode = m_bVT52Mode;
    state.originMode = m_bOriginMode;
    state.wrapAroundMode = m_bWrapAroundMode;
    state.delayedUpdate = m_bDelayedUpdate;
    state.lastUpdateTicks = m_nLastUpdateTicks;
    state.parserState = static_cast<unsigned>(m_State);
    state.param1 = m_nParam1;
    state.param2 = m_nParam2;
    state.g0CharSet = static_cast<unsigned>(m_G0CharSet);
    state.g1CharSet = static_cast<unsigned>(m_G1CharSet);
    state.useG1 = m_bUseG1;
    memcpy(state.lineAttributes, m_LineAttributes, sizeof(state.lineAttributes));
    memcpy(state.shadowCells, GetActiveShadowCells(), sizeof(state.shadowCells));
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
    m_bDimAttribute = state.dimAttribute;
    m_bUnderlineAttribute = state.underlineAttribute;
    m_bBlinkAttribute = state.blinkAttribute;
    m_bInsertOn = state.insertOn;
    m_bAutoPage = state.autoPage;
    m_bVT52Mode = state.vt52Mode;
    m_bOriginMode = state.originMode;
    m_bWrapAroundMode = state.wrapAroundMode;
    m_bDelayedUpdate = state.delayedUpdate;
    m_nLastUpdateTicks = state.lastUpdateTicks;
    m_State = static_cast<TState>(state.parserState);
    m_nParam1 = state.param1;
    m_nParam2 = state.param2;
    m_G0CharSet = static_cast<ECharacterSet>(state.g0CharSet);
    m_G1CharSet = static_cast<ECharacterSet>(state.g1CharSet);
    m_bUseG1 = state.useG1;
    memcpy(m_LineAttributes, state.lineAttributes, sizeof(m_LineAttributes));
    memcpy(GetActiveShadowCells(), state.shadowCells, sizeof(state.shadowCells));
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
