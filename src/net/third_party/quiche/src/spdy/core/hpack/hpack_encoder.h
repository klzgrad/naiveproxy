// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HPACK_HPACK_ENCODER_H_
#define QUICHE_SPDY_CORE_HPACK_HPACK_ENCODER_H_

#include <stddef.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_header_table.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

// An HpackEncoder encodes header sets as outlined in
// http://tools.ietf.org/html/rfc7541.

namespace spdy {

class HpackHuffmanTable;

namespace test {
class HpackEncoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE HpackEncoder {
 public:
  using Representation =
      std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>;
  using Representations = std::vector<Representation>;

  // Callers may provide a HeaderListener to be informed of header name-value
  // pairs processed by this encoder.
  using HeaderListener =
      std::function<void(quiche::QuicheStringPiece, quiche::QuicheStringPiece)>;

  // An indexing policy should return true if the provided header name-value
  // pair should be inserted into the HPACK dynamic table.
  using IndexingPolicy =
      std::function<bool(quiche::QuicheStringPiece, quiche::QuicheStringPiece)>;

  // |table| is an initialized HPACK Huffman table, having an
  // externally-managed lifetime which spans beyond HpackEncoder.
  explicit HpackEncoder(const HpackHuffmanTable& table);
  HpackEncoder(const HpackEncoder&) = delete;
  HpackEncoder& operator=(const HpackEncoder&) = delete;
  ~HpackEncoder();

  // Encodes the given header set into the given string. Returns
  // whether or not the encoding was successful.
  bool EncodeHeaderSet(const SpdyHeaderBlock& header_set, std::string* output);

  class QUICHE_EXPORT_PRIVATE ProgressiveEncoder {
   public:
    virtual ~ProgressiveEncoder() {}

    // Returns true iff more remains to encode.
    virtual bool HasNext() const = 0;

    // Encodes up to max_encoded_bytes of the current header block into the
    // given output string.
    virtual void Next(size_t max_encoded_bytes, std::string* output) = 0;
  };

  // Returns a ProgressiveEncoder which must be outlived by both the given
  // SpdyHeaderBlock and this object.
  std::unique_ptr<ProgressiveEncoder> EncodeHeaderSet(
      const SpdyHeaderBlock& header_set);
  // Returns a ProgressiveEncoder which must be outlived by this HpackEncoder.
  // The encoder will not attempt to split any \0-delimited values in
  // |representations|. If such splitting is desired, it must be performed by
  // the caller when constructing the list of representations.
  std::unique_ptr<ProgressiveEncoder> EncodeRepresentations(
      const Representations& representations);

  // Called upon a change to SETTINGS_HEADER_TABLE_SIZE. Specifically, this
  // is to be called after receiving (and sending an acknowledgement for) a
  // SETTINGS_HEADER_TABLE_SIZE update from the remote decoding endpoint.
  void ApplyHeaderTableSizeSetting(size_t size_setting);

  size_t CurrentHeaderTableSizeSetting() const {
    return header_table_.settings_size_bound();
  }

  // This HpackEncoder will use |policy| to determine whether to insert header
  // name-value pairs into the dynamic table.
  void SetIndexingPolicy(IndexingPolicy policy) { should_index_ = policy; }

  // |listener| will be invoked for each header name-value pair processed by
  // this encoder.
  void SetHeaderListener(HeaderListener listener) { listener_ = listener; }

  void SetHeaderTableDebugVisitor(
      std::unique_ptr<HpackHeaderTable::DebugVisitorInterface> visitor) {
    header_table_.set_debug_visitor(std::move(visitor));
  }

  void DisableCompression() { enable_compression_ = false; }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  friend class test::HpackEncoderPeer;

  class RepresentationIterator;
  class Encoderator;

  // Encodes a sequence of header name-value pairs as a single header block.
  void EncodeRepresentations(RepresentationIterator* iter, std::string* output);

  // Emits a static/dynamic indexed representation (Section 7.1).
  void EmitIndex(const HpackEntry* entry);

  // Emits a literal representation (Section 7.2).
  void EmitIndexedLiteral(const Representation& representation);
  void EmitNonIndexedLiteral(const Representation& representation);
  void EmitLiteral(const Representation& representation);

  // Emits a Huffman or identity string (whichever is smaller).
  void EmitString(quiche::QuicheStringPiece str);

  // Emits the current dynamic table size if the table size was recently
  // updated and we have not yet emitted it (Section 6.3).
  void MaybeEmitTableSize();

  // Crumbles a cookie header into ";" delimited crumbs.
  static void CookieToCrumbs(const Representation& cookie,
                             Representations* crumbs_out);

  // Crumbles other header field values at \0 delimiters.
  static void DecomposeRepresentation(const Representation& header_field,
                                      Representations* out);

  HpackHeaderTable header_table_;
  HpackOutputStream output_stream_;

  const HpackHuffmanTable& huffman_table_;
  size_t min_table_size_setting_received_;
  HeaderListener listener_;
  IndexingPolicy should_index_;
  bool enable_compression_;
  bool should_emit_table_size_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HPACK_HPACK_ENCODER_H_
