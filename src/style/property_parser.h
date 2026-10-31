#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "toy/css/stylesheet.h"
#include "toy/style/computed_style.h"

namespace toy::style::detail {

// One parsed CSS length before style resolves the font-relative units. `value`
// is in the unit's own scale: CSS pixels, em multiples, rem multiples, or
// percent points (50 for "50%"). Box-property percentages stay unresolved for
// layout; em/rem are resolved to px in style.
struct ParsedLength {
  enum class Unit { kPx, kEm, kRem, kPercent };
  float value = 0.0f;
  Unit unit = Unit::kPx;
};

// Parses "16px", "1.5em", "2rem", "50%" or unitless zero. Units are
// case-insensitive. Returns std::nullopt for anything else (other units,
// non-zero unitless numbers, malformed text).
std::optional<ParsedLength> ParseLength(std::string_view text);

// Parses a plain unitless number such as "1.5" or "2" (line-height multiplier).
// Returns std::nullopt when the text has a unit or is malformed.
std::optional<float> ParseNumber(std::string_view text);

// Parses a CSS color value into 0xAARRGGBB. Supports #rgb/#rgba/#rrggbb/
// #rrggbbaa, rgb()/rgba(), hsl()/hsla() (legacy comma-separated syntax), the
// full CSS Color Level 4 named-color set and `transparent`. Returns
// std::nullopt for unknown or malformed values.
std::optional<std::uint32_t> ParseColor(std::string_view text);

// Font-size pass: resolves `em`/`%` against the parent font size and `rem`
// against the root element's computed font size (the root element itself uses
// the 16px initial size). A declaration whose value is not understood leaves
// the style unchanged.
void ApplyFontSizeDeclaration(const css::Declaration& declaration, float parent_font_size_px,
                              float root_font_size_px, ComputedStyle* style);

// Main pass: applies one non-font-size declaration. `em` lengths resolve
// against the element's final computed font size (font-size must have been
// applied first) and `rem` lengths against the root element's computed font
// size. Unknown properties and values are ignored.
void ApplyDeclaration(const css::Declaration& declaration, float root_font_size_px,
                      ComputedStyle* style);

}  // namespace toy::style::detail
