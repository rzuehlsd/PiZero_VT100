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

#include "TRendererSurface.h"

#include <string.h>

CRendererSurface::CRendererSurface(void)
    : m_pFrameBuffer(nullptr), m_pBuffer(nullptr), m_nWidth(0), m_nHeight(0), m_nDepth(0), m_nPitch(0), m_nSize(0)
{
}

CRendererSurface::~CRendererSurface(void)
{
    delete[] m_pBuffer;
    m_pBuffer = nullptr;

    delete m_pFrameBuffer;
    m_pFrameBuffer = nullptr;
}

boolean CRendererSurface::Initialize(unsigned displayIndex, unsigned depth)
{
    m_pFrameBuffer = new CBcmFrameBuffer(0, 0, depth, 0, 0, displayIndex);
    if (m_pFrameBuffer == nullptr)
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
    m_nSize = static_cast<size_t>(m_nWidth) * m_nHeight * m_nDepth / 8;
    m_nPitch = m_nWidth * m_nDepth / 8;

    if (m_nDepth == 1 && m_nWidth % 8 != 0)
    {
        return FALSE;
    }

    m_pBuffer = new u8[m_nSize];
    if (m_pBuffer == nullptr)
    {
        return FALSE;
    }

    memset(m_pBuffer, 0, m_nSize);
    return TRUE;
}

CBcmFrameBuffer *CRendererSurface::GetDisplay(void) const
{
    return m_pFrameBuffer;
}

u8 *CRendererSurface::GetBuffer(void) const
{
    return m_pBuffer;
}

unsigned CRendererSurface::GetWidth(void) const
{
    return m_nWidth;
}

unsigned CRendererSurface::GetHeight(void) const
{
    return m_nHeight;
}

unsigned CRendererSurface::GetDepth(void) const
{
    return m_nDepth;
}

unsigned CRendererSurface::GetPitch(void) const
{
    return m_nPitch;
}

size_t CRendererSurface::GetSize(void) const
{
    return m_nSize;
}

CDisplay::TRawColor CRendererSurface::GetRawColor(CDisplay::TColor color) const
{
    return m_pFrameBuffer != nullptr ? m_pFrameBuffer->GetColor(color) : 0;
}

CDisplay::TColor CRendererSurface::GetLogicalColor(CDisplay::TRawColor rawColor) const
{
    return m_pFrameBuffer != nullptr ? m_pFrameBuffer->GetColor(rawColor) : CDisplay::Black;
}

void CRendererSurface::SetRawPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    if (m_pBuffer == nullptr || nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }

    switch (m_nDepth)
    {
    case 1:
    {
        u8 *pBuffer = &m_pBuffer[(m_nWidth * nPosY + nPosX) / 8];
        u8 uchMask = 0x80 >> (nPosX & 7);
        if (nColor)
        {
            *pBuffer |= uchMask;
        }
        else
        {
            *pBuffer &= ~uchMask;
        }
        break;
    }

    case 8:
        m_pBuffer[m_nWidth * nPosY + nPosX] = static_cast<u8>(nColor);
        break;

    case 16:
        reinterpret_cast<u16 *>(m_pBuffer)[m_nWidth * nPosY + nPosX] = static_cast<u16>(nColor);
        break;

    case 32:
        reinterpret_cast<u32 *>(m_pBuffer)[m_nWidth * nPosY + nPosX] = nColor;
        break;
    }
}

CDisplay::TRawColor CRendererSurface::GetRawPixel(unsigned nPosX,
                                                  unsigned nPosY,
                                                  CDisplay::TRawColor defaultColor) const
{
    if (m_pBuffer == nullptr || nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return defaultColor;
    }

    switch (m_nDepth)
    {
    case 1:
    {
        const u8 *pBuffer = &m_pBuffer[(m_nWidth * nPosY + nPosX) / 8];
        u8 uchMask = 0x80 >> (nPosX & 7);
        return !!(*pBuffer & uchMask);
    }

    case 8:
        return m_pBuffer[m_nWidth * nPosY + nPosX];

    case 16:
        return reinterpret_cast<const u16 *>(m_pBuffer)[m_nWidth * nPosY + nPosX];

    case 32:
        return reinterpret_cast<const u32 *>(m_pBuffer)[m_nWidth * nPosY + nPosX];

    default:
        return defaultColor;
    }
}

void CRendererSurface::FillRows(unsigned startY, unsigned endY, CDisplay::TRawColor color)
{
    if (m_pBuffer == nullptr || startY >= endY || startY >= m_nHeight)
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
        memset(m_pBuffer + startY * m_nWidth,
               static_cast<int>(static_cast<u8>(color)),
               static_cast<size_t>(rowCount) * m_nWidth);
        break;

    case 16:
    {
        u16 *pRow = reinterpret_cast<u16 *>(m_pBuffer) + startY * m_nWidth;
        const size_t pixelCount = static_cast<size_t>(rowCount) * m_nWidth;
        for (size_t index = 0; index < pixelCount; ++index)
        {
            pRow[index] = static_cast<u16>(color);
        }
        break;
    }

    case 32:
    {
        u32 *pRow = reinterpret_cast<u32 *>(m_pBuffer) + startY * m_nWidth;
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

void CRendererSurface::ScrollRowsUp(unsigned startY, unsigned endY, unsigned deltaY)
{
    if (m_pBuffer == nullptr || startY >= endY || deltaY == 0 || startY >= m_nHeight)
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
    memmove(m_pBuffer + startY * m_nPitch,
            m_pBuffer + (startY + deltaY) * m_nPitch,
            bytesToMove);
}

void CRendererSurface::SetPixel(unsigned nPosX, unsigned nPosY, CDisplay::TRawColor nColor)
{
    if (m_pFrameBuffer == nullptr || nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }

    SetRawPixel(nPosX, nPosY, nColor);
    m_pFrameBuffer->SetPixel(nPosX, nPosY, nColor);
}

void CRendererSurface::FlushArea(const CDisplay::TArea &area, const void *pSourceBuffer)
{
    if (m_pFrameBuffer == nullptr || pSourceBuffer == nullptr)
    {
        return;
    }

    m_pFrameBuffer->SetArea(area, pSourceBuffer);
}