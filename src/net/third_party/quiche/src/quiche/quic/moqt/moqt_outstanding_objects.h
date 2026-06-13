// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_OUTSTANDING_OBJECTS_H_
#define QUICHE_QUIC_MOQT_MOQT_OUTSTANDING_OBJECTS_H_

#include <cstddef>
#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/quiche_circular_deque.h"

namespace moqt {

// MoqtOutstandingObjects tracks the outgoing objects using Object ACKs. It lets
// the sender detect if the objects arrive at the receiver out of order.  In
// MOQT, the objects get out of order when:
//  - the subgroup they were a part of is timed out or explicitly reset,
//  - the track deliver order is descending, and a new group preempted the tail
//    of the old one.
// Note that in the latter scenario, the older objects may still arrive, which
// is why this class returns the size of out-of-order gap.
//
// MoqtOutstandingObjects is also generally not suitable for track with more
// than a single subgroup per group.
class MoqtOutstandingObjects {
 public:
  static constexpr size_t kMaxObjectsTracked = 512;

  // All objects that are `max_out_of_order_objects` behind the largest object
  // acked are deemed no longer useful and removed.
  explicit MoqtOutstandingObjects(uint64_t max_out_of_order_objects)
      : max_out_of_order_objects_(max_out_of_order_objects) {}

  // Adds an object into the list.
  void OnObjectAdded(Location location);

  // Marks the object as received. Returns the zero if the object arrived
  // in-order, positive value equal to the delta if it arrives out-of-order, and
  // -1 if the state to compute the delta is not available.
  int OnObjectAcked(Location location);

  size_t tracked_objects_count() const;

 private:
  using SequenceNumber = int64_t;
  struct OutstandingObject {
    explicit OutstandingObject(Location location) : location(location) {}

    Location location;
    bool acked = false;
  };

  void PopFront();

  // If reordering for an individual object exceeds the specified threshold,
  // discard it.
  const uint64_t max_out_of_order_objects_;

  // The object at the end of `objects_` has sequence number of
  // `next_sequence_number_ - 1`.
  quiche::QuicheCircularDeque<OutstandingObject> objects_;
  absl::flat_hash_map<Location, SequenceNumber> location_map_;
  SequenceNumber next_sequence_number_ = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_OUTSTANDING_OBJECTS_H_
