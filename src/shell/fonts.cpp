// Automatic font selection: the computed CSS font properties and the page
// language become concrete faces through fontconfig, and one text box is split
// into runs by character coverage so scripts missing from the preferred face
// (for example Han text on a Latin page) fall back to a face that has them.
//
// The selection is shared by layout measurement (Fonts::MeasureText) and
// painting (Fonts::Runs), so widths and glyph placement always agree. Fontconfig
// state is process-global: it is initialized once, on first use, and every
// result is cached for the lifetime of the Fonts object. A fixed fontconfig
// configuration and font set therefore produce deterministic runs and metrics.

#include "toy/shell/fonts.h"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "toy/layout/box.h"
#include "toy/paint/paint.h"
#include "toy/render/font.h"
#include "toy/style/computed_style.h"

namespace toy::shell {
namespace {

using render::Font;

// Fontconfig is a process-wide library: initialize it exactly once. The
// configuration (including FONTCONFIG_FILE) is read at this moment, which is
// why tests set the environment before the process starts. False means no
// fontconfig backing at all, so every style degrades to the bitmap path.
bool FontconfigAvailable() {
  static const bool initialized = FcInit() != FcFalse;
  return initialized;
}

// Decodes one UTF-8 code point at `*offset` and advances `*offset` past it.
// Lenient like render's text handling: malformed input (truncated or invalid
// continuation bytes) counts as the single byte's value, so the returned runs
// always cover the input exactly and never fail.
std::uint32_t DecodeUtf8(std::string_view text, std::size_t* offset) {
  const auto byte_at = [text](std::size_t index) {
    return static_cast<unsigned char>(text[index]);
  };
  const std::size_t start = *offset;
  const unsigned char lead = byte_at(start);
  ++*offset;
  if (lead < 0x80) {
    return lead;
  }
  int length = 0;
  std::uint32_t code_point = 0;
  if ((lead & 0xE0) == 0xC0) {
    length = 2;
    code_point = lead & 0x1Fu;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3;
    code_point = lead & 0x0Fu;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4;
    code_point = lead & 0x07u;
  } else {
    return lead;
  }
  if (start + static_cast<std::size_t>(length) > text.size()) {
    return lead;
  }
  for (int index = 1; index < length; ++index) {
    const unsigned char continuation = byte_at(start + static_cast<std::size_t>(index));
    if ((continuation & 0xC0) != 0x80) {
      *offset = start + 1;
      return lead;
    }
    code_point = (code_point << 6) | (continuation & 0x3Fu);
  }
  *offset = start + static_cast<std::size_t>(length);
  return code_point;
}

// One candidate face of an already resolved style: borrowed charset from the
// fontconfig font set plus the path (and lazily loaded Font) of its file.
struct Candidate {
  FcCharSet* charset = nullptr;  // Owned by the FontSet of ResolvedStyle.
  std::string file;              // Font file path, empty when unavailable.
  bool load_attempted = false;
  std::shared_ptr<const Font> font;
};

// The ordered candidate faces for one (families, weight, slant, language) key.
// `primary` is the first candidate that actually loads and is used for empty
// text and as the .notdef fallback when no candidate covers a code point.
struct ResolvedStyle {
  FcFontSet* font_set = nullptr;  // Owns the patterns (and their charsets).
  std::vector<Candidate> candidates;
  std::shared_ptr<const Font> primary;

  ResolvedStyle() = default;
  ~ResolvedStyle() {
    if (font_set != nullptr) {
      FcFontSetDestroy(font_set);
    }
  }
  ResolvedStyle(const ResolvedStyle&) = delete;
  ResolvedStyle& operator=(const ResolvedStyle&) = delete;
};

// Cache key: the computed font properties plus the page language hint. The
// family list is copied because the resolved candidate list outlives the call.
struct StyleKey {
  std::vector<std::string> families;
  int weight = 400;
  int slant = 0;
  std::string language;

  bool operator==(const StyleKey& other) const {
    return families == other.families && weight == other.weight && slant == other.slant &&
           language == other.language;
  }
};

struct StyleKeyHash {
  std::size_t operator()(const StyleKey& key) const {
    std::size_t hash = std::hash<std::string>{}(key.language);
    hash = hash * 31u + static_cast<std::size_t>(key.weight);
    hash = hash * 31u + static_cast<std::size_t>(key.slant);
    for (const std::string& family : key.families) {
      hash = hash * 31u + std::hash<std::string>{}(family);
    }
    return hash;
  }
};

}  // namespace

// The private implementation: mode, caches and the fontconfig matching itself.
// Mutable caches are an implementation detail of the logically immutable object
// (single-threaded like the rest of the MVP).
class Fonts::Impl {
 public:
  enum class Mode { kFile, kSystem };

  explicit Impl(Mode mode) : mode_(mode) {}
  ~Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  Mode mode() const { return mode_; }

  // The one face of FromFile mode (always set in that mode).
  const std::shared_ptr<const Font>& file_font() const { return file_font_; }
  void set_file_font(std::shared_ptr<const Font> font) { file_font_ = std::move(font); }

  // FromFamily fixes the family list; SystemDefault follows the computed style.
  void set_family_override(std::string family) { family_override_ = std::move(family); }

  // Resolves the style to an ordered usable candidate list, or null when no
  // candidate loads (the caller then uses the built-in bitmap metrics).
  std::shared_ptr<ResolvedStyle> Resolve(const style::ComputedStyle& style,
                                         std::string_view language) const {
    if (!FontconfigAvailable()) {
      return nullptr;
    }
    StyleKey key;
    key.families =
        family_override_.empty() ? style.font_family : std::vector<std::string>{family_override_};
    if (key.families.empty()) {
      key.families.push_back("serif");
    }
    key.weight = style.font_weight;
    key.slant = style.font_style == style::FontStyle::kItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
    key.language = std::string(language);
    const auto cached = resolved_.find(key);
    if (cached != resolved_.end()) {
      return cached->second;
    }
    std::shared_ptr<ResolvedStyle> resolved = BuildResolvedStyle(key);
    resolved_.emplace(std::move(key), resolved);
    return resolved;
  }

  // Loads a candidate's face on demand; failed loads are remembered so a broken
  // file is not reopened for every code point.
  std::shared_ptr<const Font> LoadCandidate(Candidate* candidate) const {
    if (!candidate->font && !candidate->load_attempted) {
      candidate->load_attempted = true;
      candidate->font = LoadFont(candidate->file);
    }
    return candidate->font;
  }

  // The face for one code point: the first candidate whose charset covers it, or
  // the primary face (which renders .notdef) when no candidate does. Never null.
  std::shared_ptr<const Font> FontForCodePoint(ResolvedStyle* resolved,
                                               std::uint32_t code_point) const {
    for (Candidate& candidate : resolved->candidates) {
      if (!FcCharSetHasChar(candidate.charset, code_point)) {
        continue;
      }
      if (const std::shared_ptr<const Font> font = LoadCandidate(&candidate)) {
        return font;
      }
    }
    return resolved->primary;
  }

  // Splits one text box into ordered, non-empty runs that concatenate exactly to
  // `text_utf8`. Always non-empty for non-empty text and non-null resolved.
  std::vector<paint::FontRun> RunsFor(ResolvedStyle* resolved, std::string_view text_utf8) const {
    std::vector<paint::FontRun> runs;
    std::size_t offset = 0;
    std::size_t run_start = 0;
    std::shared_ptr<const Font> run_font;
    while (offset < text_utf8.size()) {
      const std::size_t code_point_start = offset;
      const std::uint32_t code_point = DecodeUtf8(text_utf8, &offset);
      const std::shared_ptr<const Font> font = FontForCodePoint(resolved, code_point);
      if (!run_font) {
        run_font = font;
        run_start = code_point_start;
      } else if (font != run_font) {
        runs.push_back(
            paint::FontRun{run_font, text_utf8.substr(run_start, code_point_start - run_start)});
        run_font = font;
        run_start = code_point_start;
      }
    }
    if (run_font) {
      runs.push_back(paint::FontRun{run_font, text_utf8.substr(run_start)});
    }
    return runs;
  }

 private:
  // Builds the ordered candidate list for one style key through fontconfig:
  // family list, weight, slant and language become a pattern; the pattern is
  // substituted and defaulted exactly like fc-match, then sorted into the
  // closest-first list. Non-outline faces, faces without a file and faces
  // without a charset cannot be used for text, so they are dropped. The style is
  // unusable when no candidate loads, which degrades it to the bitmap font.
  std::shared_ptr<ResolvedStyle> BuildResolvedStyle(const StyleKey& key) const {
    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
      return nullptr;
    }
    for (const std::string& family : key.families) {
      FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    }
    FcPatternAddInteger(pattern, FC_WEIGHT, FcWeightFromOpenType(std::clamp(key.weight, 0, 1000)));
    FcPatternAddInteger(pattern, FC_SLANT, key.slant);
    if (!key.language.empty()) {
      FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8*>(key.language.c_str()));
    }
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult sort_result = FcResultNoMatch;
    // Language is a preference, not a filter: a page language hint must never
    // make every font disappear just because installed fonts carry no language
    // metadata, so the set is sorted rather than trimmed.
    FcFontSet* font_set = FcFontSort(nullptr, pattern, FcFalse, nullptr, &sort_result);
    FcPatternDestroy(pattern);
    if (font_set == nullptr || font_set->nfont == 0) {
      if (font_set != nullptr) {
        FcFontSetDestroy(font_set);
      }
      return nullptr;
    }

    auto resolved = std::make_shared<ResolvedStyle>();
    resolved->font_set = font_set;
    resolved->candidates.reserve(static_cast<std::size_t>(font_set->nfont));
    for (int index = 0; index < font_set->nfont; ++index) {
      FcPattern* candidate = font_set->fonts[index];
      FcBool outline = FcTrue;
      if (FcPatternGetBool(candidate, FC_OUTLINE, 0, &outline) == FcResultMatch && !outline) {
        continue;
      }
      FcChar8* file = nullptr;
      if (FcPatternGetString(candidate, FC_FILE, 0, &file) != FcResultMatch || file == nullptr) {
        continue;
      }
      FcCharSet* charset = nullptr;
      if (FcPatternGetCharSet(candidate, FC_CHARSET, 0, &charset) != FcResultMatch ||
          charset == nullptr) {
        continue;
      }
      resolved->candidates.push_back(
          Candidate{charset, reinterpret_cast<const char*>(file), false, nullptr});
    }
    for (Candidate& candidate : resolved->candidates) {
      if (const std::shared_ptr<const Font> font = LoadCandidate(&candidate)) {
        resolved->primary = font;
        break;
      }
    }
    if (!resolved->primary) {
      return nullptr;
    }
    return resolved;
  }

  // Loads (and caches) one font file. A failed load is cached as a null font.
  std::shared_ptr<const Font> LoadFont(const std::string& file) const {
    const auto cached = loaded_fonts_.find(file);
    if (cached != loaded_fonts_.end()) {
      return cached->second;
    }
    std::string error;
    std::shared_ptr<const Font> font = Font::LoadFromFile(file, &error);
    loaded_fonts_.emplace(file, font);
    return font;
  }

  Mode mode_ = Mode::kSystem;
  std::string family_override_;
  std::shared_ptr<const Font> file_font_;
  // Mutable because a Fonts object is logically immutable but fills these caches
  // as text is measured and painted.
  mutable std::unordered_map<StyleKey, std::shared_ptr<ResolvedStyle>, StyleKeyHash> resolved_;
  mutable std::unordered_map<std::string, std::shared_ptr<const Font>> loaded_fonts_;
};

Fonts::Fonts() = default;

Fonts::~Fonts() = default;

std::shared_ptr<const Fonts> Fonts::SystemDefault() {
  std::shared_ptr<Fonts> fonts(new Fonts());
  fonts->impl_ = std::make_unique<Impl>(Impl::Mode::kSystem);
  return fonts;
}

std::shared_ptr<const Fonts> Fonts::FromFile(std::string_view path, std::string* error) {
  std::string load_error;
  std::shared_ptr<const Font> font = render::Font::LoadFromFile(path, &load_error);
  if (!font) {
    if (error != nullptr) {
      *error = load_error;
    }
    return nullptr;
  }
  std::shared_ptr<Fonts> fonts(new Fonts());
  fonts->impl_ = std::make_unique<Impl>(Impl::Mode::kFile);
  fonts->impl_->set_file_font(std::move(font));
  if (error != nullptr) {
    error->clear();
  }
  return fonts;
}

std::shared_ptr<const Fonts> Fonts::FromFamily(std::string_view family, std::string* error) {
  if (family.empty()) {
    if (error != nullptr) {
      *error = "the font family name is empty";
    }
    return nullptr;
  }
  if (!FontconfigAvailable()) {
    if (error != nullptr) {
      *error = "fontconfig initialization failed";
    }
    return nullptr;
  }
  std::shared_ptr<Fonts> fonts(new Fonts());
  fonts->impl_ = std::make_unique<Impl>(Impl::Mode::kSystem);
  fonts->impl_->set_family_override(std::string(family));
  // An explicit family must not silently fall back to the bitmap font: resolve a
  // probe style now and report the failure to the caller.
  const style::ComputedStyle probe;
  if (!fonts->impl_->Resolve(probe, "")) {
    if (error != nullptr) {
      *error = "fontconfig cannot provide a usable font for family '" + std::string(family) + "'";
    }
    return nullptr;
  }
  if (error != nullptr) {
    error->clear();
  }
  return fonts;
}

layout::TextMetrics Fonts::MeasureText(std::string_view text_utf8,
                                       const style::ComputedStyle& style,
                                       std::string_view language) const {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  if (impl_->mode() == Impl::Mode::kFile) {
    const std::shared_ptr<const Font>& font = impl_->file_font();
    return layout::TextMetrics{font->MeasureTextWidth(text_utf8, font_size_px),
                               font->LineHeightPx(font_size_px), font->AscentPx(font_size_px)};
  }
  std::shared_ptr<ResolvedStyle> resolved = impl_->Resolve(style, language);
  if (!resolved) {
    const float line_height_px = render::LineHeightPx(font_size_px);
    return layout::TextMetrics{render::MeasureTextWidth(text_utf8, font_size_px), line_height_px,
                               line_height_px};
  }
  // Empty text has no code points to resolve: report the style's primary face
  // metrics (layout uses them for empty lines and <br>).
  if (text_utf8.empty()) {
    const std::shared_ptr<const Font>& font = resolved->primary;
    return layout::TextMetrics{0.0f, font->LineHeightPx(font_size_px),
                               font->AscentPx(font_size_px)};
  }
  float advance_px = 0.0f;
  float line_height_px = 0.0f;
  float ascent_px = 0.0f;
  for (const paint::FontRun& run : impl_->RunsFor(resolved.get(), text_utf8)) {
    advance_px += run.font->MeasureTextWidth(run.text, font_size_px);
    line_height_px = std::max(line_height_px, run.font->LineHeightPx(font_size_px));
    ascent_px = std::max(ascent_px, run.font->AscentPx(font_size_px));
  }
  return layout::TextMetrics{advance_px, line_height_px, ascent_px};
}

std::vector<paint::FontRun> Fonts::Runs(std::string_view text_utf8,
                                        const style::ComputedStyle& style,
                                        std::string_view language) const {
  if (text_utf8.empty()) {
    return {};
  }
  if (impl_->mode() == Impl::Mode::kFile) {
    return {paint::FontRun{impl_->file_font(), text_utf8}};
  }
  std::shared_ptr<ResolvedStyle> resolved = impl_->Resolve(style, language);
  if (!resolved) {
    return {};
  }
  return impl_->RunsFor(resolved.get(), text_utf8);
}

}  // namespace toy::shell
