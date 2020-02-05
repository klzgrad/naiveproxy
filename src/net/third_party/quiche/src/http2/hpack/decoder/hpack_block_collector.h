// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_COLLECTOR_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_COLLECTOR_H_

// HpackBlockCollector implements HpackEntryDecoderListener in order to record
// the calls using HpackEntryCollector instances (one per HPACK entry). This
// supports testing of HpackBlockDecoder, which decodes entire HPACK blocks.
//
// In addition to implementing the callback methods, HpackBlockCollector also
// supports comparing two HpackBlockCollector instances (i.e. an expected and
// an actual), or a sole HPACK entry against an expected value.

#include <stddef.h>

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_collector.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder_listener.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_piece.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

namespace http2 {
namespace test {

class HpackBlockCollector : public HpackEntryDecoderListener {
 public:
  HpackBlockCollector();
  HpackBlockCollector(const HpackBlockCollector& other);
  ~HpackBlockCollector() override;

  // Implementations of HpackEntryDecoderListener, forwarding to pending_entry_,
  // an HpackEntryCollector for the "in-progress" HPACK entry. OnIndexedHeader
  // and OnDynamicTableSizeUpdate are pending only for that one call, while
  // OnStartLiteralHeader is followed by many calls, ending with OnValueEnd.
  // Once all the calls for one HPACK entry have been received, PushPendingEntry
  // is used to append the pending_entry_ entry to the collected entries_.
  void OnIndexedHeader(size_t index) override;
  void OnDynamicTableSizeUpdate(size_t size) override;
  void OnStartLiteralHeader(HpackEntryType header_type,
                            size_t maybe_name_index) override;
  void OnNameStart(bool huffman_encoded, size_t len) override;
  void OnNameData(const char* data, size_t len) override;
  void OnNameEnd() override;
  void OnValueStart(bool huffman_encoded, size_t len) override;
  void OnValueData(const char* data, size_t len) override;
  void OnValueEnd() override;

  // Methods for creating a set of expectations (i.e. HPACK entries to compare
  // against those collected by another instance of HpackBlockCollector).

  // Add an HPACK entry for an indexed header.
  void ExpectIndexedHeader(size_t index);

  // Add an HPACK entry for a dynamic table size update.
  void ExpectDynamicTableSizeUpdate(size_t size);

  // Add an HPACK entry for a header entry with an index for the name, and a
  // literal value.
  void ExpectNameIndexAndLiteralValue(HpackEntryType type,
                                      size_t index,
                                      bool value_huffman,
                                      const std::string& value);

  // Add an HPACK entry for a header entry with a literal name and value.
  void ExpectLiteralNameAndValue(HpackEntryType type,
                                 bool name_huffman,
                                 const std::string& name,
                                 bool value_huffman,
                                 const std::string& value);

  // Shuffle the entries, in support of generating an HPACK block of entries
  // in some random order.
  void ShuffleEntries(Http2Random* rng);

  // Serialize entries_ to the HpackBlockBuilder.
  void AppendToHpackBlockBuilder(HpackBlockBuilder* hbb) const;

  // Return AssertionSuccess if there is just one entry, and it is an
  // Indexed Header with the specified index.
  ::testing::AssertionResult ValidateSoleIndexedHeader(size_t ndx) const;

  // Return AssertionSuccess if there is just one entry, and it is a
  // Dynamic Table Size Update with the specified size.
  ::testing::AssertionResult ValidateSoleDynamicTableSizeUpdate(
      size_t size) const;

  // Return AssertionSuccess if there is just one entry, and it is a Header
  // entry with an index for the name and a literal value.
  ::testing::AssertionResult ValidateSoleLiteralValueHeader(
      HpackEntryType expected_type,
      size_t expected_index,
      bool expected_value_huffman,
      Http2StringPiece expected_value) const;

  // Return AssertionSuccess if there is just one entry, and it is a Header
  // with a literal name and literal value.
  ::testing::AssertionResult ValidateSoleLiteralNameValueHeader(
      HpackEntryType expected_type,
      bool expected_name_huffman,
      Http2StringPiece expected_name,
      bool expected_value_huffman,
      Http2StringPiece expected_value) const;

  bool IsNotPending() const { return pending_entry_.IsClear(); }
  bool IsClear() const { return IsNotPending() && entries_.empty(); }
  void Clear();

  ::testing::AssertionResult VerifyEq(const HpackBlockCollector& that) const;

 private:
  // Push the value of pending_entry_ onto entries_, and clear pending_entry_.
  // The pending_entry_ must be complete.
  void PushPendingEntry();

  HpackEntryCollector pending_entry_;
  std::vector<HpackEntryCollector> entries_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_COLLECTOR_H_
