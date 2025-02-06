// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_

#include <cstdint>
#include <map>
#include <set>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {

class QpackBlockingManagerPeer;

}  // namespace test

// Class to keep track of blocked streams and blocking dynamic table entries:
// https://rfc-editor.org/rfc/rfc9204.html#section-2.2.1.
// https://rfc-editor.org/rfc/rfc9204.html#section-2.1.2
class QUICHE_EXPORT QpackBlockingManager {
 public:
  using IndexSet = absl::btree_multiset<uint64_t>;

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
  void OnHeaderBlockSent(QuicStreamId stream_id, IndexSet indices,
                         uint64_t required_insert_count);

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
  // https://rfc-editor.org/rfc/rfc9204.html#section-2.1.4.
  uint64_t known_received_count() const { return known_received_count_; }

  // Required Insert Count for set of indices.
  // TODO(fayang): move this method to qpack_encoder once deprecating
  // optimize_qpack_blocking_manager flag.
  static uint64_t RequiredInsertCount(const IndexSet& indices);

 private:
  friend test::QpackBlockingManagerPeer;

  // A stream typically has only one header block, except for the rare cases of
  // 1xx responses and trailers. Even if there are multiple header blocks sent
  // on a single stream, they might not be blocked at the same time. Use
  // std::list instead of quiche::QuicheCircularDeque because it has lower
  // memory footprint when holding few elements.
  struct HeaderBlock {
    const IndexSet indices;
    const uint64_t required_insert_count = 0;
  };
  using HeaderBlocks =
      absl::flat_hash_map<QuicStreamId, std::list<HeaderBlock>>;

  // Increase or decrease the reference count for each index in |indices|.
  void IncreaseReferenceCounts(const IndexSet& indices);
  void DecreaseReferenceCounts(const IndexSet& indices);

  // Called to cleanup blocked_streams_ when known_received_count is increased.
  void OnKnownReceivedCountIncreased();

  // Multiset of indices in each header block for each stream.
  // Must not contain a stream id with an empty queue.
  HeaderBlocks header_blocks_;

  // Number of references in |header_blocks_| for each entry index.
  absl::btree_map<uint64_t, uint64_t> entry_reference_counts_;

  uint64_t known_received_count_;

  // Mapping from blocked streams to their required insert count (>
  // known_received_count_).
  absl::flat_hash_map<QuicStreamId, uint64_t> blocked_streams_;

  const bool optimize_qpack_blocking_manager_ =
      GetQuicReloadableFlag(quic_optimize_qpack_blocking_manager);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_BLOCKING_MANAGER_H_
