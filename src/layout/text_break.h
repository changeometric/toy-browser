#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace toy::layout::detail {

// One piece of a text node's line-breaking segmentation, in source order.
enum class TextPieceKind {
  // A run of ASCII whitespace or U+3000 IDEOGRAPHIC SPACE. It produces no box
  // and collapses with adjacent separators into one pending space by the caller.
  kSeparator,
  // An unbreakable break unit: it becomes one word-level text box, and a line
  // may break between any two units.
  kUnit,
};

struct TextPiece {
  TextPieceKind kind = TextPieceKind::kUnit;
  // kUnit: the unit's source bytes. kSeparator: empty (the caller measures the
  // style's space metrics, the separator's own width is not accumulated).
  std::string text;
};

// Splits UTF-8 text into break units for inline layout.
//
// Non-CJK text is grouped into maximal word-like runs delimited by whitespace or
// a CJK code point, which reproduces the pre-M10 word splitting for ASCII text.
// Every CJK code point is its own unit, so Han/Kana/Hangul/Bopomofo/fullwidth
// text can break between characters without spaces. A simplified approximation
// of the two Unicode line-breaking prohibitions is applied (MVP: no UAX #14, no
// compressed punctuation, no hyphenation): closing punctuation joins the
// preceding unit so it never starts a line, and opening punctuation joins the
// following unit so it never ends a line. U+3000 IDEOGRAPHIC SPACE is whitespace,
// not a CJK unit.
//
// Invalid or truncated UTF-8 is handled leniently: the offending lead byte
// becomes its own unit and decoding resumes after it, so malformed input can
// neither crash nor loop. Never fails.
std::vector<TextPiece> SplitBreakUnits(std::string_view text_utf8);

}  // namespace toy::layout::detail
