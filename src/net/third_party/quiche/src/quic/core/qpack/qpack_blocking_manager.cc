// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_blocking_manager.h"

#include <utility>

namespace quic {

QpackBlockingManager::QpackBlockingManager() : known_received_count_(0) {}

bool QpackBlockingManager::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  auto it = header_blocks_.find(stream_id);
  if (it == header_blocks_.end()) {
    return false;
  }

  DCHECK(!it->second.empty());

  const IndexSet& indices = it->second.front();
  DCHECK(!indices.empty());

  const uint64_t required_index_count = RequiredInsertCount(indices);
  if (known_received_count_ < required_index_count) {
    IncreaseKnownReceivedCountTo(required_index_count);
  }

  DecreaseReferenceCounts(indices);

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

  for (const IndexSet& indices : it->second) {
    DecreaseReferenceCounts(indices);
  }

  header_blocks_.erase(it);
}

void QpackBlockingManager::OnInsertCountIncrement(uint64_t increment) {
  IncreaseKnownReceivedCountTo(known_received_count_ + increment);
}

void QpackBlockingManager::OnHeaderBlockSent(QuicStreamId stream_id,
                                             IndexSet indices) {
  DCHECK(!indices.empty());

  IncreaseReferenceCounts(indices);
  header_blocks_[stream_id].push_back(std::move(indices));
}

void QpackBlockingManager::OnReferenceSentOnEncoderStream(
    uint64_t inserted_index,
    uint64_t referred_index) {
  auto result = unacked_encoder_stream_references_.insert(
      {inserted_index, referred_index});
  // Each dynamic table entry can refer to at most one |referred_index|.
  DCHECK(result.second);
  IncreaseReferenceCounts({referred_index});
}

uint64_t QpackBlockingManager::blocked_stream_count() const {
  uint64_t blocked_stream_count = 0;
  for (const auto& header_blocks_for_stream : header_blocks_) {
    for (const IndexSet& indices : header_blocks_for_stream.second) {
      if (RequiredInsertCount(indices) > known_received_count_) {
        ++blocked_stream_count;
        break;
      }
    }
  }

  return blocked_stream_count;
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

void QpackBlockingManager::IncreaseKnownReceivedCountTo(
    uint64_t new_known_received_count) {
  DCHECK_GT(new_known_received_count, known_received_count_);

  known_received_count_ = new_known_received_count;

  // Remove referred indices with key less than new Known Received Count from
  // |unacked_encoder_stream_references_| and |entry_reference_counts_|.
  IndexSet acknowledged_references;
  auto it = unacked_encoder_stream_references_.begin();
  while (it != unacked_encoder_stream_references_.end() &&
         it->first < known_received_count_) {
    acknowledged_references.insert(it->second);
    ++it;
  }
  unacked_encoder_stream_references_.erase(
      unacked_encoder_stream_references_.begin(), it);
  DecreaseReferenceCounts(acknowledged_references);
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
    DCHECK(it != entry_reference_counts_.end());
    DCHECK_NE(0u, it->second);

    if (it->second == 1) {
      entry_reference_counts_.erase(it);
    } else {
      --it->second;
    }
  }
}

}  // namespace quic
