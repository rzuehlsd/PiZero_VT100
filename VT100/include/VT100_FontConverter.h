//------------------------------------------------------------------------------
// Module:        VT100_FontConverter
// Description:   Declares helpers to convert VT100 ROM data into Circle fonts.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2025-12-05
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2025-12-05     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/types.h>
#include <circle/font.h>

// Forward declaration for font selection enum to avoid circular dependency
enum class EFontSelection : unsigned int;

/**
 * @file VT100_FontConverter.h
 * @brief Declares helper functions that translate VT100 ROM fonts to Circle.
 * @details These conversion utilities are shared between the standalone font
 * converter and the runtime task. They encapsulate the mechanical steps needed
 * to expand original DEC terminal bitmaps into the Circle TFont format across
 * all supported sizes and variants.
 */

/// \brief Access the converted Circle font for the requested selection.
const TFont &GetVT100Font(EFontSelection selection);

/// \brief Perform the full conversion for all VT100 fonts.
void ConvertVT100Font(void);

/// \brief Convert the 10x20 VT100 font with CRT gaps.
void ConvertVT100FontToCircle(void);
/// \brief Convert the 10x20 VT100 font with solid doubling.
void ConvertVT100FontToCircle_SolidDoubling(void);
/// \brief Convert the 8x20 VT100 font variant.
void ConvertVT100FontToCircle_8x20(void);

/// \brief Convert the graphics font (10x20 with CRT gaps).
void ConvertVT100GraphicsToCircle(void);
/// \brief Convert the graphics font solid doubling variant.
void ConvertVT100GraphicsToCircle_SolidDoubling(void);
/// \brief Convert the graphics font 8x20 variant.
void ConvertVT100GraphicsToCircle_8x20(void);
