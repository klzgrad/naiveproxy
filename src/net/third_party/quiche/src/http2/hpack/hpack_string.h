// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HPACK_STRING_H_
#define QUICHE_HTTP2_HPACK_HPACK_STRING_H_

// HpackString is currently a very simple container for a string, but allows us
// to relatively easily experiment with alternate string storage mechanisms for
// handling strings to be encoded with HPACK, or decoded from HPACK, such as
// a ref-counted string.

#include <stddef.h>

#include <iosfwd>
#include <string>

#include "absl/strings/string_view.h"
#include "common/platform/api/quiche_export.h"

namespace http2 {

class QUICHE_EXPORT_PRIVATE HpackString {
 public:
  explicit HpackString(const char* data);
  explicit HpackString(absl::string_view str);
  explicit HpackString(std::string str);
  HpackString(const HpackString& other);

  // Not sure yet whether this move ctor is required/sensible.
  HpackString(HpackString&& other) = default;

  ~HpackString();

  size_t size() const { return str_.size(); }
  const std::string& ToString() const { return str_; }
  absl::string_view ToStringPiece() const;

  bool operator==(const HpackString& other) const;

  bool operator==(absl::string_view str) const;

 private:
  std::string str_;
};

QUICHE_EXPORT_PRIVATE bool operator==(absl::string_view a,
                                      const HpackString& b);
QUICHE_EXPORT_PRIVATE bool operator!=(absl::string_view a,
                                      const HpackString& b);
QUICHE_EXPORT_PRIVATE bool operator!=(const HpackString& a,
                                      const HpackString& b);
QUICHE_EXPORT_PRIVATE bool operator!=(const HpackString& a,
                                      absl::string_view b);
QUICHE_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                               const HpackString& v);

struct QUICHE_EXPORT_PRIVATE HpackStringPair {
  HpackStringPair(const HpackString& name, const HpackString& value);
  HpackStringPair(absl::string_view name, absl::string_view value);
  ~HpackStringPair();

  // Returns the size of a header entry with this name and value, per the RFC:
  // http://httpwg.org/specs/rfc7541.html#calculating.table.size
  size_t size() const { return 32 + name.size() + value.size(); }

  std::string DebugString() const;

  const HpackString name;
  const HpackString value;
};

QUICHE_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                               const HpackStringPair& p);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_HPACK_STRING_H_
