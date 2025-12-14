// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HPACK_ENTRY_H_
#define QUICHE_HTTP2_HPACK_HPACK_ENTRY_H_

#include <cstddef>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

// All section references below are to
// http://tools.ietf.org/html/draft-ietf-httpbis-header-compression-08

namespace spdy {

// The constant amount added to name().size() and value().size() to
// get the size of an HpackEntry as defined in 5.1.
inline constexpr size_t kHpackEntrySizeOverhead = 32;

// A structure for looking up entries in the static and dynamic tables.
struct QUICHE_EXPORT HpackLookupEntry {
  absl::string_view name;
  absl::string_view value;

  bool operator==(const HpackLookupEntry& other) const {
    return name == other.name && value == other.value;
  }

  // Abseil hashing framework extension according to absl/hash/hash.h:
  template <typename H>
  friend H AbslHashValue(H h, const HpackLookupEntry& entry) {
    return H::combine(std::move(h), entry.name, entry.value);
  }
};

// A structure for an entry in the static table (3.3.1)
// and the header table (3.3.2).
class QUICHE_EXPORT HpackEntry {
 public:
  HpackEntry(std::string name, std::string value);

  // Make HpackEntry non-copyable to make sure it is always moved.
  HpackEntry(const HpackEntry&) = delete;
  HpackEntry& operator=(const HpackEntry&) = delete;

  HpackEntry(HpackEntry&&) = default;
  HpackEntry& operator=(HpackEntry&&) = default;

  // Getters for std::string members traditionally return const std::string&.
  // However, HpackHeaderTable uses string_view as keys in the maps
  // static_name_index_ and dynamic_name_index_.  If HpackEntry::name() returned
  // const std::string&, then
  //   dynamic_name_index_.insert(std::make_pair(entry.name(), index));
  // would silently create a dangling reference: make_pair infers type from the
  // return type of entry.name() and silently creates a temporary string copy.
  // Insert creates a string_view that points to this copy, which then
  // immediately goes out of scope and gets destroyed.  While this is quite easy
  // to avoid, for example, by explicitly specifying type as a template
  // parameter to make_pair, returning string_view here is less error-prone.
  absl::string_view name() const { return name_; }
  absl::string_view value() const { return value_; }

  // Returns the size of an entry as defined in 5.1.
  static size_t Size(absl::string_view name, absl::string_view value);
  size_t Size() const;

  std::string GetDebugString() const;

 private:
  std::string name_;
  std::string value_;
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_HPACK_HPACK_ENTRY_H_
