// Break-unit tokenizer for inline text: minimal, documented CJK line breaking.
// See text_break.h for the model and the documented MVP limits.

#include "text_break.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toy::layout::detail {
namespace {

// One decoded UTF-8 code point. `length` bytes were consumed (always >= 1).
// `valid` is false for an invalid lead byte, a truncated sequence or a malformed
// continuation byte; then only the lead byte is consumed and `value` holds it.
struct DecodedCodePoint {
  std::uint32_t value = 0;
  std::size_t length = 1;
  bool valid = true;
};

DecodedCodePoint DecodeUtf8CodePoint(std::string_view text, std::size_t index) {
  const unsigned char lead = static_cast<unsigned char>(text[index]);
  DecodedCodePoint result;
  result.value = lead;
  if (lead < 0x80) {
    return result;  // ASCII.
  }
  std::size_t length = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if ((lead & 0xE0u) == 0xC0u) {
    length = 2;
    value = lead & 0x1Fu;
    minimum = 0x80;
  } else if ((lead & 0xF0u) == 0xE0u) {
    length = 3;
    value = lead & 0x0Fu;
    minimum = 0x800;
  } else if ((lead & 0xF8u) == 0xF0u) {
    length = 4;
    value = lead & 0x07u;
    minimum = 0x10000;
  } else {
    result.valid = false;  // A continuation byte or an invalid lead byte.
    return result;
  }
  if (index + length > text.size()) {
    result.valid = false;  // Truncated sequence: consume the lead byte only.
    return result;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
    if ((continuation & 0xC0u) != 0x80u) {
      result.valid = false;  // Malformed sequence: consume the lead byte only.
      return result;
    }
    value = (value << 6) | (continuation & 0x3Fu);
  }
  if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    result.valid = false;  // Overlong encoding, out of range or a surrogate.
    return result;
  }
  result.value = value;
  result.length = length;
  return result;
}

// The CJK code points that become individual break units (the contract's ranges).
// Closing/opening punctuation is handled before this check.
bool IsCjkCodePoint(std::uint32_t code_point) {
  return (code_point >= 0x3000 && code_point <= 0x303F) ||    // CJK punctuation/symbols
         (code_point >= 0x3040 && code_point <= 0x309F) ||    // Hiragana
         (code_point >= 0x30A0 && code_point <= 0x30FF) ||    // Katakana
         (code_point >= 0x3100 && code_point <= 0x312F) ||    // Bopomofo
         (code_point >= 0x3400 && code_point <= 0x4DBF) ||    // Han extension A
         (code_point >= 0x4E00 && code_point <= 0x9FFF) ||    // Han
         (code_point >= 0xF900 && code_point <= 0xFAFF) ||    // Han compatibility
         (code_point >= 0x20000 && code_point <= 0x2FA1F) ||  // Han extensions B-F
         (code_point >= 0x1100 && code_point <= 0x11FF) ||    // Hangul Jamo
         (code_point >= 0x3130 && code_point <= 0x318F) ||    // Hangul compatibility Jamo
         (code_point >= 0xAC00 && code_point <= 0xD7AF) ||    // Hangul syllables
         (code_point >= 0xFF00 && code_point <= 0xFFEF);      // Fullwidth/halfwidth forms
}

// Closing punctuation never starts a line: it attaches to the preceding unit.
bool IsClosingPunctuation(std::uint32_t code_point) {
  switch (code_point) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x3017:  // 〗
    case 0x3019:  // 〙
    case 0x301B:  // 〛
    case 0x2025:  // ‥
    case 0x2026:  // …
    case 0x2014:  // —
    case 0xFF01:  // ！
    case 0xFF09:  // ）
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1F:  // ？
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
      return true;
    default:
      return false;
  }
}

// Opening punctuation never ends a line: it attaches to the following unit.
bool IsOpeningPunctuation(std::uint32_t code_point) {
  switch (code_point) {
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0x3016:  // 〖
    case 0x3018:  // 〘
    case 0x301A:  // 〚
    case 0xFF08:  // （
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
      return true;
    default:
      return false;
  }
}

// Classification of one code point for the unit-building state machine.
enum class UnitKind { kNone, kSeparator, kOther, kCjk, kClosing, kOpening };

bool IsAsciiWhitespace(std::uint32_t code_point) {
  return code_point == ' ' || code_point == '\t' || code_point == '\n' || code_point == '\r' ||
         code_point == '\f';
}

UnitKind ClassifyCodePoint(std::uint32_t code_point) {
  // ASCII whitespace and U+3000 IDEOGRAPHIC SPACE are separators, not units.
  if (IsAsciiWhitespace(code_point) || code_point == 0x3000) {
    return UnitKind::kSeparator;
  }
  if (IsClosingPunctuation(code_point)) {
    return UnitKind::kClosing;
  }
  if (IsOpeningPunctuation(code_point)) {
    return UnitKind::kOpening;
  }
  if (IsCjkCodePoint(code_point)) {
    return UnitKind::kCjk;
  }
  return UnitKind::kOther;
}

}  // namespace

std::vector<TextPiece> SplitBreakUnits(std::string_view text_utf8) {
  std::vector<TextPiece> pieces;
  std::string unit;
  UnitKind last = UnitKind::kNone;
  // True while `unit` holds only leading closing punctuation that still has to
  // absorb the following content so it cannot start a line.
  bool leading_closing = false;

  const auto flush = [&]() {
    if (!unit.empty()) {
      pieces.push_back(TextPiece{TextPieceKind::kUnit, std::move(unit)});
      unit.clear();
    }
    last = UnitKind::kNone;
    leading_closing = false;
  };
  const auto append = [&](std::string_view bytes, UnitKind kind) {
    unit.append(bytes);
    last = kind;
  };

  std::size_t index = 0;
  while (index < text_utf8.size()) {
    const DecodedCodePoint decoded = DecodeUtf8CodePoint(text_utf8, index);
    const std::string_view bytes = text_utf8.substr(index, decoded.length);
    index += decoded.length;
    if (!decoded.valid) {
      // The offending byte is its own unit; decoding continues after it.
      flush();
      pieces.push_back(TextPiece{TextPieceKind::kUnit, std::string(bytes)});
      continue;
    }
    const UnitKind kind = ClassifyCodePoint(decoded.value);
    switch (kind) {
      case UnitKind::kSeparator:
        flush();
        // Collapse a whitespace run into one separator piece.
        if (pieces.empty() || pieces.back().kind != TextPieceKind::kSeparator) {
          pieces.push_back(TextPiece{TextPieceKind::kSeparator, std::string()});
        }
        break;
      case UnitKind::kClosing:
        // Attaches to the preceding unit; with no preceding unit it becomes a
        // leading-closing prefix that absorbs the following content.
        leading_closing = unit.empty();
        append(bytes, UnitKind::kClosing);
        break;
      case UnitKind::kOpening:
        if (!unit.empty() && !leading_closing && last != UnitKind::kOpening) {
          flush();
        }
        append(bytes, UnitKind::kOpening);
        break;
      case UnitKind::kCjk:
      case UnitKind::kOther: {
        const bool joins_current =
            !unit.empty() && (leading_closing || last == UnitKind::kOpening ||
                              (kind == UnitKind::kOther && last == UnitKind::kOther));
        if (!joins_current) {
          flush();
        }
        append(bytes, kind);
        leading_closing = false;
        break;
      }
      case UnitKind::kNone:
        break;  // Unreachable: every case above returns a real kind.
    }
  }
  flush();
  return pieces;
}

}  // namespace toy::layout::detail
