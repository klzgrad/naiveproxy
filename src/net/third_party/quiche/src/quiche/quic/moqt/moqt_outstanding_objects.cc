// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_outstanding_objects.h"

#include <cstddef>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt {

void MoqtOutstandingObjects::OnObjectAdded(Location location) {
  objects_.push_back(OutstandingObject(location));
  location_map_.emplace(location, next_sequence_number_++);
  while (objects_.size() > kMaxObjectsTracked) {
    PopFront();
  }
}

int MoqtOutstandingObjects::OnObjectAcked(Location location) {
  auto it = location_map_.find(location);
  if (it == location_map_.end()) {
    // The relevant state has already been discarded.
    return -1;
  }
  const SequenceNumber sequence_number = it->second;
  const SequenceNumber offset = static_cast<SequenceNumber>(objects_.size()) +
                                sequence_number - next_sequence_number_;
  if (offset < 0 || offset > objects_.size()) {
    QUICHE_BUG(MoqtOutstandingObjects_BadOffset)
        << "Invalid offset computed. Object list size: " << objects_.size()
        << ", next sequence number: " << next_sequence_number_
        << ", sequence number: " << sequence_number;
    return -1;
  }
  objects_[offset].acked = true;

  if (offset > max_out_of_order_objects_) {
    for (SequenceNumber i = 0; i < offset - max_out_of_order_objects_; ++i) {
      PopFront();
    }
  }
  while (!objects_.empty() && objects_.front().acked) {
    PopFront();
  }
  return offset;
}

void MoqtOutstandingObjects::PopFront() {
  QUICHE_DCHECK(!objects_.empty());
  location_map_.erase(objects_.front().location);
  objects_.pop_front();
}

size_t MoqtOutstandingObjects::tracked_objects_count() const {
  QUICHE_DCHECK_EQ(objects_.size(), location_map_.size());
  return objects_.size();
}

}  // namespace moqt
