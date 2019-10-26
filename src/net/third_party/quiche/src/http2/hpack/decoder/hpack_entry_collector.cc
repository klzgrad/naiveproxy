// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_collector.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_collector.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"

using ::testing::AssertionResult;

namespace http2 {
namespace test {
namespace {

const HpackEntryType kInvalidHeaderType = static_cast<HpackEntryType>(99);
const size_t kInvalidIndex = 99999999;

}  // namespace

HpackEntryCollector::HpackEntryCollector() {
  Clear();
}

HpackEntryCollector::HpackEntryCollector(const HpackEntryCollector& other) =
    default;

HpackEntryCollector::HpackEntryCollector(HpackEntryType type,
                                         size_t index_or_size)
    : header_type_(type), index_(index_or_size), started_(true), ended_(true) {}
HpackEntryCollector::HpackEntryCollector(HpackEntryType type,
                                         size_t index,
                                         bool value_huffman,
                                         const std::string& value)
    : header_type_(type),
      index_(index),
      value_(value, value_huffman),
      started_(true),
      ended_(true) {}
HpackEntryCollector::HpackEntryCollector(HpackEntryType type,
                                         bool name_huffman,
                                         const std::string& name,
                                         bool value_huffman,
                                         const std::string& value)
    : header_type_(type),
      index_(0),
      name_(name, name_huffman),
      value_(value, value_huffman),
      started_(true),
      ended_(true) {}

HpackEntryCollector::~HpackEntryCollector() = default;

void HpackEntryCollector::OnIndexedHeader(size_t index) {
  ASSERT_FALSE(started_);
  ASSERT_TRUE(IsClear()) << ToString();
  Init(HpackEntryType::kIndexedHeader, index);
  ended_ = true;
}
void HpackEntryCollector::OnStartLiteralHeader(HpackEntryType header_type,
                                               size_t maybe_name_index) {
  ASSERT_FALSE(started_);
  ASSERT_TRUE(IsClear()) << ToString();
  Init(header_type, maybe_name_index);
}
void HpackEntryCollector::OnNameStart(bool huffman_encoded, size_t len) {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  ASSERT_FALSE(IsClear());
  ASSERT_TRUE(LiteralNameExpected()) << ToString();
  name_.OnStringStart(huffman_encoded, len);
}
void HpackEntryCollector::OnNameData(const char* data, size_t len) {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  ASSERT_TRUE(LiteralNameExpected()) << ToString();
  ASSERT_TRUE(name_.IsInProgress());
  name_.OnStringData(data, len);
}
void HpackEntryCollector::OnNameEnd() {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  ASSERT_TRUE(LiteralNameExpected()) << ToString();
  ASSERT_TRUE(name_.IsInProgress());
  name_.OnStringEnd();
}
void HpackEntryCollector::OnValueStart(bool huffman_encoded, size_t len) {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  if (LiteralNameExpected()) {
    ASSERT_TRUE(name_.HasEnded());
  }
  ASSERT_TRUE(LiteralValueExpected()) << ToString();
  ASSERT_TRUE(value_.IsClear()) << value_.ToString();
  value_.OnStringStart(huffman_encoded, len);
}
void HpackEntryCollector::OnValueData(const char* data, size_t len) {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  ASSERT_TRUE(LiteralValueExpected()) << ToString();
  ASSERT_TRUE(value_.IsInProgress());
  value_.OnStringData(data, len);
}
void HpackEntryCollector::OnValueEnd() {
  ASSERT_TRUE(started_);
  ASSERT_FALSE(ended_);
  ASSERT_TRUE(LiteralValueExpected()) << ToString();
  ASSERT_TRUE(value_.IsInProgress());
  value_.OnStringEnd();
  ended_ = true;
}
void HpackEntryCollector::OnDynamicTableSizeUpdate(size_t size) {
  ASSERT_FALSE(started_);
  ASSERT_TRUE(IsClear()) << ToString();
  Init(HpackEntryType::kDynamicTableSizeUpdate, size);
  ended_ = true;
}

void HpackEntryCollector::Clear() {
  header_type_ = kInvalidHeaderType;
  index_ = kInvalidIndex;
  name_.Clear();
  value_.Clear();
  started_ = ended_ = false;
}
bool HpackEntryCollector::IsClear() const {
  return header_type_ == kInvalidHeaderType && index_ == kInvalidIndex &&
         name_.IsClear() && value_.IsClear() && !started_ && !ended_;
}
bool HpackEntryCollector::IsComplete() const {
  return started_ && ended_;
}
bool HpackEntryCollector::LiteralNameExpected() const {
  switch (header_type_) {
    case HpackEntryType::kIndexedLiteralHeader:
    case HpackEntryType::kUnindexedLiteralHeader:
    case HpackEntryType::kNeverIndexedLiteralHeader:
      return index_ == 0;
    default:
      return false;
  }
}
bool HpackEntryCollector::LiteralValueExpected() const {
  switch (header_type_) {
    case HpackEntryType::kIndexedLiteralHeader:
    case HpackEntryType::kUnindexedLiteralHeader:
    case HpackEntryType::kNeverIndexedLiteralHeader:
      return true;
    default:
      return false;
  }
}
AssertionResult HpackEntryCollector::ValidateIndexedHeader(
    size_t expected_index) const {
  VERIFY_TRUE(started_);
  VERIFY_TRUE(ended_);
  VERIFY_EQ(HpackEntryType::kIndexedHeader, header_type_);
  VERIFY_EQ(expected_index, index_);
  return ::testing::AssertionSuccess();
}
AssertionResult HpackEntryCollector::ValidateLiteralValueHeader(
    HpackEntryType expected_type,
    size_t expected_index,
    bool expected_value_huffman,
    Http2StringPiece expected_value) const {
  VERIFY_TRUE(started_);
  VERIFY_TRUE(ended_);
  VERIFY_EQ(expected_type, header_type_);
  VERIFY_NE(0u, expected_index);
  VERIFY_EQ(expected_index, index_);
  VERIFY_TRUE(name_.IsClear());
  VERIFY_SUCCESS(value_.Collected(expected_value, expected_value_huffman));
  return ::testing::AssertionSuccess();
}
AssertionResult HpackEntryCollector::ValidateLiteralNameValueHeader(
    HpackEntryType expected_type,
    bool expected_name_huffman,
    Http2StringPiece expected_name,
    bool expected_value_huffman,
    Http2StringPiece expected_value) const {
  VERIFY_TRUE(started_);
  VERIFY_TRUE(ended_);
  VERIFY_EQ(expected_type, header_type_);
  VERIFY_EQ(0u, index_);
  VERIFY_SUCCESS(name_.Collected(expected_name, expected_name_huffman));
  VERIFY_SUCCESS(value_.Collected(expected_value, expected_value_huffman));
  return ::testing::AssertionSuccess();
}
AssertionResult HpackEntryCollector::ValidateDynamicTableSizeUpdate(
    size_t size) const {
  VERIFY_TRUE(started_);
  VERIFY_TRUE(ended_);
  VERIFY_EQ(HpackEntryType::kDynamicTableSizeUpdate, header_type_);
  VERIFY_EQ(index_, size);
  return ::testing::AssertionSuccess();
}

void HpackEntryCollector::AppendToHpackBlockBuilder(
    HpackBlockBuilder* hbb) const {
  ASSERT_TRUE(started_ && ended_) << *this;
  switch (header_type_) {
    case HpackEntryType::kIndexedHeader:
      hbb->AppendIndexedHeader(index_);
      return;

    case HpackEntryType::kDynamicTableSizeUpdate:
      hbb->AppendDynamicTableSizeUpdate(index_);
      return;

    case HpackEntryType::kIndexedLiteralHeader:
    case HpackEntryType::kUnindexedLiteralHeader:
    case HpackEntryType::kNeverIndexedLiteralHeader:
      ASSERT_TRUE(value_.HasEnded()) << *this;
      if (index_ != 0) {
        CHECK(name_.IsClear());
        hbb->AppendNameIndexAndLiteralValue(header_type_, index_,
                                            value_.huffman_encoded, value_.s);
      } else {
        CHECK(name_.HasEnded()) << *this;
        hbb->AppendLiteralNameAndValue(header_type_, name_.huffman_encoded,
                                       name_.s, value_.huffman_encoded,
                                       value_.s);
      }
      return;

    default:
      ADD_FAILURE() << *this;
  }
}

std::string HpackEntryCollector::ToString() const {
  std::string result("Type=");
  switch (header_type_) {
    case HpackEntryType::kIndexedHeader:
      result += "IndexedHeader";
      break;
    case HpackEntryType::kDynamicTableSizeUpdate:
      result += "DynamicTableSizeUpdate";
      break;
    case HpackEntryType::kIndexedLiteralHeader:
      result += "IndexedLiteralHeader";
      break;
    case HpackEntryType::kUnindexedLiteralHeader:
      result += "UnindexedLiteralHeader";
      break;
    case HpackEntryType::kNeverIndexedLiteralHeader:
      result += "NeverIndexedLiteralHeader";
      break;
    default:
      if (header_type_ == kInvalidHeaderType) {
        result += "<unset>";
      } else {
        Http2StrAppend(&result, header_type_);
      }
  }
  if (index_ != 0) {
    Http2StrAppend(&result, " Index=", index_);
  }
  if (!name_.IsClear()) {
    Http2StrAppend(&result, " Name", name_.ToString());
  }
  if (!value_.IsClear()) {
    Http2StrAppend(&result, " Value", value_.ToString());
  }
  if (!started_) {
    EXPECT_FALSE(ended_);
    Http2StrAppend(&result, " !started");
  } else if (!ended_) {
    Http2StrAppend(&result, " !ended");
  } else {
    Http2StrAppend(&result, " Complete");
  }
  return result;
}

void HpackEntryCollector::Init(HpackEntryType type, size_t maybe_index) {
  ASSERT_TRUE(IsClear()) << ToString();
  header_type_ = type;
  index_ = maybe_index;
  started_ = true;
}

bool operator==(const HpackEntryCollector& a, const HpackEntryCollector& b) {
  return a.name() == b.name() && a.value() == b.value() &&
         a.index() == b.index() && a.header_type() == b.header_type() &&
         a.started() == b.started() && a.ended() == b.ended();
}
bool operator!=(const HpackEntryCollector& a, const HpackEntryCollector& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& out, const HpackEntryCollector& v) {
  return out << v.ToString();
}

}  // namespace test
}  // namespace http2
