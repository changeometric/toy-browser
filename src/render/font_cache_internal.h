#pragma once

#include <cstddef>
#include <iterator>
#include <list>
#include <new>
#include <unordered_map>
#include <utility>

// Private to the render module: the bounded least-recently-used cache used by
// the real-font engine (src/render/font_ttf.cpp) for shaped text runs and glyph
// coverage bitmaps (M11). It is deliberately tiny: no threads, no reference
// counting, no persistence. Every failure mode degrades to a cache miss, never
// to a different rendering result.
namespace toy::render::internal {

// Fixed-budget LRU map. Both budgets are mandatory: the byte budget bounds big
// entries (large glyph bitmaps, long shaped runs) and the entry budget bounds
// bookkeeping overhead for many small entries. `Key` must be hashable with
// std::hash, and `Value` movable and copyable-or-movable; the caller bills the
// approximate per-entry cost in `bytes` so the cache can stay bounded without
// knowing the value layout.
//
// The cache is not thread-safe and is meant to be private state of one
// single-threaded object. Allocation failures are swallowed: Put() returns
// nullptr and leaves an empty, usable cache; ordinary rendering must not fail
// because a cache could not grow.
template <typename Key, typename Value>
class LruCache {
 public:
  LruCache(std::size_t max_bytes, std::size_t max_entries)
      : max_bytes_(max_bytes), max_entries_(max_entries) {}

  LruCache(const LruCache&) = delete;
  LruCache& operator=(const LruCache&) = delete;

  // Returns the value stored for `key` and marks it most recently used, or
  // nullptr on a miss. The returned pointer stays valid until the next mutating
  // call on this cache (Get, Put, Clear), so callers must use it immediately.
  const Value* Get(const Key& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
      return nullptr;
    }
    order_.splice(order_.begin(), order_, found->second);
    return &found->second->value;
  }

  // Stores `value` under `key`, charging `bytes` against the budget and
  // evicting the least recently used entries until both budgets hold. A value
  // that alone exceeds the byte budget is not cached (it would flush everything
  // else for one entry). Returns the stored value or nullptr when it was not
  // stored (too large or out of memory).
  const Value* Put(Key key, Value value, std::size_t bytes) {
    if (bytes > max_bytes_) {
      return nullptr;
    }
    try {
      const auto found = entries_.find(key);
      if (found != entries_.end()) {
        Node& node = *found->second;
        total_bytes_ -= node.bytes;
        node.value = std::move(value);
        node.bytes = bytes;
        total_bytes_ += bytes;
        order_.splice(order_.begin(), order_, found->second);
        return &node.value;
      }
      order_.emplace_front(Node{std::move(key), std::move(value), bytes});
      try {
        entries_.emplace(order_.front().key, order_.begin());
      } catch (...) {
        order_.pop_front();
        throw;
      }
      total_bytes_ += bytes;
      Evict();
      return order_.empty() ? nullptr : &order_.front().value;
    } catch (const std::bad_alloc&) {
      // The cache is a pure optimisation: drop its contents (never rendering
      // data) and report a miss so the caller keeps the locally computed value.
      Clear();
      return nullptr;
    }
  }

  // Drops every entry. Used on allocation failure and by tests; no-throw.
  void Clear() noexcept {
    order_.clear();
    entries_.clear();
    total_bytes_ = 0;
  }

  std::size_t bytes() const { return total_bytes_; }
  std::size_t size() const { return order_.size(); }

 private:
  struct Node {
    Key key;
    Value value;
    std::size_t bytes = 0;
  };

  using Order = std::list<Node>;
  using Iterator = typename Order::iterator;

  void Evict() {
    while (!order_.empty() && (total_bytes_ > max_bytes_ || order_.size() > max_entries_)) {
      const auto last = std::prev(order_.end());
      total_bytes_ -= last->bytes;
      entries_.erase(last->key);
      order_.erase(last);
    }
  }

  Order order_;
  std::unordered_map<Key, Iterator> entries_;
  std::size_t total_bytes_ = 0;
  std::size_t max_bytes_ = 0;
  std::size_t max_entries_ = 0;
};

}  // namespace toy::render::internal
