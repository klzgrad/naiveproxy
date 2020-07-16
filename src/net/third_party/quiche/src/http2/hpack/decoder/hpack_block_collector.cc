// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_block_collector.h"

#include <algorithm>
#include <memory>

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"

using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {

HpackBlockCollector::HpackBlockCollector() = default;
HpackBlockCollector::HpackBlockCollector(const HpackBlockCollector& other)
    : pending_entry_(other.pending_entry_), entries_(other.entries_) {}
HpackBlockCollector::~HpackBlockCollector() = default;

void HpackBlockCollector::OnIndexedHeader(size_t index) {
  pending_entry_.OnIndexedHeader(index);
  PushPendingEntry();
}
void HpackBlockCollector::OnDynamicTableSizeUpdate(size_t size) {
  pending_entry_.OnDynamicTableSizeUpdate(size);
  PushPendingEntry();
}
void HpackBlockCollector::OnStartLiteralHeader(HpackEntryType header_type,
                                               size_t maybe_name_index) {
  pending_entry_.OnStartLiteralHeader(header_type, maybe_name_index);
}
void HpackBlockCollector::OnNameStart(bool huffman_encoded, size_t len) {
  pending_entry_.OnNameStart(huffman_encoded, len);
}
void HpackBlockCollector::OnNameData(const char* data, size_t len) {
  pending_entry_.OnNameData(data, len);
}
void HpackBlockCollector::OnNameEnd() {
  pending_entry_.OnNameEnd();
}
void HpackBlockCollector::OnValueStart(bool huffman_encoded, size_t len) {
  pending_entry_.OnValueStart(huffman_encoded, len);
}
void HpackBlockCollector::OnValueData(const char* data, size_t len) {
  pending_entry_.OnValueData(data, len);
}
void HpackBlockCollector::OnValueEnd() {
  pending_entry_.OnValueEnd();
  PushPendingEntry();
}

void HpackBlockCollector::PushPendingEntry() {
  EXPECT_TRUE(pending_entry_.IsComplete());
  HTTP2_DVLOG(2) << "PushPendingEntry: " << pending_entry_;
  entries_.push_back(pending_entry_);
  EXPECT_TRUE(entries_.back().IsComplete());
  pending_entry_.Clear();
}
void HpackBlockCollector::Clear() {
  pending_entry_.Clear();
  entries_.clear();
}

void HpackBlockCollector::ExpectIndexedHeader(size_t index) {
  entries_.push_back(
      HpackEntryCollector(HpackEntryType::kIndexedHeader, index));
}
void HpackBlockCollector::ExpectDynamicTableSizeUpdate(size_t size) {
  entries_.push_back(
      HpackEntryCollector(HpackEntryType::kDynamicTableSizeUpdate, size));
}
void HpackBlockCollector::ExpectNameIndexAndLiteralValue(
    HpackEntryType type,
    size_t index,
    bool value_huffman,
    const std::string& value) {
  entries_.push_back(HpackEntryCollector(type, index, value_huffman, value));
}
void HpackBlockCollector::ExpectLiteralNameAndValue(HpackEntryType type,
                                                    bool name_huffman,
                                                    const std::string& name,
                                                    bool value_huffman,
                                                    const std::string& value) {
  entries_.push_back(
      HpackEntryCollector(type, name_huffman, name, value_huffman, value));
}

void HpackBlockCollector::ShuffleEntries(Http2Random* rng) {
  std::shuffle(entries_.begin(), entries_.end(), *rng);
}

void HpackBlockCollector::AppendToHpackBlockBuilder(
    HpackBlockBuilder* hbb) const {
  CHECK(IsNotPending());
  for (const auto& entry : entries_) {
    entry.AppendToHpackBlockBuilder(hbb);
  }
}

AssertionResult HpackBlockCollector::ValidateSoleIndexedHeader(
    size_t ndx) const {
  VERIFY_TRUE(pending_entry_.IsClear());
  VERIFY_EQ(1u, entries_.size());
  VERIFY_TRUE(entries_.front().ValidateIndexedHeader(ndx));
  return AssertionSuccess();
}
AssertionResult HpackBlockCollector::ValidateSoleLiteralValueHeader(
    HpackEntryType expected_type,
    size_t expected_index,
    bool expected_value_huffman,
    quiche::QuicheStringPiece expected_value) const {
  VERIFY_TRUE(pending_entry_.IsClear());
  VERIFY_EQ(1u, entries_.size());
  VERIFY_TRUE(entries_.front().ValidateLiteralValueHeader(
      expected_type, expected_index, expected_value_huffman, expected_value));
  return AssertionSuccess();
}
AssertionResult HpackBlockCollector::ValidateSoleLiteralNameValueHeader(
    HpackEntryType expected_type,
    bool expected_name_huffman,
    quiche::QuicheStringPiece expected_name,
    bool expected_value_huffman,
    quiche::QuicheStringPiece expected_value) const {
  VERIFY_TRUE(pending_entry_.IsClear());
  VERIFY_EQ(1u, entries_.size());
  VERIFY_TRUE(entries_.front().ValidateLiteralNameValueHeader(
      expected_type, expected_name_huffman, expected_name,
      expected_value_huffman, expected_value));
  return AssertionSuccess();
}
AssertionResult HpackBlockCollector::ValidateSoleDynamicTableSizeUpdate(
    size_t size) const {
  VERIFY_TRUE(pending_entry_.IsClear());
  VERIFY_EQ(1u, entries_.size());
  VERIFY_TRUE(entries_.front().ValidateDynamicTableSizeUpdate(size));
  return AssertionSuccess();
}

AssertionResult HpackBlockCollector::VerifyEq(
    const HpackBlockCollector& that) const {
  VERIFY_EQ(pending_entry_, that.pending_entry_);
  VERIFY_EQ(entries_, that.entries_);
  return AssertionSuccess();
}

}  // namespace test
}  // namespace http2
