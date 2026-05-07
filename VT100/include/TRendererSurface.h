//------------------------------------------------------------------------------
// Module:        CRendererSurface
// Description:   Owns the framebuffer backend and raw pixel buffer.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-05-07
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-05-07     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/bcmframebuffer.h>
#include <circle/display.h>
#include <circle/types.h>

/**
 * @file TRendererSurface.h
 * @brief Declares the raw framebuffer backend used by the VT100 renderer stack.
 * @details CRendererSurface owns Circle's CBcmFrameBuffer instance together
 * with the backing raw pixel buffer. It centralizes framebuffer initialization,
 * logical-to-raw color conversion, raw pixel access, row-oriented pixel
 * mutations, and incremental area flushes so higher-level renderer code does
 * not need to depend directly on the display device implementation.
 */

/**
 * @class CRendererSurface
 * @brief Encapsulates the framebuffer device and its raw pixel storage.
 * @details The surface provides a narrow backend API tailored to the VT100
 * renderer: initialize the display, expose geometry, mutate raw pixels, and
 * flush changed areas to the hardware. CTRenderer retains VT100 semantics while
 * CTRendererProjector uses this backend to project shadow-state changes.
 */
class CRendererSurface
{
public:
    /// @brief Construct an empty framebuffer backend.
    CRendererSurface(void);
    /// @brief Release the framebuffer device and raw buffer.
    ~CRendererSurface(void);

    /// @brief Initialize the Circle framebuffer and allocate the backing store.
    /// @param displayIndex Circle display index to open.
    /// @param depth Requested color depth.
    /// @return TRUE on success, FALSE otherwise.
    boolean Initialize(unsigned displayIndex, unsigned depth);

    /// @brief Access the underlying Circle framebuffer device.
    CBcmFrameBuffer *GetDisplay(void) const;
    /// @brief Access the raw framebuffer backing store.
    u8 *GetBuffer(void) const;
    /// @brief Query framebuffer width in pixels.
    unsigned GetWidth(void) const;
    /// @brief Query framebuffer height in pixels.
    unsigned GetHeight(void) const;
    /// @brief Query framebuffer color depth.
    unsigned GetDepth(void) const;
    /// @brief Query framebuffer pitch in bytes.
    unsigned GetPitch(void) const;
    /// @brief Query raw buffer size in bytes.
    size_t GetSize(void) const;

    /// @brief Convert a logical display color to the raw framebuffer format.
    CDisplay::TRawColor GetRawColor(CDisplay::TColor color) const;
    /// @brief Convert a raw framebuffer color back to a logical display color.
    CDisplay::TColor GetLogicalColor(CDisplay::TRawColor rawColor) const;

    /// @brief Write one raw pixel into the backing store.
    void SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);
    /// @brief Read one raw pixel from the backing store.
    CDisplay::TRawColor GetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor defaultColor) const;
    /// @brief Fill a half-open range of pixel rows with one raw color.
    void FillRows(unsigned startY, unsigned endY, CDisplay::TRawColor color);
    /// @brief Scroll a half-open range of pixel rows upward in-place.
    void ScrollRowsUp(unsigned startY, unsigned endY, unsigned deltaY);
    /// @brief Write a single pixel to both the backing store and the hardware device.
    void SetPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor);
    /// @brief Flush a changed framebuffer area from a caller-provided source pointer.
    void FlushArea(const CDisplay::TArea &area, const void *pSourceBuffer);

private:
    CBcmFrameBuffer *m_pFrameBuffer;
    u8 *m_pBuffer;
    unsigned m_nWidth;
    unsigned m_nHeight;
    unsigned m_nDepth;
    unsigned m_nPitch;
    size_t m_nSize;
};