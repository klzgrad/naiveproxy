// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/hpack_string.h"

#include <utility>

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"

namespace http2 {

HpackString::HpackString(const char* data) : str_(data) {}
HpackString::HpackString(Http2StringPiece str) : str_(std::string(str)) {}
HpackString::HpackString(std::string str) : str_(std::move(str)) {}
HpackString::HpackString(const HpackString& other) = default;
HpackString::~HpackString() = default;

Http2StringPiece HpackString::ToStringPiece() const {
  return str_;
}

bool HpackString::operator==(const HpackString& other) const {
  return str_ == other.str_;
}
bool HpackString::operator==(Http2StringPiece str) const {
  return str == str_;
}

bool operator==(Http2StringPiece a, const HpackString& b) {
  return b == a;
}
bool operator!=(Http2StringPiece a, const HpackString& b) {
  return !(b == a);
}
bool operator!=(const HpackString& a, const HpackString& b) {
  return !(a == b);
}
bool operator!=(const HpackString& a, Http2StringPiece b) {
  return !(a == b);
}
std::ostream& operator<<(std::ostream& out, const HpackString& v) {
  return out << v.ToString();
}

HpackStringPair::HpackStringPair(const HpackString& name,
                                 const HpackString& value)
    : name(name), value(value) {
  HTTP2_DVLOG(3) << DebugString() << " ctor";
}

HpackStringPair::HpackStringPair(Http2StringPiece name, Http2StringPiece value)
    : name(name), value(value) {
  HTTP2_DVLOG(3) << DebugString() << " ctor";
}

HpackStringPair::~HpackStringPair() {
  HTTP2_DVLOG(3) << DebugString() << " dtor";
}

std::string HpackStringPair::DebugString() const {
  return Http2StrCat("HpackStringPair(name=", name.ToString(),
                     ", value=", value.ToString(), ")");
}

std::ostream& operator<<(std::ostream& os, const HpackStringPair& p) {
  os << p.DebugString();
  return os;
}

}  // namespace http2
