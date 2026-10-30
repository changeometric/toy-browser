#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "toy/css/stylesheet.h"
#include "toy/style/computed_style.h"

namespace toy::style::detail {

// One parsed length: CSS pixels, or a multiple of the relevant font size.
struct Length {
  float value = 0;
  bool in_em = false;
};

// Parses "16px", "1.5em" or unitless zero (the only unitless length CSS
// allows). Units are case-insensitive. Returns std::nullopt for anything else,
// including percentages and other units.
std::optional<Length> ParseLength(std::string_view text);

// Parses #rgb, #rrggbb and the named colors of the M4 subset. Returns
// std::nullopt for unknown or malformed values.
std::optional<std::uint32_t> ParseColor(std::string_view text);

// Font-size pass: `em` resolves against the parent's computed font size.
// A declaration whose value is not understood leaves the style unchanged.
void ApplyFontSizeDeclaration(const css::Declaration& declaration, float parent_font_size_px,
                              ComputedStyle* style);

// Main pass: applies one non-font-size declaration. `em` lengths resolve
// against the element's final computed font size, so font-size must have been
// applied first. Unknown properties and values are ignored.
void ApplyDeclaration(const css::Declaration& declaration, ComputedStyle* style);

}  // namespace toy::style::detail
