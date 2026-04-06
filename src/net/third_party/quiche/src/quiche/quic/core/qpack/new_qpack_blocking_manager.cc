// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/new_qpack_blocking_manager.h"

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

NewQpackBlockingManager::IndexSet::IndexSet(
    std::initializer_list<uint64_t> indices) {
  for (const uint64_t index : indices) {
    insert(index);
  }
}

void NewQpackBlockingManager::IndexSet::insert(uint64_t index) {
  if (index > max_index_) {
    max_index_ = index;
  }
  if (index < min_index_) {
    min_index_ = index;
  }
}

uint64_t NewQpackBlockingManager::IndexSet::RequiredInsertCount() const {
  if (empty()) {
    QUIC_BUG(qpack_blocking_manager_required_insert_count_on_empty_set)
        << "RequiredInsertCount called on an empty IndexSet.";
    return 0;
  }
  return max_index_ + 1;
}

uint64_t NewQpackBlockingManager::StreamRecord::MaxRequiredInsertCount() const {
  uint64_t result = 0;
  for (const IndexSet& header_block : header_blocks) {
    uint64_t required_insert_count = header_block.RequiredInsertCount();
    if (required_insert_count > result) {
      result = required_insert_count;
    }
  }
  return result;
}

bool NewQpackBlockingManager::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    return false;
  }

  if (it->second->header_blocks.empty()) {
    QUIC_BUG(qpack_blocking_manager_no_unacked_header_blocks_in_stream)
        << "OnHeaderAcknowledgement is called on a stream with no "
           "unacked header blocks. stream_id:"
        << stream_id;
    return false;
  }

  {
    // Scoped to prevent accidental access to |acked_header_block| after
    // it is erased right after the scope.
    const IndexSet& acked_header_block = it->second->header_blocks.front();
    if (known_received_count_ < acked_header_block.RequiredInsertCount()) {
      IncreaseKnownReceivedCount(acked_header_block.RequiredInsertCount());
    }
    DecMinIndexReferenceCounts(acked_header_block.min_index());
  }
  it->second->header_blocks.erase(it->second->header_blocks.begin());

  bool ok = true;
  if (it->second->header_blocks.empty()) {
    if (blocked_streams_.is_linked(it->second.get())) {
      // header_blocks.empty() means all header blocks in the stream are acked,
      // thus the stream should not be blocked.
      QUIC_BUG(qpack_blocking_manager_stream_blocked_unexpectedly)
          << "Stream is blocked unexpectedly. stream_id:" << stream_id;
      ok = false;
      UpdateBlockedListForStream(*it->second);
    }
    stream_map_.erase(it);
  }
  return ok;
}

void NewQpackBlockingManager::IncreaseKnownReceivedCount(
    uint64_t new_known_received_count) {
  if (new_known_received_count <= known_received_count_) {
    QUIC_BUG(qpack_blocking_manager_known_received_count_not_increased)
        << "new_known_received_count:" << new_known_received_count
        << ", known_received_count_:" << known_received_count_;
    return;
  }

  known_received_count_ = new_known_received_count;

  // Go through blocked streams and remove those that are no longer blocked.
  for (auto it = blocked_streams_.begin(); it != blocked_streams_.end();) {
    if (it->MaxRequiredInsertCount() > known_received_count_) {
      // Stream is still blocked.
      ++it;
      continue;
    }

    // Stream is no longer blocked.
    it = blocked_streams_.erase(it);
    num_blocked_streams_--;
  }
}

void NewQpackBlockingManager::OnStreamCancellation(QuicStreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    return;
  }

  for (const IndexSet& header_block : it->second->header_blocks) {
    DecMinIndexReferenceCounts(header_block.min_index());
  }

  // header_blocks.clear() cause StreamRecord.MaxRequiredInsertCount() to return
  // zero, thus UpdateBlockedListForStream will remove it from blocked_streams_.
  it->second->header_blocks.clear();
  UpdateBlockedListForStream(*it->second);

  stream_map_.erase(it);
}

bool NewQpackBlockingManager::OnInsertCountIncrement(uint64_t increment) {
  if (increment >
      std::numeric_limits<uint64_t>::max() - known_received_count_) {
    return false;
  }

  IncreaseKnownReceivedCount(known_received_count_ + increment);
  return true;
}

void NewQpackBlockingManager::OnHeaderBlockSent(
    QuicStreamId stream_id, IndexSet indices, uint64_t required_insert_count) {
  if (indices.empty()) {
    QUIC_BUG(qpack_blocking_manager_empty_indices)
        << "OnHeaderBlockSent must not be called with empty indices. stream_id:"
        << stream_id;
    return;
  }

  IncMinIndexReferenceCounts(indices.min_index());

  QUICHE_DCHECK_EQ(required_insert_count, indices.RequiredInsertCount());
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    it =
        stream_map_.insert({stream_id, std::make_unique<StreamRecord>()}).first;
  }
  it->second->header_blocks.push_back(std::move(indices));

  UpdateBlockedListForStream(*it->second);
}

void NewQpackBlockingManager::UpdateBlockedListForStream(
    StreamRecord& stream_record) {
  if (stream_record.MaxRequiredInsertCount() > known_received_count_) {
    // Stream is blocked.
    if (!blocked_streams_.is_linked(&stream_record)) {
      blocked_streams_.push_back(&stream_record);
      num_blocked_streams_++;
    }
  } else {
    // Stream is not blocked.
    if (blocked_streams_.is_linked(&stream_record)) {
      blocked_streams_.erase(&stream_record);
      num_blocked_streams_--;
    }
  }
}

bool NewQpackBlockingManager::stream_is_blocked(QuicStreamId stream_id) const {
  auto it = stream_map_.find(stream_id);
  return it != stream_map_.end() &&
         blocked_streams_.is_linked(it->second.get());
}

bool NewQpackBlockingManager::blocking_allowed_on_stream(
    QuicStreamId stream_id, uint64_t maximum_blocked_streams) const {
  if (num_blocked_streams_ < maximum_blocked_streams) {
    // Whether |stream_id| is currently blocked or not, blocking on it will not
    // exceed |maximum_blocked_streams|.
    return true;
  }

  // We've reached |maximum_blocked_streams| so no _new_ blocked streams are
  // allowed. Return true iff |stream_id| is already blocked.
  return stream_is_blocked(stream_id);
}

uint64_t NewQpackBlockingManager::smallest_blocking_index() const {
  return min_index_reference_counts_.empty()
             ? std::numeric_limits<uint64_t>::max()
             : min_index_reference_counts_.begin()->first;
}

// static
uint64_t NewQpackBlockingManager::RequiredInsertCount(const IndexSet& indices) {
  return indices.RequiredInsertCount();
}

void NewQpackBlockingManager::IncMinIndexReferenceCounts(uint64_t min_index) {
  min_index_reference_counts_[min_index]++;
}

void NewQpackBlockingManager::DecMinIndexReferenceCounts(uint64_t min_index) {
  auto it = min_index_reference_counts_.find(min_index);
  if (it == min_index_reference_counts_.end()) {
    QUIC_BUG(qpack_blocking_manager_removing_non_existent_min_index)
        << "Removing min index:" << min_index
        << " which do not exist in min_index_reference_counts_.";
    return;
  }
  if (it->second == 1) {
    min_index_reference_counts_.erase(it);
  } else {
    it->second--;
  }
}

}  // namespace quic
