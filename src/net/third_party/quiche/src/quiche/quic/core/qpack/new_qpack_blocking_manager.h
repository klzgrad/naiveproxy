// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_NEW_QPACK_BLOCKING_MANAGER_H_
#define QUICHE_QUIC_CORE_QPACK_NEW_QPACK_BLOCKING_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_intrusive_list.h"

namespace quic {

// Class to keep track of blocked streams and blocking dynamic table entries:
// https://rfc-editor.org/rfc/rfc9204.html#section-2.2.1.
// https://rfc-editor.org/rfc/rfc9204.html#section-2.1.2
class QUICHE_EXPORT NewQpackBlockingManager {
 public:
  // "IndexSet" is a misnomer. It actually keeps track of the min and max of a
  // set of indices.
  class IndexSet {
   public:
    IndexSet() = default;
    IndexSet(std::initializer_list<uint64_t> indices);  // Test only.

    void insert(uint64_t index);

    bool empty() const { return min_index_ > max_index_; }

    uint64_t min_index() const { return min_index_; }
    uint64_t max_index() const { return max_index_; }

    uint64_t RequiredInsertCount() const;

   private:
    // The minimum and maximum index of the set.
    uint64_t min_index_ = std::numeric_limits<uint64_t>::max();
    uint64_t max_index_ = 0;
  };

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

  // Whether |stream_id| is currently blocked.
  bool stream_is_blocked(QuicStreamId stream_id) const;

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
  static uint64_t RequiredInsertCount(const IndexSet& indices);

 private:
  // Internal representation of a stream.
  struct StreamRecord : quiche::QuicheIntrusiveLink<StreamRecord> {
    // Returns the maximum "Required Insert Count" over all |header_blocks|.
    uint64_t MaxRequiredInsertCount() const;

    absl::InlinedVector<IndexSet, 2> header_blocks;
  };

  // Updates the membership of |stream_record| in |blocked_streams_|.
  void UpdateBlockedListForStream(StreamRecord& stream_record);

  // Increases |known_received_count_| to |new_known_received_count|, then
  // removes streams from |blocked_streams_| that are no longer blocked.
  void IncreaseKnownReceivedCount(uint64_t new_known_received_count);

  // Increase or decrease the reference counts in |min_index_reference_counts_|.
  void IncMinIndexReferenceCounts(uint64_t min_index);
  void DecMinIndexReferenceCounts(uint64_t min_index);

  // Map from stream ID to its StreamRecord, for all streams with unacked header
  // blocks. The subset of "blocked streams" are in |blocked_streams_|.
  absl::flat_hash_map<QuicStreamId, std::unique_ptr<StreamRecord>> stream_map_;

  // List of blocked streams.
  quiche::QuicheIntrusiveList<StreamRecord> blocked_streams_;
  size_t num_blocked_streams_ = 0;

  // Map from "min index" to the number of HeaderBlock(s) having that min index.
  // This is needed to provide smallest_blocking_index().
  absl::btree_map<uint64_t, uint64_t> min_index_reference_counts_;

  uint64_t known_received_count_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_NEW_QPACK_BLOCKING_MANAGER_H_
