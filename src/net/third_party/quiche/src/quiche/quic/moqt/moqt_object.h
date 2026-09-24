// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_OBJECT_H_
#define QUICHE_QUIC_MOQT_MOQT_OBJECT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_cord_utils.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

struct PublishedObjectMetadata {
  Location location;
  std::optional<uint64_t> subgroup;  // nullopt for datagrams.
  std::string extensions;
  MoqtObjectStatus status = MoqtObjectStatus::kNormal;
  MoqtPriority publisher_priority = kDefaultPublisherPriority;
  // `first_object_in_subgroup` is only available in objects communicated via
  // a subscription.  It is not available for the fetched objects; however, the
  // subscriber can guarantee that all subgroups have a start object by setting
  // the FETCH range appropriately.
  std::optional<bool> first_object_in_subgroup;
  // The length of the entire payload, which might include data that is not
  // present in an encompassing PublishedObject or CachedObject.
  uint64_t payload_length;
  quic::QuicTime arrival_time = quic::QuicTime::Zero();
  bool IsMalformed(const PublishedObjectMetadata& other) const {
    // It's OK for arrival_time to be different when checking immutables.
    return (location != other.location || subgroup != other.subgroup ||
            status != other.status ||
            publisher_priority != other.publisher_priority);
  }
  bool operator==(const PublishedObjectMetadata& other) const = default;
};

// PublishedObject is a description of an object that is sufficient to publish
// it on a given track.
struct PublishedObject {
  PublishedObjectMetadata metadata;
  // This could be a partial object, containing the data between the requested
  // offset and the end of the data the publisher has on hand.
  std::vector<quiche::QuicheMemSlice> payload;
  bool fin_after_this = false;
};

// CachedObject is a version of PublishedObject with a reference counted
// payload. This is thread-safe.
// TODO(martinduke): Allow for the deletion of the front of the payload. The
// number of bytes deleted will have to be subtracted from any offset that the
// caller provides (as well as added in payload_received).
class CachedObject {
 public:
  CachedObject(const PublishedObjectMetadata& metadata,
               quiche::QuicheMemSlice payload, bool fin_after_this)
      : metadata_(metadata),
        payload_(quiche::MemSliceToCord(std::move(payload))),
        fin_after_this_(fin_after_this) {}

  // Add |payload| at |offset|. Checks for overlaps in data. Returns false if
  // the payload is too large, or there is no new data.
  bool Append(uint64_t offset, absl::string_view payload);
  // Returns a PublishedObject with only the portion of payload starting at
  // |offset|.
  PublishedObject ToPublishedObject(uint64_t offset = 0) const;
  const PublishedObjectMetadata& metadata() const { return metadata_; }
  bool fin_after_this() const ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    return fin_after_this_;
  }
  void set_fin_after_this(bool fin) {
    absl::MutexLock lock(mutex_);
    fin_after_this_ = fin;
  }
  // This function wraps payload_.size(), both for Mutex purposes, and because
  // eventually it will account for memory blocks that have been freed from the
  // front of the payload.
  uint64_t payload_received() const {
    absl::MutexLock lock(mutex_);
    return payload_received_locked();
  }
  // Returns true if data in payload_ starting at |offset| is equal to
  // |payload|, checking only until the offset where one of the two strings
  // ends. Returns true if there is no overlap in the offsets.
  bool OverlapIsEqual(uint64_t offset, absl::string_view payload) const;

 private:
  // TODO(martinduke): Account for memory blocks that have been freed from the
  // front of the payload.
  uint64_t payload_received_locked() const { return payload_.size(); }

  mutable absl::Mutex mutex_;
  const PublishedObjectMetadata metadata_;
  absl::Cord payload_;
  // If true, this is the last object before FIN.
  bool ABSL_GUARDED_BY(mutex_) fin_after_this_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_OBJECT_H_
