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

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/spdy/core/hpack/hpack_header_table.h"
#include "quiche/spdy/core/hpack/hpack_output_stream.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

// An HpackEncoder encodes header sets as outlined in
// http://tools.ietf.org/html/rfc7541.

namespace spdy {

namespace test {
class HpackEncoderPeer;
}  // namespace test

class QUICHE_EXPORT HpackEncoder {
 public:
  using Representation = std::pair<absl::string_view, absl::string_view>;
  using Representations = std::vector<Representation>;

  // Callers may provide a HeaderListener to be informed of header name-value
  // pairs processed by this encoder.
  using HeaderListener =
      quiche::MultiUseCallback<void(absl::string_view, absl::string_view)>;

  // An indexing policy should return true if the provided header name-value
  // pair should be inserted into the HPACK dynamic table.
  using IndexingPolicy =
      quiche::MultiUseCallback<bool(absl::string_view, absl::string_view)>;

  HpackEncoder();
  HpackEncoder(const HpackEncoder&) = delete;
  HpackEncoder& operator=(const HpackEncoder&) = delete;
  ~HpackEncoder();

  // Encodes and returns the given header set as a string.
  std::string EncodeHeaderBlock(const Http2HeaderBlock& header_set);

  class QUICHE_EXPORT ProgressiveEncoder {
   public:
    virtual ~ProgressiveEncoder() {}

    // Returns true iff more remains to encode.
    virtual bool HasNext() const = 0;

    // Encodes and returns up to max_encoded_bytes of the current header block.
    virtual std::string Next(size_t max_encoded_bytes) = 0;
  };

  // Returns a ProgressiveEncoder which must be outlived by both the given
  // Http2HeaderBlock and this object.
  std::unique_ptr<ProgressiveEncoder> EncodeHeaderSet(
      const Http2HeaderBlock& header_set);
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

  // TODO(birenroy): Rename this GetDynamicTableCapacity().
  size_t CurrentHeaderTableSizeSetting() const {
    return header_table_.settings_size_bound();
  }

  // This HpackEncoder will use |policy| to determine whether to insert header
  // name-value pairs into the dynamic table.
  void SetIndexingPolicy(IndexingPolicy policy) {
    should_index_ = std::move(policy);
  }

  // |listener| will be invoked for each header name-value pair processed by
  // this encoder.
  void SetHeaderListener(HeaderListener listener) {
    listener_ = std::move(listener);
  }

  void DisableCompression() { enable_compression_ = false; }

  // Returns the current dynamic table size, including the 32 bytes per entry
  // overhead mentioned in RFC 7541 section 4.1.
  size_t GetDynamicTableSize() const { return header_table_.size(); }

 private:
  friend class test::HpackEncoderPeer;

  class RepresentationIterator;
  class Encoderator;

  // Encodes a sequence of header name-value pairs as a single header block.
  std::string EncodeRepresentations(RepresentationIterator* iter);

  // Emits a static/dynamic indexed representation (Section 7.1).
  void EmitIndex(size_t index);

  // Emits a literal representation (Section 7.2).
  void EmitIndexedLiteral(const Representation& representation);
  void EmitNonIndexedLiteral(const Representation& representation,
                             bool enable_compression);
  void EmitLiteral(const Representation& representation);

  // Emits a Huffman or identity string (whichever is smaller).
  void EmitString(absl::string_view str);

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

  size_t min_table_size_setting_received_;
  HeaderListener listener_;
  IndexingPolicy should_index_;
  bool enable_compression_;
  bool should_emit_table_size_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HPACK_HPACK_ENCODER_H_
