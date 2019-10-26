// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_

#include <cstdint>
#include <map>
#include <set>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Class to keep track of blocked streams and blocking dynamic table entries:
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#blocked-decoding
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#blocked-insertion
class QUIC_EXPORT_PRIVATE QpackBlockingManager {
 public:
  using IndexSet = std::multiset<uint64_t>;

  QpackBlockingManager();

  // Called when a Header Acknowledgement instruction is received on the decoder
  // stream.  Returns false if there are no outstanding header blocks to be
  // acknowledged on |stream_id|.
  bool OnHeaderAcknowledgement(QuicStreamId stream_id);

  // Called when a Stream Cancellation instruction is received on the decoder
  // stream.
  void OnStreamCancellation(QuicStreamId stream_id);

  // Called when an Insert Count Increment instruction is received on the
  // decoder stream.
  void OnInsertCountIncrement(uint64_t increment);

  // Called when sending a header block containing references to dynamic table
  // entries with |indices|.  |indices| must not be empty.
  void OnHeaderBlockSent(QuicStreamId stream_id, IndexSet indices);

  // Called when sending Insert With Name Reference or Duplicate instruction on
  // encoder stream, inserting entry |inserted_index| referring to
  // |referred_index|.
  void OnReferenceSentOnEncoderStream(uint64_t inserted_index,
                                      uint64_t referred_index);

  // Returns the number of blocked streams.
  uint64_t blocked_stream_count() const;

  // Returns the index of the blocking entry with the smallest index,
  // or std::numeric_limits<uint64_t>::max() if there are no blocking entries.
  uint64_t smallest_blocking_index() const;

  // Returns the Known Received Count as defined at
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#known-received-count.
  uint64_t known_received_count() const { return known_received_count_; }

  // Required Insert Count for set of indices.
  static uint64_t RequiredInsertCount(const IndexSet& indices);

 private:
  // A stream typically has only one header block, except for the rare cases of
  // 1xx responses, trailers, or push promises.  Even if there are multiple
  // header blocks sent on a single stream, they might not be blocked at the
  // same time.  Use std::list instead of QuicDeque because it has lower memory
  // footprint when holding few elements.
  using HeaderBlocksForStream = std::list<IndexSet>;
  using HeaderBlocks = QuicUnorderedMap<QuicStreamId, HeaderBlocksForStream>;

  // Increases |known_received_count_| to |new_known_received_count|, which must
  // me larger than |known_received_count_|.  Removes acknowledged references
  // from |unacked_encoder_stream_references_|.
  void IncreaseKnownReceivedCountTo(uint64_t new_known_received_count);

  // Increase or decrease the reference count for each index in |indices|.
  void IncreaseReferenceCounts(const IndexSet& indices);
  void DecreaseReferenceCounts(const IndexSet& indices);

  // Multiset of indices in each header block for each stream.
  // Must not contain a stream id with an empty queue.
  HeaderBlocks header_blocks_;

  // Unacknowledged references on the encoder stream.
  // The key is the absolute index of the inserted entry,
  // the mapped value is the absolute index of the entry referred.
  std::map<uint64_t, uint64_t> unacked_encoder_stream_references_;

  // Number of references in |header_blocks_| and
  // |unacked_encoder_stream_references_| for each entry index.
  std::map<uint64_t, uint64_t> entry_reference_counts_;

  uint64_t known_received_count_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_
