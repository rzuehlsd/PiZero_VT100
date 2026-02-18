//------------------------------------------------------------------------------
// Module:        TColorPalette
// Description:   Shared color selection definitions for the VT100 renderer.
// Author:        R. Zuehlsdorff, ralf.zuehlsdorff@t-online.de
// Created:       2026-01-18
// License:       MIT License (https://opensource.org/license/mit/)
//------------------------------------------------------------------------------
// Change Log:
// 2026-01-18     R. Zuehlsdorff        Initial creation
//------------------------------------------------------------------------------

#pragma once

#include <circle/display.h>

/**
 * @file TColorPalette.h
 * @brief Declares shared color selection enums for the VT100 renderer stack.
 * @details This header centralizes all logical color choices exposed through
 * the configuration task and consumed by the renderer. By mapping high-level
 * palette selections to Circle display colors in one place, the renderer and
 * configuration code can evolve without duplicating RGB definitions across
 * multiple translation units.
 */

enum EColorSelection : unsigned int
{
    TerminalColorBlack = 0,
    TerminalColorWhite = 1,
    TerminalColorAmber = 2,
    TerminalColorGreen = 3
};

using TRendererColor = CDisplay::TColor;
