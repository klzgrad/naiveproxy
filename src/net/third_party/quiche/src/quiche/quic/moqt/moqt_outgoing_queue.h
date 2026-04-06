// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

// MoqtOutgoingQueue lets the user send objects by providing the contents of the
// object and a keyframe flag.  The queue will automatically number objects and
// groups, and maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by original publishers to buffer the
// frames that they produce.
class MoqtOutgoingQueue : public MoqtTrackPublisher {
 public:
  MoqtOutgoingQueue(FullTrackName track, const quic::QuicClock* clock =
                                             quic::QuicDefaultClock::Get())
      : clock_(clock), track_(std::move(track)) {}

  MoqtOutgoingQueue(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue(MoqtOutgoingQueue&&) = default;
  MoqtOutgoingQueue& operator=(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue& operator=(MoqtOutgoingQueue&&) = default;

  // If `key` is true, the object is placed into a new group, and the previous
  // group is closed. The first object ever sent MUST have `key` set to true.
  void AddObject(quiche::QuicheMemSlice payload, bool key);

  // MoqtTrackPublisher implementation.
  const FullTrackName& GetTrackName() const override { return track_; }
  std::optional<PublishedObject> GetCachedObject(
      uint64_t group, std::optional<uint64_t> subgroup,
      uint64_t min_object) const override;
  void AddObjectListener(MoqtObjectListener* listener) override {
    listeners_.insert(listener);
    listener->OnSubscribeAccepted();
  }
  void RemoveObjectListener(MoqtObjectListener* listener) override {
    listeners_.erase(listener);
  }

  std::optional<Location> largest_location() const override;
  std::optional<quic::QuicTimeDelta> expiration() const override {
    return quic::QuicTimeDelta::Zero();
  }
  const TrackExtensions& extensions() const override { return extensions_; }

  std::unique_ptr<MoqtFetchTask> StandaloneFetch(
      Location start, Location end, MoqtDeliveryOrder order) override;
  std::unique_ptr<MoqtFetchTask> RelativeFetch(
      uint64_t group_diff, MoqtDeliveryOrder order) override;
  std::unique_ptr<MoqtFetchTask> AbsoluteFetch(
      uint64_t group, MoqtDeliveryOrder order) override;

  bool HasSubscribers() const { return !listeners_.empty(); }

  // Since MoqtTrackPublisher is generally held in a shared_ptr, an explicit
  // call allows all the listeners to delete their reference and actually
  // destroy the object.
  void RemoveAllSubscriptions() {
    while (!listeners_.empty()) {
      (*listeners_.begin())->OnTrackPublisherGone();
    }
  }

  // Sends an "End of Track" object.
  void Close();

  std::vector<Location> GetCachedObjectsInRange(Location start,
                                                Location end) const;

 protected:
  MoqtPriority default_publisher_priority() const {
    return extensions_.default_publisher_priority();
  }

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  // Fetch task for a fetch from the cache.
  class FetchTask : public MoqtFetchTask {
   public:
    FetchTask(MoqtOutgoingQueue* queue, std::vector<Location> objects)
        : queue_(queue), objects_(objects.begin(), objects.end()) {}

    GetNextObjectResult GetNextObject(PublishedObject&) override;
    absl::Status GetStatus() override { return status_; }

    void SetObjectAvailableCallback(
        ObjectsAvailableCallback callback) override {
      // Not needed since all objects in a fetch against an in-memory queue are
      // guaranteed to resolve immediately.
      callback();
    }
    void SetFetchResponseCallback(FetchResponseCallback callback) override {
      if (!status_.ok()) {
        MoqtRequestError error(0, StatusToRequestErrorCode(status_),
                               std::nullopt, std::string(status_.message()));
        std::move(callback)(error);
        return;
      }
      if (objects_.empty()) {
        MoqtRequestError error(0, StatusToRequestErrorCode(status_),
                               std::nullopt, "No objects in range");
        std::move(callback)(error);
        return;
      }
      MoqtFetchOk ok;
      ok.end_location = *(objects_.crbegin());
      if (objects_.size() > 1 && *(objects_.cbegin()) > ok.end_location) {
        ok.extensions = TrackExtensions(
            std::nullopt, std::nullopt, std::nullopt,
            MoqtDeliveryOrder::kDescending, std::nullopt, std::nullopt);
        ok.end_location = *(objects_.cbegin());
      }
      ok.end_of_track =
          queue_->closed_ && ok.end_location == queue_->largest_location();
      std::move(callback)(ok);
    }

   private:
    GetNextObjectResult GetNextObjectInner(PublishedObject&);

    MoqtOutgoingQueue* queue_;
    quiche::QuicheCircularDeque<Location> objects_;
    absl::Status status_ = absl::OkStatus();
  };

  using Group = std::vector<CachedObject>;

  // Appends an object to the end of the current group.
  void AddRawObject(MoqtObjectStatus status, quiche::QuicheMemSlice payload);
  // Closes the current group, if there is any, and opens a new one.
  void OpenNewGroup();

  // The number of the oldest group available.
  uint64_t first_group_in_queue() const {
    return current_group_id_ - queue_.size() + 1;
  }

  const quic::QuicClock* clock_;
  FullTrackName track_;
  TrackExtensions extensions_;
  bool closed_ = false;
  absl::InlinedVector<Group, kMaxQueuedGroups> queue_;
  uint64_t current_group_id_ = -1;
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
