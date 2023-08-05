// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/test_tools/hpack_string_collector.h"

#include <stddef.h>

#include <iosfwd>
#include <ostream>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "quiche/http2/test_tools/verify_macros.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace test {
namespace {

std::ostream& operator<<(std::ostream& out,
                         HpackStringCollector::CollectorState v) {
  switch (v) {
    case HpackStringCollector::CollectorState::kGenesis:
      return out << "kGenesis";
    case HpackStringCollector::CollectorState::kStarted:
      return out << "kStarted";
    case HpackStringCollector::CollectorState::kEnded:
      return out << "kEnded";
  }
  return out << "UnknownCollectorState";
}

}  // namespace

HpackStringCollector::HpackStringCollector() { Clear(); }

HpackStringCollector::HpackStringCollector(const std::string& str, bool huffman)
    : s(str), len(str.size()), huffman_encoded(huffman), state(kEnded) {}

void HpackStringCollector::Clear() {
  s = "";
  len = 0;
  huffman_encoded = false;
  state = kGenesis;
}

bool HpackStringCollector::IsClear() const {
  return s.empty() && len == 0 && huffman_encoded == false && state == kGenesis;
}

bool HpackStringCollector::IsInProgress() const { return state == kStarted; }

bool HpackStringCollector::HasEnded() const { return state == kEnded; }

void HpackStringCollector::OnStringStart(bool huffman, size_t length) {
  EXPECT_TRUE(IsClear()) << ToString();
  state = kStarted;
  huffman_encoded = huffman;
  len = length;
}

void HpackStringCollector::OnStringData(const char* data, size_t length) {
  absl::string_view sp(data, length);
  EXPECT_TRUE(IsInProgress()) << ToString();
  EXPECT_LE(sp.size(), len) << ToString();
  absl::StrAppend(&s, sp);
  EXPECT_LE(s.size(), len) << ToString();
}

void HpackStringCollector::OnStringEnd() {
  EXPECT_TRUE(IsInProgress()) << ToString();
  EXPECT_EQ(s.size(), len) << ToString();
  state = kEnded;
}

::testing::AssertionResult HpackStringCollector::Collected(
    absl::string_view str, bool is_huffman_encoded) const {
  HTTP2_VERIFY_TRUE(HasEnded());
  HTTP2_VERIFY_EQ(str.size(), len);
  HTTP2_VERIFY_EQ(is_huffman_encoded, huffman_encoded);
  HTTP2_VERIFY_EQ(str, s);
  return ::testing::AssertionSuccess();
}

std::string HpackStringCollector::ToString() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

bool operator==(const HpackStringCollector& a, const HpackStringCollector& b) {
  return a.s == b.s && a.len == b.len &&
         a.huffman_encoded == b.huffman_encoded && a.state == b.state;
}

bool operator!=(const HpackStringCollector& a, const HpackStringCollector& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& out, const HpackStringCollector& v) {
  out << "HpackStringCollector(state=" << v.state;
  if (v.state == HpackStringCollector::kGenesis) {
    return out << ")";
  }
  if (v.huffman_encoded) {
    out << ", Huffman Encoded";
  }
  out << ", Length=" << v.len;
  if (!v.s.empty() && v.len != v.s.size()) {
    out << " (" << v.s.size() << ")";
  }
  return out << ", String=\"" << absl::CHexEscape(v.s) << "\")";
}

}  // namespace test
}  // namespace http2
