// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_blocking_manager.h"

#include <limits>
#include <utility>

#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

QpackBlockingManager::QpackBlockingManager() : known_received_count_(0) {}

bool QpackBlockingManager::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  auto it = header_blocks_.find(stream_id);
  if (it == header_blocks_.end()) {
    return false;
  }

  QUICHE_DCHECK(!it->second.empty());

  const HeaderBlock& header_block = it->second.front();
  QUICHE_DCHECK(!header_block.indices.empty());

  if (known_received_count_ < header_block.required_insert_count) {
    known_received_count_ = header_block.required_insert_count;
    if (optimize_qpack_blocking_manager_) {
      OnKnownReceivedCountIncreased();
    }
  }

  DecreaseReferenceCounts(header_block.indices);

  it->second.pop_front();
  if (it->second.empty()) {
    header_blocks_.erase(it);
  }

  return true;
}

void QpackBlockingManager::OnStreamCancellation(QuicStreamId stream_id) {
  auto it = header_blocks_.find(stream_id);
  if (it == header_blocks_.end()) {
    return;
  }

  for (const HeaderBlock& header_block : it->second) {
    DecreaseReferenceCounts(header_block.indices);
  }

  header_blocks_.erase(it);
  if (optimize_qpack_blocking_manager_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_optimize_qpack_blocking_manager, 1, 5);
    blocked_streams_.erase(stream_id);
  }
}

bool QpackBlockingManager::OnInsertCountIncrement(uint64_t increment) {
  if (increment >
      std::numeric_limits<uint64_t>::max() - known_received_count_) {
    return false;
  }

  known_received_count_ += increment;
  if (optimize_qpack_blocking_manager_) {
    OnKnownReceivedCountIncreased();
  }
  return true;
}

void QpackBlockingManager::OnHeaderBlockSent(QuicStreamId stream_id,
                                             IndexSet indices,
                                             uint64_t required_insert_count) {
  QUICHE_DCHECK(!indices.empty());

  IncreaseReferenceCounts(indices);
  header_blocks_[stream_id].push_back(
      {std::move(indices), required_insert_count});
  if (optimize_qpack_blocking_manager_ &&
      required_insert_count > known_received_count_) {
    auto it = blocked_streams_.find(stream_id);
    if (it != blocked_streams_.end()) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_optimize_qpack_blocking_manager, 2, 5);
      it->second = std::max(it->second, required_insert_count);
    } else {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_optimize_qpack_blocking_manager, 3, 5);
      blocked_streams_[stream_id] = required_insert_count;
    }
  }
}

bool QpackBlockingManager::blocking_allowed_on_stream(
    QuicStreamId stream_id, uint64_t maximum_blocked_streams) const {
  if (optimize_qpack_blocking_manager_) {
    // Sending blocked reference is allowed if:
    // 1) Stream |stream_id| is already blocked, or
    // 2) The number of blocked streams is less than the limit.
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_optimize_qpack_blocking_manager, 4, 5);
    return blocked_streams_.contains(stream_id) ||
           blocked_streams_.size() < maximum_blocked_streams;
  }
  // This should be the most common case: the limit is larger than the number of
  // streams that have unacknowledged header blocks (regardless of whether they
  // are blocked or not) plus one for stream |stream_id|.
  if (header_blocks_.size() + 1 <= maximum_blocked_streams) {
    return true;
  }

  // This should be another common case: no blocked stream allowed.
  if (maximum_blocked_streams == 0) {
    return false;
  }

  uint64_t blocked_stream_count = 0;
  for (const auto& header_blocks_for_stream : header_blocks_) {
    for (const HeaderBlock& header_block : header_blocks_for_stream.second) {
      if (header_block.required_insert_count > known_received_count_) {
        if (header_blocks_for_stream.first == stream_id) {
          // Sending blocking references is allowed if stream |stream_id| is
          // already blocked.
          return true;
        }
        ++blocked_stream_count;
        // If stream |stream_id| is already blocked, then it is not counted yet,
        // therefore the number of blocked streams is at least
        // |blocked_stream_count + 1|, which cannot be more than
        // |maximum_blocked_streams| by API contract.
        // If stream |stream_id| is not blocked, then blocking will increase the
        // blocked stream count to at least |blocked_stream_count + 1|.  If that
        // is larger than |maximum_blocked_streams|, then blocking is not
        // allowed on stream |stream_id|.
        if (blocked_stream_count + 1 > maximum_blocked_streams) {
          return false;
        }
        break;
      }
    }
  }

  // Stream |stream_id| is not blocked.
  // If there are no blocked streams, then
  // |blocked_stream_count + 1 <= maximum_blocked_streams| because
  // |maximum_blocked_streams| is larger than zero.
  // If there are are blocked streams, then
  // |blocked_stream_count + 1 <= maximum_blocked_streams| otherwise the method
  // would have returned false when |blocked_stream_count| was incremented.
  // Therefore blocking on |stream_id| is allowed.
  return true;
}

uint64_t QpackBlockingManager::smallest_blocking_index() const {
  return entry_reference_counts_.empty()
             ? std::numeric_limits<uint64_t>::max()
             : entry_reference_counts_.begin()->first;
}

// static
uint64_t QpackBlockingManager::RequiredInsertCount(const IndexSet& indices) {
  return *indices.rbegin() + 1;
}

void QpackBlockingManager::IncreaseReferenceCounts(const IndexSet& indices) {
  for (const uint64_t index : indices) {
    auto it = entry_reference_counts_.lower_bound(index);
    if (it != entry_reference_counts_.end() && it->first == index) {
      ++it->second;
    } else {
      entry_reference_counts_.insert(it, {index, 1});
    }
  }
}

void QpackBlockingManager::DecreaseReferenceCounts(const IndexSet& indices) {
  for (const uint64_t index : indices) {
    auto it = entry_reference_counts_.find(index);
    QUICHE_DCHECK(it != entry_reference_counts_.end());
    QUICHE_DCHECK_NE(0u, it->second);

    if (it->second == 1) {
      entry_reference_counts_.erase(it);
    } else {
      --it->second;
    }
  }
}

void QpackBlockingManager::OnKnownReceivedCountIncreased() {
  QUICHE_DCHECK(optimize_qpack_blocking_manager_);
  for (auto blocked_it = blocked_streams_.begin();
       blocked_it != blocked_streams_.end();) {
    if (blocked_it->second > known_received_count_) {
      ++blocked_it;
      continue;
    }
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_optimize_qpack_blocking_manager, 5, 5);
    blocked_streams_.erase(blocked_it++);
  }
}

}  // namespace quic
