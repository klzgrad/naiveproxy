// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_HPACK_HPACK_STRING_H_
#define NET_HTTP2_HPACK_HPACK_STRING_H_

// HpackString is currently a very simple container for a string, but allows us
// to relatively easily experiment with alternate string storage mechanisms for
// handling strings to be encoded with HPACK, or decoded from HPACK, such as
// a ref-counted string.

#include <stddef.h>

#include <iosfwd>

#include "net/http2/platform/api/http2_export.h"
#include "net/http2/platform/api/http2_string.h"
#include "net/http2/platform/api/http2_string_piece.h"

namespace net {

class HTTP2_EXPORT_PRIVATE HpackString {
 public:
  explicit HpackString(const char* data);
  explicit HpackString(Http2StringPiece str);
  explicit HpackString(Http2String str);
  HpackString(const HpackString& other);

  // Not sure yet whether this move ctor is required/sensible.
  HpackString(HpackString&& other) = default;

  HpackString& operator=(const HpackString& other) = default;

  ~HpackString();

  size_t size() const { return str_.size(); }
  const Http2String& ToString() const { return str_; }
  Http2StringPiece ToStringPiece() const;

  bool operator==(const HpackString& other) const;

  bool operator==(Http2StringPiece str) const;

 private:
  Http2String str_;
};

HTTP2_EXPORT_PRIVATE bool operator==(Http2StringPiece a, const HpackString& b);
HTTP2_EXPORT_PRIVATE bool operator!=(Http2StringPiece a, const HpackString& b);
HTTP2_EXPORT_PRIVATE bool operator!=(const HpackString& a,
                                     const HpackString& b);
HTTP2_EXPORT_PRIVATE bool operator!=(const HpackString& a, Http2StringPiece b);
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const HpackString& v);

struct HTTP2_EXPORT_PRIVATE HpackStringPair {
  HpackStringPair(const HpackString& name, const HpackString& value);
  HpackStringPair(Http2StringPiece name, Http2StringPiece value);
  ~HpackStringPair();

  // Returns the size of a header entry with this name and value, per the RFC:
  // http://httpwg.org/specs/rfc7541.html#calculating.table.size
  size_t size() const { return 32 + name.size() + value.size(); }

  Http2String DebugString() const;

  HpackString name;
  HpackString value;
};

HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                              const HpackStringPair& p);

}  // namespace net

#endif  // NET_HTTP2_HPACK_HPACK_STRING_H_
