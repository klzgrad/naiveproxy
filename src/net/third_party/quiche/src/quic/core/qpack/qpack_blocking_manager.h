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

namespace test {

class QpackBlockingManagerPeer;

}  // namespace test

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
  // decoder stream.  Returns true if Known Received Count is successfully
  // updated.  Returns false on overflow.
  bool OnInsertCountIncrement(uint64_t increment);

  // Called when sending a header block containing references to dynamic table
  // entries with |indices|.  |indices| must not be empty.
  void OnHeaderBlockSent(QuicStreamId stream_id, IndexSet indices);

  // Returns true if sending blocking references on stream |stream_id| would not
  // increase the total number of blocked streams above
  // |maximum_blocked_streams|.  Note that if |stream_id| is already blocked
  // then it is always allowed to send more blocking references on it.
  // Behavior is undefined if |maximum_blocked_streams| is smaller than number
  // of currently blocked streams.
  bool blocking_allowed_on_stream(QuicStreamId stream_id,
                                  uint64_t maximum_blocked_streams) const;

  // Returns the index of the blocking entry with the smallest index,
  // or std::numeric_limits<uint64_t>::max() if there are no blocking entries.
  uint64_t smallest_blocking_index() const;

  // Returns the Known Received Count as defined at
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#known-received-count.
  uint64_t known_received_count() const { return known_received_count_; }

  // Required Insert Count for set of indices.
  static uint64_t RequiredInsertCount(const IndexSet& indices);

 private:
  friend test::QpackBlockingManagerPeer;

  // A stream typically has only one header block, except for the rare cases of
  // 1xx responses, trailers, or push promises.  Even if there are multiple
  // header blocks sent on a single stream, they might not be blocked at the
  // same time.  Use std::list instead of QuicCircularDeque because it has lower
  // memory footprint when holding few elements.
  using HeaderBlocksForStream = std::list<IndexSet>;
  using HeaderBlocks = QuicUnorderedMap<QuicStreamId, HeaderBlocksForStream>;

  // Increase or decrease the reference count for each index in |indices|.
  void IncreaseReferenceCounts(const IndexSet& indices);
  void DecreaseReferenceCounts(const IndexSet& indices);

  // Multiset of indices in each header block for each stream.
  // Must not contain a stream id with an empty queue.
  HeaderBlocks header_blocks_;

  // Number of references in |header_blocks_| for each entry index.
  std::map<uint64_t, uint64_t> entry_reference_counts_;

  uint64_t known_received_count_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_
