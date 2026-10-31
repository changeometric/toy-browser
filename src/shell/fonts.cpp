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
//
// Since M11 (scroll/resize frame cost) the object also caches cheaply, all
// private to this translation unit and single-threaded like the rest of the MVP:
// - Style resolution: a small direct-mapped cache answers the common
//   "same font properties as the previous call" case by comparing the computed
//   font properties and the language without building or copying a StyleKey.
//   Its misses fall back to a map that keeps every resolved style alive.
// - Text metrics: a bounded LRU keyed by (resolved style, page language, text,
//   font size) -> layout::TextMetrics, used by MeasureText so a re-layout of an
//   unchanged page does not re-shape measured words. The bitmap-degradation path
//   (no usable system font) measures directly instead: its metrics are cheaper to
//   compute than to cache, and that path must stay as fast as before the caches.
// - Runs: a bounded LRU keyed by (resolved style, page language, text) storing
//   (font, byte offset, byte length) so a paint hit rebuilds FontRun views into
//   the current text without decoding per-code-point coverage again.
// A cache allocation failure is treated as a miss (or clears the cache), so the
// caches can never change a measurement, a run or a rendered pixel.

#include "toy/shell/fonts.h"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <new>
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
  // Stable identity of this resolved style, assigned once when it is first
  // built and never reused (M11). The metrics/run caches key on it instead of on
  // a pointer, so even a style that could not be retained by the resolution map
  // can never be confused with a different one. Id 0 is reserved for the
  // bitmap-degradation and FromFile paths, which have no resolved style.
  std::uint64_t id = 0;

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

// Cheap 64-bit mix for the cache hash functions (the standard library does not
// expose one); only the distribution matters, not cryptographic quality.
std::size_t HashCombine(std::size_t seed, std::size_t value) {
  constexpr std::size_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;
  return seed ^ (value + kGoldenRatio + (seed << 6) + (seed >> 2));
}

// Hash of the computed font properties and the language, used to index the
// direct-mapped style cache. It hashes the family text but copies nothing.
std::size_t HashResolvedStyle(const std::vector<std::string>& families, int weight, int slant,
                              std::string_view language) {
  std::size_t hash = std::hash<std::string_view>{}(language);
  hash = HashCombine(hash, static_cast<std::size_t>(weight));
  hash = HashCombine(hash, static_cast<std::size_t>(slant));
  hash = HashCombine(hash, families.size());
  for (const std::string& family : families) {
    hash = HashCombine(hash, std::hash<std::string_view>{}(family));
  }
  return hash;
}

// Hash of a float font size by its exact bit pattern (two equal sizes always
// produce equal metrics, so bit equality is the right cache identity).
std::size_t HashFontSize(float font_size_px) {
  return static_cast<std::size_t>(std::bit_cast<std::uint32_t>(font_size_px));
}

// --- Bounded cache budgets (M11) -------------------------------------------
// The estimates below count the owned key payload, the value payload and a
// fixed per-node overhead for the list entry and the hash-map node. A long page
// has up to ~100k measured words; these budgets hold a whole large page and
// stay bounded when pages contain unique text. An entry larger than half its
// cache budget is not cached at all (it would evict everything else and thrash).
constexpr std::size_t kMaxMetricsCacheEntries = 100'000;
constexpr std::size_t kMaxMetricsCacheBytes = 32u * 1024u * 1024u;
constexpr std::size_t kMaxRunCacheEntries = 100'000;
constexpr std::size_t kMaxRunCacheBytes = 16u * 1024u * 1024u;
// Fixed per-entry overhead estimate: list/map node pointers and allocator slack.
constexpr std::size_t kCacheEntryOverheadBytes = 64;
constexpr std::size_t kMetricEntryLimitBytes = kMaxMetricsCacheBytes / 2;
constexpr std::size_t kRunEntryLimitBytes = kMaxRunCacheBytes / 2;

// Key of the text-metrics LRU: (resolved style id, page language, exact text,
// font size). `language` is already part of the resolved style identity, but it
// is kept in the key because the cache contract names it and because the
// sentinel id 0 (bitmap degradation, FromFile) has no resolved style to carry it.
struct MetricsKey {
  std::uint64_t style_id = 0;
  std::string language;
  std::string text;
  float font_size_px = 0.0f;
};

// Read-only lookup view for MetricsKey: a hit compares and hashes the caller's
// text/language in place, without copying them into an owned key.
struct MetricsLookup {
  std::uint64_t style_id = 0;
  std::string_view language;
  std::string_view text;
  float font_size_px = 0.0f;
};

std::size_t HashMetrics(std::uint64_t style_id, std::string_view language, std::string_view text,
                        float font_size_px) {
  std::size_t hash = static_cast<std::size_t>(style_id);
  hash = HashCombine(hash, HashFontSize(font_size_px));
  hash = HashCombine(hash, std::hash<std::string_view>{}(language));
  hash = HashCombine(hash, std::hash<std::string_view>{}(text));
  return hash;
}

struct MetricsKeyHash {
  using is_transparent = void;
  std::size_t operator()(const MetricsKey& key) const {
    return HashMetrics(key.style_id, key.language, key.text, key.font_size_px);
  }
  std::size_t operator()(const MetricsLookup& key) const {
    return HashMetrics(key.style_id, key.language, key.text, key.font_size_px);
  }
};

struct MetricsKeyEqual {
  using is_transparent = void;
  bool operator()(const MetricsKey& a, const MetricsKey& b) const {
    return a.style_id == b.style_id && a.font_size_px == b.font_size_px &&
           a.language == b.language && a.text == b.text;
  }
  bool operator()(const MetricsKey& a, const MetricsLookup& b) const {
    return a.style_id == b.style_id && a.font_size_px == b.font_size_px &&
           a.language == b.language && a.text == b.text;
  }
  bool operator()(const MetricsLookup& a, const MetricsKey& b) const {
    return a.style_id == b.style_id && a.font_size_px == b.font_size_px &&
           a.language == b.language && a.text == b.text;
  }
};

// One cached run: the font plus the byte range of the run in the cached text.
// The range is resolved back into FontRun string_views of the *current* input
// text on a hit, so a cache entry owns no view into caller memory.
struct CachedRun {
  std::shared_ptr<const Font> font;
  std::size_t offset = 0;
  std::size_t length = 0;
};

// Key of the run LRU: (resolved style id, page language, exact text).
struct RunKey {
  std::uint64_t style_id = 0;
  std::string language;
  std::string text;
};

struct RunLookup {
  std::uint64_t style_id = 0;
  std::string_view language;
  std::string_view text;
};

std::size_t HashRuns(std::uint64_t style_id, std::string_view language, std::string_view text) {
  std::size_t hash = static_cast<std::size_t>(style_id);
  hash = HashCombine(hash, std::hash<std::string_view>{}(language));
  hash = HashCombine(hash, std::hash<std::string_view>{}(text));
  return hash;
}

struct RunKeyHash {
  using is_transparent = void;
  std::size_t operator()(const RunKey& key) const {
    return HashRuns(key.style_id, key.language, key.text);
  }
  std::size_t operator()(const RunLookup& key) const {
    return HashRuns(key.style_id, key.language, key.text);
  }
};

struct RunKeyEqual {
  using is_transparent = void;
  bool operator()(const RunKey& a, const RunKey& b) const {
    return a.style_id == b.style_id && a.language == b.language && a.text == b.text;
  }
  bool operator()(const RunKey& a, const RunLookup& b) const {
    return a.style_id == b.style_id && a.language == b.language && a.text == b.text;
  }
  bool operator()(const RunLookup& a, const RunKey& b) const {
    return a.style_id == b.style_id && a.language == b.language && a.text == b.text;
  }
};

// A bounded least-recently-used cache used by the text-metrics and run caches.
// Keys own their payload (so entries stay valid after the caller's buffers go
// away); lookups use a read-only view type, so a hit neither allocates nor
// copies the text. The byte budget is an estimate supplied by the caller.
//
// Single-threaded, like the Fonts object: no locking. Update failures from
// allocation clear the cache and leave the freshly computed result usable, so a
// cache can never change a measurement or a run.
template <typename Key, typename Lookup, typename Value, typename Hash, typename Equal>
class BoundedLruCache {
 public:
  BoundedLruCache(std::size_t max_entries, std::size_t max_bytes)
      : max_entries_(max_entries), max_bytes_(max_bytes) {}

  // Returns the cached value for `lookup`, or null on a miss. A hit becomes the
  // most recently used entry.
  const Value* Find(const Lookup& lookup) const {
    const auto found = index_.find(lookup);
    if (found == index_.end()) {
      return nullptr;
    }
    const typename EntryList::iterator entry = found->second;
    if (entry != entries_.begin()) {
      entries_.splice(entries_.begin(), entries_, entry);
    }
    return &entries_.front().value;
  }

  // Stores `value` for `key` as the most recently used entry, then evicts the
  // least recently used entries while the entry or byte budget is exceeded. An
  // entry that could never fit is ignored; allocation failure clears the cache.
  void Insert(const Key& key, Value value, std::size_t bytes) const {
    if (bytes > max_bytes_) {
      return;
    }
    try {
      entries_.push_front(Entry{key, std::move(value), bytes});
      if (!index_.emplace(entries_.front().key, entries_.begin()).second) {
        entries_.pop_front();
        return;
      }
      bytes_ += bytes;
      Evict();
    } catch (const std::bad_alloc&) {
      Clear();
    }
  }

 private:
  struct Entry {
    Key key;
    Value value;
    std::size_t bytes = 0;
  };
  using EntryList = std::list<Entry>;

  void Evict() const {
    while (!entries_.empty() && (entries_.size() > max_entries_ || bytes_ > max_bytes_)) {
      const auto oldest = std::prev(entries_.end());
      bytes_ -= oldest->bytes;
      index_.erase(oldest->key);
      entries_.erase(oldest);
    }
  }

  void Clear() const {
    entries_.clear();
    index_.clear();
    bytes_ = 0;
  }

  std::size_t max_entries_;
  std::size_t max_bytes_;
  mutable EntryList entries_;
  mutable std::unordered_map<Key, typename EntryList::iterator, Hash, Equal> index_;
  mutable std::size_t bytes_ = 0;
};

// Direct-mapped fast path for style resolution: a hit compares the computed
// font properties and the language without building or copying a StyleKey. The
// `resolved_` map below remains the fallback on a slot miss (and keeps every
// resolved style alive for the lifetime of the Fonts object).
struct ResolvedSlot {
  bool valid = false;
  std::vector<std::string> families;
  int weight = 400;
  int slant = 0;
  std::string language;
  std::shared_ptr<ResolvedStyle> resolved;
};
constexpr std::size_t kResolvedSlotCount = 16;

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
  void set_family_override(std::string family) {
    family_override_ = std::move(family);
    override_families_ = {family_override_};
  }

  // Resolves the style to an ordered usable candidate list, or null when no
  // candidate loads (the caller then uses the built-in bitmap metrics).
  std::shared_ptr<ResolvedStyle> Resolve(const style::ComputedStyle& style,
                                         std::string_view language) const {
    if (!FontconfigAvailable()) {
      return nullptr;
    }
    const int weight = style.font_weight;
    const int slant =
        style.font_style == style::FontStyle::kItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
    const std::vector<std::string>& families = EffectiveFamilies(style);
    const std::size_t hash = HashResolvedStyle(families, weight, slant, language);
    ResolvedSlot& slot = resolved_slots_[hash % kResolvedSlotCount];
    if (slot.valid && slot.weight == weight && slot.slant == slant && slot.language == language &&
        slot.families == families) {
      return slot.resolved;
    }
    std::shared_ptr<ResolvedStyle> resolved = ResolveUncached(families, weight, slant, language);
    try {
      // Mark the slot invalid before overwriting it, so a failed copy leaves no
      // half-updated key behind that a later comparison could accept.
      slot.valid = false;
      slot.families = families;
      slot.weight = weight;
      slot.slant = slant;
      slot.language.assign(language);
      slot.resolved = resolved;
      slot.valid = true;
    } catch (const std::bad_alloc&) {
      slot.valid = false;
    }
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

  // Metrics for layout, served from the bounded metrics LRU. The key ids the
  // resolved style (see ResolvedStyle::id) and includes the page language and
  // the exact font size, so different styles/sizes/languages can never share an
  // entry. FromFile mode uses the sentinel id 0; bitmap degradation bypasses the
  // cache entirely (see below).
  layout::TextMetrics Measure(std::string_view text_utf8, const style::ComputedStyle& style,
                              std::string_view language, float font_size_px) const {
    const bool file_mode = mode_ == Mode::kFile;
    std::shared_ptr<ResolvedStyle> resolved;
    std::uint64_t style_id = 0;
    if (!file_mode) {
      resolved = Resolve(style, language);
      if (!resolved) {
        // Bitmap degradation: the built-in metrics are one cheap pass over the
        // text, so caching them would cost more (an allocation plus hashing)
        // than recomputing. This keeps the frozen bitmap path as fast as before
        // the caches; every font-dependent path below does cache.
        return ComputeMetrics(false, nullptr, language, text_utf8, font_size_px);
      }
      style_id = resolved->id;
    }
    const MetricsLookup lookup{style_id, language, text_utf8, font_size_px};
    if (const layout::TextMetrics* cached = metrics_cache_.Find(lookup)) {
      return *cached;
    }
    const layout::TextMetrics metrics =
        ComputeMetrics(file_mode, resolved.get(), language, text_utf8, font_size_px);
    StoreMetrics(lookup, metrics);
    return metrics;
  }

  // Runs for painting, served from the run LRU on the real-font path.
  std::vector<paint::FontRun> Runs(std::string_view text_utf8, const style::ComputedStyle& style,
                                   std::string_view language) const {
    if (mode_ == Mode::kFile) {
      return {paint::FontRun{file_font_, text_utf8}};
    }
    std::shared_ptr<ResolvedStyle> resolved = Resolve(style, language);
    if (!resolved) {
      return {};
    }
    return CachedRuns(resolved.get(), language, text_utf8);
  }

 private:
  // Effective family list of a computed style: the FromFamily override when set,
  // otherwise the computed list (or the initial "serif" when it is empty).
  const std::vector<std::string>& EffectiveFamilies(const style::ComputedStyle& style) const {
    if (!family_override_.empty()) {
      return override_families_;
    }
    return style.font_family.empty() ? default_families_ : style.font_family;
  }

  // Slow path of Resolve: map lookup or a fresh fontconfig match. The returned
  // style gets the next never-reused cache id before it is exposed.
  std::shared_ptr<ResolvedStyle> ResolveUncached(const std::vector<std::string>& families,
                                                 int weight, int slant,
                                                 std::string_view language) const {
    StyleKey key;
    key.families = families;
    key.weight = weight;
    key.slant = slant;
    key.language.assign(language);
    const auto cached = resolved_.find(key);
    if (cached != resolved_.end()) {
      return cached->second;
    }
    std::shared_ptr<ResolvedStyle> resolved = BuildResolvedStyle(key);
    if (!resolved) {
      // Remember the unusable style too, so a bitmap-mode page does not run
      // fontconfig for every measure/paint call.
      try {
        resolved_.emplace(std::move(key), nullptr);
      } catch (const std::bad_alloc&) {
        // A cache-budget failure is a miss: the caller still gets the bitmap path.
      }
      return nullptr;
    }
    resolved->id = next_style_id_++;
    try {
      resolved_.emplace(std::move(key), resolved);
    } catch (const std::bad_alloc&) {
      // Not retained for future calls, but still usable for this one. Its id is
      // never reused, so metric/run entries keyed by it cannot be hit by a
      // different style.
    }
    return resolved;
  }

  // The real-font runs of one text box, from the bounded run LRU. A hit returns
  // fresh FontRun views into `text_utf8` without re-decoding code points.
  std::vector<paint::FontRun> CachedRuns(ResolvedStyle* resolved, std::string_view language,
                                         std::string_view text_utf8) const {
    const RunLookup lookup{resolved->id, language, text_utf8};
    if (const std::vector<CachedRun>* cached = run_cache_.Find(lookup)) {
      std::vector<paint::FontRun> runs;
      runs.reserve(cached->size());
      for (const CachedRun& run : *cached) {
        runs.push_back(paint::FontRun{run.font, text_utf8.substr(run.offset, run.length)});
      }
      return runs;
    }
    std::vector<paint::FontRun> runs = RunsFor(resolved, text_utf8);
    StoreRuns(lookup, text_utf8, runs);
    return runs;
  }

  // Computes the metrics that the caches may then store: one font face in
  // FromFile mode, the built-in bitmap metrics when no font resolved, the
  // primary face metrics for empty text, and otherwise the sum/maximum over the
  // runs (exactly what the runs provider gives painting).
  layout::TextMetrics ComputeMetrics(bool file_mode, ResolvedStyle* resolved,
                                     std::string_view language, std::string_view text_utf8,
                                     float font_size_px) const {
    if (file_mode) {
      const std::shared_ptr<const Font>& font = file_font_;
      return layout::TextMetrics{font->MeasureTextWidth(text_utf8, font_size_px),
                                 font->LineHeightPx(font_size_px), font->AscentPx(font_size_px)};
    }
    if (resolved == nullptr) {
      const float line_height_px = render::LineHeightPx(font_size_px);
      return layout::TextMetrics{render::MeasureTextWidth(text_utf8, font_size_px), line_height_px,
                                 line_height_px};
    }
    if (text_utf8.empty()) {
      const std::shared_ptr<const Font>& font = resolved->primary;
      return layout::TextMetrics{0.0f, font->LineHeightPx(font_size_px),
                                 font->AscentPx(font_size_px)};
    }
    float advance_px = 0.0f;
    float line_height_px = 0.0f;
    float ascent_px = 0.0f;
    for (const paint::FontRun& run : CachedRuns(resolved, language, text_utf8)) {
      advance_px += run.font->MeasureTextWidth(run.text, font_size_px);
      line_height_px = std::max(line_height_px, run.font->LineHeightPx(font_size_px));
      ascent_px = std::max(ascent_px, run.font->AscentPx(font_size_px));
    }
    return layout::TextMetrics{advance_px, line_height_px, ascent_px};
  }

  // Stores one metrics result. Any allocation failure (key construction or the
  // cache update itself) is a miss: the computed metrics are still returned.
  void StoreMetrics(const MetricsLookup& lookup, const layout::TextMetrics& metrics) const {
    const std::size_t bytes = sizeof(MetricsKey) + sizeof(layout::TextMetrics) +
                              kCacheEntryOverheadBytes + lookup.language.size() +
                              lookup.text.size();
    if (bytes > kMetricEntryLimitBytes) {
      return;
    }
    try {
      const MetricsKey key{lookup.style_id, std::string(lookup.language), std::string(lookup.text),
                           lookup.font_size_px};
      metrics_cache_.Insert(key, metrics, bytes);
    } catch (const std::bad_alloc&) {
      // A cache-budget failure is a miss, never a behavior change.
    }
  }

  // Stores one run result as (font, byte offset, length) records. Any allocation
  // failure is a miss: the freshly computed runs are still returned.
  void StoreRuns(const RunLookup& lookup, std::string_view text_utf8,
                 const std::vector<paint::FontRun>& runs) const {
    const std::size_t bytes = sizeof(RunKey) + kCacheEntryOverheadBytes + lookup.language.size() +
                              lookup.text.size() + runs.size() * sizeof(CachedRun);
    if (bytes > kRunEntryLimitBytes) {
      return;
    }
    try {
      std::vector<CachedRun> cached;
      cached.reserve(runs.size());
      for (const paint::FontRun& run : runs) {
        cached.push_back(CachedRun{run.font,
                                   static_cast<std::size_t>(run.text.data() - text_utf8.data()),
                                   run.text.size()});
      }
      const RunKey key{lookup.style_id, std::string(lookup.language), std::string(lookup.text)};
      run_cache_.Insert(key, std::move(cached), bytes);
    } catch (const std::bad_alloc&) {
      // A cache-budget failure is a miss, never a behavior change.
    }
  }

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
  std::vector<std::string> override_families_;
  const std::vector<std::string> default_families_{"serif"};
  std::shared_ptr<const Font> file_font_;
  // Next never-reused resolved-style cache id (see ResolvedStyle::id).
  mutable std::uint64_t next_style_id_ = 1;
  // Mutable because a Fonts object is logically immutable but fills these caches
  // as text is measured and painted.
  mutable std::unordered_map<StyleKey, std::shared_ptr<ResolvedStyle>, StyleKeyHash> resolved_;
  mutable std::array<ResolvedSlot, kResolvedSlotCount> resolved_slots_{};
  mutable std::unordered_map<std::string, std::shared_ptr<const Font>> loaded_fonts_;
  mutable BoundedLruCache<MetricsKey, MetricsLookup, layout::TextMetrics, MetricsKeyHash,
                          MetricsKeyEqual>
      metrics_cache_{kMaxMetricsCacheEntries, kMaxMetricsCacheBytes};
  mutable BoundedLruCache<RunKey, RunLookup, std::vector<CachedRun>, RunKeyHash, RunKeyEqual>
      run_cache_{kMaxRunCacheEntries, kMaxRunCacheBytes};
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
  return impl_->Measure(text_utf8, style, language, font_size_px);
}

std::vector<paint::FontRun> Fonts::Runs(std::string_view text_utf8,
                                        const style::ComputedStyle& style,
                                        std::string_view language) const {
  if (text_utf8.empty()) {
    return {};
  }
  return impl_->Runs(text_utf8, style, language);
}

}  // namespace toy::shell
