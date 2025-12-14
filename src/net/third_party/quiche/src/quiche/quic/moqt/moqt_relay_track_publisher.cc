// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_relay_track_publisher.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

void MoqtRelayTrackPublisher::OnReply(
    const FullTrackName&,
    std::variant<SubscribeOkData, MoqtRequestError> response) {
  if (std::holds_alternative<MoqtRequestError>(response)) {
    auto request_error = std::get<MoqtRequestError>(response);
    for (MoqtObjectListener* listener : listeners_) {
      listener->OnSubscribeRejected(request_error);
    }
    DeleteTrack();
    return;
  }
  SubscribeOkData ok_data = std::get<SubscribeOkData>(response);
  if (ok_data.expires.IsInfinite()) {
    expiration_ = quic::QuicTime::Infinite();
  } else {
    expiration_ = clock_->Now() + ok_data.expires;
  }
  delivery_order_ = ok_data.delivery_order;
  next_location_ = ok_data.largest_location.has_value()
                       ? ok_data.largest_location->Next()
                       : Location(0, 0);
  // TODO(martinduke): Handle parameters.
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnSubscribeAccepted();
  }
}

void MoqtRelayTrackPublisher::OnObjectFragment(
    const FullTrackName& full_track_name,
    const PublishedObjectMetadata& metadata, absl::string_view object,
    bool end_of_message) {
  if (!end_of_message) {
    QUICHE_BUG(moqt_relay_track_publisher_got_fragment)
        << "Received a fragment of an object.";
    return;
  }
  bool last_object_in_stream = false;
  if (full_track_name != track_) {
    QUICHE_BUG(moqt_got_wrong_track) << "Received object for wrong track.";
    return;
  }
  if (queue_.size() == kMaxQueuedGroups) {
    if (queue_.begin()->first > metadata.location.group) {
      QUICHE_DLOG(INFO) << "Skipping object from group "
                        << metadata.location.group << " because it is too old.";
      return;
    }
    if (queue_.find(metadata.location.group) == queue_.end()) {
      // Erase the oldest group.
      for (MoqtObjectListener* listener : listeners_) {
        listener->OnGroupAbandoned(queue_.begin()->first);
      }
      queue_.erase(queue_.begin());
    }
  }
  // Validate the input given previously received markers.
  if (end_of_track_.has_value() && metadata.location > *end_of_track_) {
    QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                      << "track";
    OnMalformedTrack(full_track_name);
    return;
  }
  if (metadata.status == MoqtObjectStatus::kEndOfTrack) {
    if (metadata.location < next_location_) {
      QUICHE_DLOG(INFO) << "EndOfTrack is too early.";
      OnMalformedTrack(full_track_name);
      return;
    }
    // TODO(martinduke): Check that EndOfTrack has normal IDs.
    end_of_track_ = metadata.location;
  }
  auto group_it = queue_.try_emplace(metadata.location.group);
  Group& group = group_it.first->second;
  if (!group_it.second) {  // Group already exists.
    if (group.complete && metadata.location.object >= group.next_object) {
      QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                        << "group";
      OnMalformedTrack(full_track_name);
      return;
    }
    if (metadata.status == MoqtObjectStatus::kEndOfGroup &&
        metadata.location.object < group.next_object) {
      QUICHE_DLOG(INFO) << "Skipping EndOfGroup because it is not the last "
                        << "object in the group.";
      OnMalformedTrack(full_track_name);
      return;
    }
  }
  auto subgroup_it = group.subgroups.try_emplace(metadata.subgroup);
  auto& subgroup = subgroup_it.first->second;
  if (!subgroup.empty()) {  // Check if the new object is valid
    CachedObject& last_object = subgroup.rbegin()->second;
    if (last_object.metadata.publisher_priority !=
        metadata.publisher_priority) {
      QUICHE_DLOG(INFO) << "Publisher priority changing in a subgroup";
      OnMalformedTrack(full_track_name);
      return;
    }
    if (last_object.fin_after_this) {
      QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                        << "subgroup";
      OnMalformedTrack(full_track_name);
      return;
    }
    // If last_object has stream-ending status, it should have been caught by
    // the fin_after_this check above.
    QUICHE_DCHECK(
        last_object.metadata.status != MoqtObjectStatus::kEndOfGroup &&
        last_object.metadata.status != MoqtObjectStatus::kEndOfTrack);
    if (last_object.metadata.location.object >= metadata.location.object) {
      QUICHE_DLOG(INFO) << "Skipping object because it does not increase the "
                        << "object ID monotonically in the subgroup.";
      return;
    }
  }
  // Object is valid. Update state.
  if (next_location_ <= metadata.location) {
    next_location_ = metadata.location.Next();
  }
  if (metadata.location.object >= group.next_object) {
    group.next_object = metadata.location.object + 1;
  }
  // Anticipate stream FIN with most non-normal objects.
  switch (metadata.status) {
    case MoqtObjectStatus::kEndOfTrack:
      end_of_track_ = metadata.location;
      last_object_in_stream = true;
      ABSL_FALLTHROUGH_INTENDED;
    case MoqtObjectStatus::kEndOfGroup:
      group.complete = true;
      last_object_in_stream = true;
      break;
    default:
      break;
  }
  std::shared_ptr<quiche::QuicheMemSlice> slice;
  if (!object.empty()) {
    slice = std::make_shared<quiche::QuicheMemSlice>(
        quiche::QuicheMemSlice::Copy(object));
  }
  subgroup.emplace(metadata.location.object,
                   CachedObject{metadata, slice, last_object_in_stream});
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(metadata.location, metadata.subgroup,
                                   metadata.publisher_priority);
    if (last_object_in_stream) {
      listener->OnNewFinAvailable(metadata.location, metadata.subgroup);
    }
  }
}

void MoqtRelayTrackPublisher::OnStreamFin(const FullTrackName&,
                                          DataStreamIndex stream) {
  auto group_it = queue_.find(stream.group);
  if (group_it == queue_.end()) {
    return;
  }
  auto subgroup_it = group_it->second.subgroups.find(stream.subgroup);
  if (subgroup_it == group_it->second.subgroups.end()) {
    return;
  }
  if (subgroup_it->second.empty()) {
    QUICHE_LOG(INFO) << "got a FIN for an empty subgroup";
    return;
  }
  CachedObject& last_object = subgroup_it->second.rbegin()->second;
  last_object.fin_after_this = true;
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewFinAvailable(last_object.metadata.location, stream.subgroup);
  }
}

void MoqtRelayTrackPublisher::OnStreamReset(const FullTrackName&,
                                            DataStreamIndex stream) {
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnSubgroupAbandoned(stream.group, stream.subgroup,
                                  kResetCodeCanceled);
  }
}

std::optional<PublishedObject> MoqtRelayTrackPublisher::GetCachedObject(
    uint64_t group_id, uint64_t subgroup_id, uint64_t min_object_id) const {
  auto group_it = queue_.find(group_id);
  if (group_it == queue_.end()) {
    // Group does not exist.
    return std::nullopt;
  }
  const Group& group = group_it->second;
  auto subgroup_it = group.subgroups.find(subgroup_id);
  if (subgroup_it == group.subgroups.end()) {
    // Subgroup does not exist.
    return std::nullopt;
  }
  const Subgroup& subgroup = subgroup_it->second;
  if (subgroup.empty()) {
    return std::nullopt;  // There are no objects.
  }
  // Find an object with ID of at least min_object_id.
  auto object_it = subgroup.lower_bound(min_object_id);
  if (object_it == subgroup.end()) {
    // No object after the last one received.
    return std::nullopt;
  }
  return CachedObjectToPublishedObject(object_it->second);
}

void MoqtRelayTrackPublisher::AddObjectListener(MoqtObjectListener* listener) {
  if (listeners_.empty()) {
    MoqtSessionInterface* session = upstream_.GetIfAvailable();
    if (session == nullptr) {
      // upstream went away, reject the subscribe.
      listener->OnSubscribeRejected(MoqtRequestError{
          RequestErrorCode::kInternalError,
          "The upstream session was closed before a subscription could be "
          "established."});
      DeleteTrack();
      return;
    }
    session->SubscribeCurrentObject(track_, this, VersionSpecificParameters());
  }
  listeners_.insert(listener);
}

void MoqtRelayTrackPublisher::RemoveObjectListener(
    MoqtObjectListener* listener) {
  listeners_.erase(listener);
  if (listeners_.empty()) {
    DeleteTrack();
  }
  // No class access below this line!
}

void MoqtRelayTrackPublisher::ForAllObjects(
    quiche::UnretainedCallback<void(const CachedObject&)> callback) {
  for (auto& group_it : queue_) {
    for (auto& subgroup_it : group_it.second.subgroups) {
      for (auto& object_it : subgroup_it.second) {
        callback(object_it.second);
      }
    }
  }
}

std::optional<Location> MoqtRelayTrackPublisher::largest_location() const {
  if (next_location_ == Location(0, 0)) {
    // Nothing observed or reported.
    return std::nullopt;
  }
  return Location{next_location_.group, next_location_.object - 1};
}

std::optional<quic::QuicTimeDelta> MoqtRelayTrackPublisher::expiration() const {
  if (!expiration_.has_value()) {
    return std::nullopt;
  }
  if (expiration_ == quic::QuicTime::Infinite()) {
    return quic::QuicTimeDelta::Infinite();
  }
  quic::QuicTime now = clock_->Now();
  if (expiration_ < now) {
    // TODO(martinduke): Tear everything down; the track is expired.
    return quic::QuicTimeDelta::Zero();
  }
  return *expiration_ - now;
}

void MoqtRelayTrackPublisher::DeleteTrack() {
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnTrackPublisherGone();
  }
  MoqtSessionInterface* session = upstream_.GetIfAvailable();
  if (session != nullptr) {
    session->Unsubscribe(track_);
  }
  std::move(delete_track_callback_)();
}

}  // namespace moqt
