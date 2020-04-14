// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_TOOLS_HPACK_BLOCK_BUILDER_H_
#define QUICHE_HTTP2_HPACK_TOOLS_HPACK_BLOCK_BUILDER_H_

// HpackBlockBuilder builds wire-format HPACK blocks (or fragments thereof)
// from components.

// Supports very large varints to enable tests to create HPACK blocks with
// values that the decoder should reject. For now, this is only intended for
// use in tests, and thus has EXPECT* in the code. If desired to use it in an
// encoder, it will need optimization work, especially w.r.t memory mgmt, and
// the EXPECT* will need to be removed or replaced with DCHECKs. And of course
// the support for very large varints will not be needed in production code.

#include <stddef.h>

#include <cstdint>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {
namespace test {

class HpackBlockBuilder {
 public:
  explicit HpackBlockBuilder(quiche::QuicheStringPiece initial_contents)
      : buffer_(initial_contents.data(), initial_contents.size()) {}
  HpackBlockBuilder() {}
  ~HpackBlockBuilder() {}

  size_t size() const { return buffer_.size(); }
  const std::string& buffer() const { return buffer_; }

  //----------------------------------------------------------------------------
  // Methods for appending a valid HPACK entry.

  void AppendIndexedHeader(uint64_t index) {
    AppendEntryTypeAndVarint(HpackEntryType::kIndexedHeader, index);
  }

  void AppendDynamicTableSizeUpdate(uint64_t size) {
    AppendEntryTypeAndVarint(HpackEntryType::kDynamicTableSizeUpdate, size);
  }

  void AppendNameIndexAndLiteralValue(HpackEntryType entry_type,
                                      uint64_t name_index,
                                      bool value_is_huffman_encoded,
                                      quiche::QuicheStringPiece value) {
    // name_index==0 would indicate that the entry includes a literal name.
    // Call AppendLiteralNameAndValue in that case.
    EXPECT_NE(0u, name_index);
    AppendEntryTypeAndVarint(entry_type, name_index);
    AppendString(value_is_huffman_encoded, value);
  }

  void AppendLiteralNameAndValue(HpackEntryType entry_type,
                                 bool name_is_huffman_encoded,
                                 quiche::QuicheStringPiece name,
                                 bool value_is_huffman_encoded,
                                 quiche::QuicheStringPiece value) {
    AppendEntryTypeAndVarint(entry_type, 0);
    AppendString(name_is_huffman_encoded, name);
    AppendString(value_is_huffman_encoded, value);
  }

  //----------------------------------------------------------------------------
  // Primitive methods that are not guaranteed to write a valid HPACK entry.

  // Appends a varint, with the specified high_bits above the prefix of the
  // varint.
  void AppendHighBitsAndVarint(uint8_t high_bits,
                               uint8_t prefix_length,
                               uint64_t varint);

  // Append the start of an HPACK entry for the specified type, with the
  // specified varint.
  void AppendEntryTypeAndVarint(HpackEntryType entry_type, uint64_t varint);

  // Append a header string (i.e. a header name or value) in HPACK format.
  // Does NOT perform Huffman encoding.
  void AppendString(bool is_huffman_encoded, quiche::QuicheStringPiece str);

 private:
  std::string buffer_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_TOOLS_HPACK_BLOCK_BUILDER_H_
