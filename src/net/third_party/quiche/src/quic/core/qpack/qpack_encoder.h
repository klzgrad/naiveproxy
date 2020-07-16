// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_blocking_manager.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_receiver.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace spdy {

class SpdyHeaderBlock;

}  // namespace spdy

namespace quic {

namespace test {

class QpackEncoderPeer;

}  // namespace test

// QPACK encoder class.  Exactly one instance should exist per QUIC connection.
class QUIC_EXPORT_PRIVATE QpackEncoder
    : public QpackDecoderStreamReceiver::Delegate {
 public:
  // Interface for receiving notification that an error has occurred on the
  // decoder stream.  This MUST be treated as a connection error of type
  // HTTP_QPACK_DECODER_STREAM_ERROR.
  class QUIC_EXPORT_PRIVATE DecoderStreamErrorDelegate {
   public:
    virtual ~DecoderStreamErrorDelegate() {}

    virtual void OnDecoderStreamError(
        quiche::QuicheStringPiece error_message) = 0;
  };

  QpackEncoder(DecoderStreamErrorDelegate* decoder_stream_error_delegate);
  ~QpackEncoder() override;

  // Encode a header list.  If |encoder_stream_sent_byte_count| is not null,
  // |*encoder_stream_sent_byte_count| will be set to the number of bytes sent
  // on the encoder stream to insert dynamic table entries.
  std::string EncodeHeaderList(QuicStreamId stream_id,
                               const spdy::SpdyHeaderBlock& header_list,
                               QuicByteCount* encoder_stream_sent_byte_count);

  // Set maximum dynamic table capacity to |maximum_dynamic_table_capacity|,
  // measured in bytes.  Called when SETTINGS_QPACK_MAX_TABLE_CAPACITY is
  // received.  Encoder needs to know this value so that it can calculate
  // MaxEntries, used as a modulus to encode Required Insert Count.
  void SetMaximumDynamicTableCapacity(uint64_t maximum_dynamic_table_capacity);

  // Set dynamic table capacity to |dynamic_table_capacity|.
  // |dynamic_table_capacity| must not exceed maximum dynamic table capacity.
  // Also sends Set Dynamic Table Capacity instruction on encoder stream.
  void SetDynamicTableCapacity(uint64_t dynamic_table_capacity);

  // Set maximum number of blocked streams.
  // Called when SETTINGS_QPACK_BLOCKED_STREAMS is received.
  void SetMaximumBlockedStreams(uint64_t maximum_blocked_streams);

  // QpackDecoderStreamReceiver::Delegate implementation
  void OnInsertCountIncrement(uint64_t increment) override;
  void OnHeaderAcknowledgement(QuicStreamId stream_id) override;
  void OnStreamCancellation(QuicStreamId stream_id) override;
  void OnErrorDetected(quiche::QuicheStringPiece error_message) override;

  // delegate must be set if dynamic table capacity is not zero.
  void set_qpack_stream_sender_delegate(QpackStreamSenderDelegate* delegate) {
    encoder_stream_sender_.set_qpack_stream_sender_delegate(delegate);
  }

  QpackStreamReceiver* decoder_stream_receiver() {
    return &decoder_stream_receiver_;
  }

  // True if any dynamic table entries have been referenced from a header block.
  bool dynamic_table_entry_referenced() const {
    return header_table_.dynamic_table_entry_referenced();
  }

 private:
  friend class test::QpackEncoderPeer;

  using Instructions = std::vector<QpackInstructionWithValues>;

  // Generate indexed header field instruction
  // and optionally update |*referred_indices|.
  static QpackInstructionWithValues EncodeIndexedHeaderField(
      bool is_static,
      uint64_t index,
      QpackBlockingManager::IndexSet* referred_indices);

  // Generate literal header field with name reference instruction
  // and optionally update |*referred_indices|.
  static QpackInstructionWithValues EncodeLiteralHeaderFieldWithNameReference(
      bool is_static,
      uint64_t index,
      quiche::QuicheStringPiece value,
      QpackBlockingManager::IndexSet* referred_indices);

  // Generate literal header field instruction.
  static QpackInstructionWithValues EncodeLiteralHeaderField(
      quiche::QuicheStringPiece name,
      quiche::QuicheStringPiece value);

  // Performs first pass of two-pass encoding: represent each header field in
  // |*header_list| as a reference to an existing entry, the name of an existing
  // entry with a literal value, or a literal name and value pair.  Sends
  // necessary instructions on the encoder stream coalesced in a single write.
  // Records absolute indices of referred dynamic table entries in
  // |*referred_indices|.  If |encoder_stream_sent_byte_count| is not null, then
  // sets |*encoder_stream_sent_byte_count| to the number of bytes sent on the
  // encoder stream to insert dynamic table entries.  Returns list of header
  // field representations, with all dynamic table entries referred to with
  // absolute indices.  Returned Instructions object may have
  // quiche::QuicheStringPieces pointing to strings owned by |*header_list|.
  Instructions FirstPassEncode(QuicStreamId stream_id,
                               const spdy::SpdyHeaderBlock& header_list,
                               QpackBlockingManager::IndexSet* referred_indices,
                               QuicByteCount* encoder_stream_sent_byte_count);

  // Performs second pass of two-pass encoding: serializes representations
  // generated in first pass, transforming absolute indices of dynamic table
  // entries to relative indices.
  std::string SecondPassEncode(Instructions instructions,
                               uint64_t required_insert_count) const;

  DecoderStreamErrorDelegate* const decoder_stream_error_delegate_;
  QpackDecoderStreamReceiver decoder_stream_receiver_;
  QpackEncoderStreamSender encoder_stream_sender_;
  QpackHeaderTable header_table_;
  uint64_t maximum_blocked_streams_;
  QpackBlockingManager blocking_manager_;
  int header_list_count_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_
