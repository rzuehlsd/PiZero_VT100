//------------------------------------------------------------------------------
// Module:        CTFontConverter
// Description:   Loads and caches VT100 font assets for renderer consumption.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

// Include Circle core components
#include <circle/sched/task.h>
#include <circle/font.h>

#include "TColorPalette.h"

/**
 * @file TFontConverter.h
 * @brief Declares the task that prepares VT100 font assets for rendering.
 * @details CTFontConverter loads historical VT100 ROM data, converts glyphs to
 * Circle's font representation, and caches the results for the renderer. It
 * exposes convenient accessors for other components to query fonts by logical
 * selection while keeping conversion logic encapsulated in one task.
 */

enum class EFontSelection : unsigned int
{
    VT100Font8x20 = 1,                   // Standard VT100 font 8x20
    VT100Font10x20 = 2,                  // DEC VT100 font 10x20 with dot stretching and scan lines
    VT100Font10x20Solid = 3,             // DEC VT100 font 10x20 solid variant
    VT100GraphicsFont8x20 = 6,           // Graphic VT100 font
    VT100GraphicsFont10x20 = 8,          // DEC Graphic VT100 font 10x20
    VT100GraphicsFont10x20Solid = 10     // DEC Graphic VT100 font 10x20 solid variant
};


/**
 * @class CTFontConverter
 * @brief Background task that materializes VT100 fonts on demand.
 * @details The task encapsulates all initialization and lifetime management for
 * the converted font catalog. It keeps track of whether conversions have run
 * and provides a static GetFont helper so the renderer can fetch glyph tables
 * without duplicating setup logic.
 */
class CTFontConverter : public CTask
{
public:
    /// \brief Access the singleton font converter task.
    /// \return Pointer to the font converter task instance.
    static CTFontConverter *Get(void);

    /// \brief Construct the font converter task.
    CTFontConverter();
    /// \brief Release any converter resources.
    ~CTFontConverter();

    /// \brief Initialize font assets and resume the task.
    /// \return TRUE when initialization succeeds, FALSE otherwise.
    bool Initialize();

    /// \brief Idle loop keeping the converter task alive when needed.
    void Run() override;

    /// \brief Retrieve the Circle font matching the given selection.
    /// \param font Font selection enumeration value.
    /// \return Reference to the converted font.
    static const TFont &GetFont(EFontSelection font);

private:
    /// \brief Track whether conversion has already occurred.
    bool m_Initialized{false};
};