// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_COLLECTOR_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_COLLECTOR_H_

// HpackEntryCollector records calls to HpackEntryDecoderListener in support
// of tests of HpackEntryDecoder, or which use it. Can only record the callbacks
// for the decoding of a single entry; call Clear() between decoding successive
// entries or use a distinct HpackEntryCollector for each entry.

#include <stddef.h>

#include <iosfwd>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder_listener.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_collector.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {
namespace test {

class HpackEntryCollector : public HpackEntryDecoderListener {
 public:
  HpackEntryCollector();
  HpackEntryCollector(const HpackEntryCollector& other);

  // These next three constructors are intended for use in tests that create
  // an HpackEntryCollector "manually", and then compare it against another
  // that is populated via calls to the HpackEntryDecoderListener methods.
  HpackEntryCollector(HpackEntryType type, size_t index_or_size);
  HpackEntryCollector(HpackEntryType type,
                      size_t index,
                      bool value_huffman,
                      const std::string& value);
  HpackEntryCollector(HpackEntryType type,
                      bool name_huffman,
                      const std::string& name,
                      bool value_huffman,
                      const std::string& value);

  ~HpackEntryCollector() override;

  // Methods defined by HpackEntryDecoderListener.
  void OnIndexedHeader(size_t index) override;
  void OnStartLiteralHeader(HpackEntryType header_type,
                            size_t maybe_name_index) override;
  void OnNameStart(bool huffman_encoded, size_t len) override;
  void OnNameData(const char* data, size_t len) override;
  void OnNameEnd() override;
  void OnValueStart(bool huffman_encoded, size_t len) override;
  void OnValueData(const char* data, size_t len) override;
  void OnValueEnd() override;
  void OnDynamicTableSizeUpdate(size_t size) override;

  // Clears the fields of the collector so that it is ready to start collecting
  // another HPACK block entry.
  void Clear();

  // Is the collector ready to start collecting another HPACK block entry.
  bool IsClear() const;

  // Has a complete entry been collected?
  bool IsComplete() const;

  // Based on the HpackEntryType, is a literal name expected?
  bool LiteralNameExpected() const;

  // Based on the HpackEntryType, is a literal value expected?
  bool LiteralValueExpected() const;

  // Returns success if collected an Indexed Header (i.e. OnIndexedHeader was
  // called).
  ::testing::AssertionResult ValidateIndexedHeader(size_t expected_index) const;

  // Returns success if collected a Header with an indexed name and literal
  // value (i.e. OnStartLiteralHeader was called with a non-zero index for
  // the name, which must match expected_index).
  ::testing::AssertionResult ValidateLiteralValueHeader(
      HpackEntryType expected_type,
      size_t expected_index,
      bool expected_value_huffman,
      quiche::QuicheStringPiece expected_value) const;

  // Returns success if collected a Header with an literal name and literal
  // value.
  ::testing::AssertionResult ValidateLiteralNameValueHeader(
      HpackEntryType expected_type,
      bool expected_name_huffman,
      quiche::QuicheStringPiece expected_name,
      bool expected_value_huffman,
      quiche::QuicheStringPiece expected_value) const;

  // Returns success if collected a Dynamic Table Size Update,
  // with the specified size.
  ::testing::AssertionResult ValidateDynamicTableSizeUpdate(
      size_t expected_size) const;

  void set_header_type(HpackEntryType v) { header_type_ = v; }
  HpackEntryType header_type() const { return header_type_; }

  void set_index(size_t v) { index_ = v; }
  size_t index() const { return index_; }

  void set_name(const HpackStringCollector& v) { name_ = v; }
  const HpackStringCollector& name() const { return name_; }

  void set_value(const HpackStringCollector& v) { value_ = v; }
  const HpackStringCollector& value() const { return value_; }

  void set_started(bool v) { started_ = v; }
  bool started() const { return started_; }

  void set_ended(bool v) { ended_ = v; }
  bool ended() const { return ended_; }

  void AppendToHpackBlockBuilder(HpackBlockBuilder* hbb) const;

  // Returns a debug string.
  std::string ToString() const;

 private:
  void Init(HpackEntryType type, size_t maybe_index);

  HpackEntryType header_type_;
  size_t index_;

  HpackStringCollector name_;
  HpackStringCollector value_;

  // True if has received a call to an HpackEntryDecoderListener method
  // indicating the start of decoding an HPACK entry; for example,
  // OnIndexedHeader set it true, but OnNameStart does not change it.
  bool started_ = false;

  // True if has received a call to an HpackEntryDecoderListener method
  // indicating the end of decoding an HPACK entry; for example,
  // OnIndexedHeader and OnValueEnd both set it true, but OnNameEnd does
  // not change it.
  bool ended_ = false;
};

bool operator==(const HpackEntryCollector& a, const HpackEntryCollector& b);
bool operator!=(const HpackEntryCollector& a, const HpackEntryCollector& b);
std::ostream& operator<<(std::ostream& out, const HpackEntryCollector& v);

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_COLLECTOR_H_
