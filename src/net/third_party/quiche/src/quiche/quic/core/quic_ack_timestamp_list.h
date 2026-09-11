// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_ACK_TIMESTAMP_LIST_H_
#define QUICHE_QUIC_CORE_QUIC_ACK_TIMESTAMP_LIST_H_

#include <cstdint>
#include <string>

#include "absl/container/fixed_array.h"
#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/frames/quic_ack_frame.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// QuicAckTimestampList stores the block of ACK timestamps
// (draft-ietf-quic-receive-ts-02) in a form suitable for serialization.
class QUICHE_EXPORT QuicAckTimestampList {
 public:
  // Constructs the ACK timestamp list from the provided `ack` frame.  If an
  // error is encountered, `error()` will be set to a non-empty string.
  explicit QuicAckTimestampList(const QuicAckFrame& ack, uint32_t max_ack_count,
                                uint32_t exponent,
                                QuicTime receive_timestamp_basis);

  // Avoid moving QuicAckTimestampList.  It should always be allocated on the
  // stack, since all of the data here is transient.
  QuicAckTimestampList(const QuicAckTimestampList&) = delete;
  QuicAckTimestampList(QuicAckTimestampList&&) = delete;
  QuicAckTimestampList& operator=(const QuicAckTimestampList&) = delete;
  QuicAckTimestampList& operator=(QuicAckTimestampList&&) = delete;

  // Returns the encoded size of the ACK timestamp list in bytes, assuming no
  // truncation occurs.
  QuicByteCount MaxEncodedSize() const;
  // Serializes the timestamp list into the `writer`.  Returns false if the
  // writer runs out of space while serializing.
  [[nodiscard]] bool Write(QuicDataWriter& writer) const;

  const std::string& error() const { return error_; }

 private:
  // `Timestamp Range` as defined in draft-ietf-quic-receive-ts-02.
  struct QUICHE_EXPORT TimestampRange {
    QuicPacketCount delta_from_largest_acked;
    // Offset into `timestamps_` list.
    uint32_t first_timestamp;
    uint32_t timestamp_count;
  };

  // `FixedIntEncoding` represents a final encoding of the ACK block, except
  // instead of varint62, uint64_t is used.
  using FixedIntEncoding = absl::InlinedVector<uint64_t, 32>;

  // Serializes the timestamp list into memory as an array of 64-bit integers
  // rather than 62-bit varints.
  [[nodiscard]] bool Encode(QuicByteCount max_size,
                            FixedIntEncoding& encoded) const;

  // Timestamp ranges, in the order they would be encoded on the wire.
  absl::InlinedVector<TimestampRange, 2> ranges_;
  // Timestamps, in the format it would be encoded on the wire.
  absl::FixedArray<uint64_t> timestamps_;
  // Parse error message; empty if there is no error.
  std::string error_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ACK_TIMESTAMP_LIST_H_
