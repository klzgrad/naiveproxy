// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_ENCODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_ENCODER_H_

#include <cstddef>

#include "net/third_party/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quic/core/qpack/qpack_instruction_encoder.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {

class QpackHeaderTable;

// An implementation of ProgressiveEncoder interface that encodes a single
// header block.
class QUIC_EXPORT_PRIVATE QpackProgressiveEncoder
    : public spdy::HpackEncoder::ProgressiveEncoder {
 public:
  QpackProgressiveEncoder() = delete;
  QpackProgressiveEncoder(QuicStreamId stream_id,
                          QpackHeaderTable* header_table,
                          QpackEncoderStreamSender* encoder_stream_sender,
                          const spdy::SpdyHeaderBlock* header_list);
  QpackProgressiveEncoder(const QpackProgressiveEncoder&) = delete;
  QpackProgressiveEncoder& operator=(const QpackProgressiveEncoder&) = delete;
  ~QpackProgressiveEncoder() override = default;

  // Returns true iff more remains to encode.
  bool HasNext() const override;

  // Encodes up to |max_encoded_bytes| octets, appending to |output|.
  void Next(size_t max_encoded_bytes, QuicString* output) override;

 private:
  const QuicStreamId stream_id_;
  QpackInstructionEncoder instruction_encoder_;
  const QpackHeaderTable* const header_table_;
  QpackEncoderStreamSender* const encoder_stream_sender_;
  const spdy::SpdyHeaderBlock* const header_list_;

  // Header field currently being encoded.
  spdy::SpdyHeaderBlock::const_iterator header_list_iterator_;

  // False until prefix is fully encoded.
  bool prefix_encoded_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_ENCODER_H_
