// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

using DeleteTrackCallback = quiche::SingleUseCallback<void()>;

// MoqtRelayTrackPublisher lets the user send objects by providing the contents
// of the object and the object metadata. It will store these by location
// number. When called on to provide a range of objects, it will fill in any
// missing objects and groups.
//
// The queue will maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by live relays to buffer the
// frames that arrive for a short time.
class MoqtRelayTrackPublisher : public MoqtTrackPublisher,
                                public SubscribeVisitor,
                                public MoqtPublishingMonitorInterface {
 public:
  MoqtRelayTrackPublisher(
      FullTrackName track, quiche::QuicheWeakPtr<MoqtSessionInterface> upstream,
      DeleteTrackCallback delete_track_callback,
      std::optional<quic::QuicTime> expiration = quic::QuicTime::Infinite(),
      const quic::QuicClock* clock = quic::QuicDefaultClock::Get(),
      std::optional<quic::QuicTimeDelta> oack_window_size = std::nullopt)
      : clock_(clock),
        track_(std::move(track)),
        upstream_(std::move(upstream)),
        delete_track_callback_(std::move(delete_track_callback)),
        expiration_(expiration),
        next_location_(0, 0),
        oack_window_size_(oack_window_size) {}

  MoqtRelayTrackPublisher(const MoqtRelayTrackPublisher&) = delete;
  MoqtRelayTrackPublisher(MoqtRelayTrackPublisher&&) = default;
  MoqtRelayTrackPublisher& operator=(const MoqtRelayTrackPublisher&) = delete;
  MoqtRelayTrackPublisher& operator=(MoqtRelayTrackPublisher&&) = default;

  // SubscribeVisitor implementation.
  void OnReply(
      const FullTrackName& full_track_name,
      std::variant<SubscribeOkData, MoqtRequestErrorInfo> response) override;
  void OnCanAckObjects(MoqtObjectAckFunction ack_function) override;
  void OnObjectFragment(const FullTrackName& full_track_name,
                        const PublishedObjectMetadata& metadata,
                        absl::string_view object, uint64_t offset) override;
  void OnPublishDone(FullTrackName full_track_name) override;
  void OnMalformedTrack(const FullTrackName& /*full_track_name*/) override {
    DeleteTrack();
  }
  void OnStreamFin(const FullTrackName&, DataStreamIndex stream) override;
  void OnStreamReset(const FullTrackName&, DataStreamIndex stream) override;

  // MoqtTrackPublisher implementation.
  const FullTrackName& GetTrackName() const override { return track_; }
  MoqtPublishingMonitorInterface* absl_nonnull GetMonitoringInterface()
      override {
    return this;
  }
  std::optional<PublishedObject> GetCachedObject(
      uint64_t group_id, std::optional<uint64_t> subgroup_id,
      uint64_t min_object, uint64_t offset = 0) const override;
  void AddObjectListener(MoqtObjectListener* listener) override;
  void RemoveObjectListener(MoqtObjectListener* listener) override;
  std::optional<Location> largest_location() const override;
  const TrackExtensions& extensions() const override { return extensions_; }
  std::optional<quic::QuicTimeDelta> expiration() const override;
  std::optional<quic::QuicTimeDelta> oack_window_size() const {
    return oack_window_size_;
  }
  void set_oack_window_size(
      std::optional<quic::QuicTimeDelta> oack_window_size) {
    oack_window_size_ = oack_window_size;
  }
  std::unique_ptr<MoqtFetchTask> StandaloneFetch(Location /*start*/,
                                                 Location /*end*/,
                                                 MoqtDeliveryOrder) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }
  std::unique_ptr<MoqtFetchTask> RelativeFetch(uint64_t /*group_diff*/,
                                               MoqtDeliveryOrder) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }
  std::unique_ptr<MoqtFetchTask> AbsoluteFetch(uint64_t /*group*/,
                                               MoqtDeliveryOrder) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }

  // MoqtPublishingMonitorInterface implementation.
  //
  // Note: Forwarding object ACKs across a relay does not work well for
  // situations when there is more than one subscriber (since the publisher
  // cannot tell them apart); this is a temporary solution.
  void OnObjectAckSupportKnown(
      std::optional<quic::QuicTimeDelta> /*time_window*/) override {}
  void OnNewObjectEnqueued(Location /*location*/) override {}
  void OnObjectAckReceived(Location location,
                           quic::QuicTimeDelta delta_from_deadline) override;

  void ForAllObjects(
      quiche::UnretainedCallback<void(const CachedObject&)> callback);

  void Close() { is_closing_ = true; }

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  void DeleteTrack();

  // Ordered by object id.
  using Subgroup = std::map<uint64_t, CachedObject>;

  struct Group {
    uint64_t next_object = 0;
    bool complete = false;  // If true, kEndOfGroup has been received.
    absl::btree_map<uint64_t, Subgroup> subgroups;  // Ordered by subgroup id.
    std::map<uint64_t, CachedObject> datagrams;
  };

  bool is_closing_ = false;
  bool got_response_ = false;
  const quic::QuicClock* clock_;
  FullTrackName track_;
  quiche::QuicheWeakPtr<MoqtSessionInterface> upstream_;
  DeleteTrackCallback delete_track_callback_;
  TrackExtensions extensions_;
  // TODO(martinduke): This publisher should destroy itself when the expiration
  // time passes.
  std::optional<quic::QuicTime> expiration_;
  absl::btree_map<uint64_t, Group> queue_;  // Ordered by group id.
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
  std::optional<Location> end_of_track_;
  Location next_location_;
  // Fixed value of `oack_window_size` used for the upstream subscription.
  std::optional<quic::QuicTimeDelta> oack_window_size_;
  // Callback to send object ACKs to the upstream publisher if available.
  MoqtObjectAckFunction absl_nullable object_ack_function_ = nullptr;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_
