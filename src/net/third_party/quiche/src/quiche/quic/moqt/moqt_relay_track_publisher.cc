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
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

void MoqtRelayTrackPublisher::OnReply(
    const FullTrackName&,
    std::variant<SubscribeOkData, MoqtRequestErrorInfo> response) {
  if (is_closing_) {
    return;
  }
  if (std::holds_alternative<MoqtRequestErrorInfo>(response)) {
    auto request_error = std::get<MoqtRequestErrorInfo>(response);
    // Delete upstream_ to avoid sending UNSUBSCRIBE.
    upstream_ = quiche::QuicheWeakPtr<MoqtSessionInterface>();
    // Sessions will delete listeners, causing the track to delete itself.
    for (MoqtObjectListener* listener : listeners_) {
      listener->OnSubscribeRejected(request_error);
    }
    return;
  }
  SubscribeOkData ok_data = std::get<SubscribeOkData>(response);
  quic::QuicTimeDelta expires =
      ok_data.parameters.expires.value_or(kDefaultExpires);
  expiration_ = expires.IsInfinite() ? quic::QuicTime::Infinite()
                                     : clock_->Now() + expires;
  extensions_ = ok_data.extensions;
  next_location_ = ok_data.parameters.largest_object.has_value()
                       ? ok_data.parameters.largest_object->Next()
                       : Location(0, 0);
  got_response_ = true;
  // TODO(martinduke): Handle parameters.
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnSubscribeAccepted();
  }
}

void MoqtRelayTrackPublisher::OnObjectFragment(
    const FullTrackName& full_track_name,
    const PublishedObjectMetadata& metadata, absl::string_view object,
    bool end_of_message) {
  if (is_closing_) {
    return;
  }
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
  CachedObject* duplicate_object = nullptr;
  if (!metadata.subgroup.has_value()) {  // It's a datagram.
    std::shared_ptr<quiche::QuicheMemSlice> slice;
    if (!object.empty()) {
      slice = std::make_shared<quiche::QuicheMemSlice>(
          quiche::QuicheMemSlice::Copy(object));
    }
    auto [it, inserted] = group.datagrams.try_emplace(
        metadata.location.object, CachedObject{metadata, slice, false});
    if (!inserted) {
      duplicate_object = &it->second;
    }
  } else {
    auto subgroup_it = group.subgroups.try_emplace(*metadata.subgroup);
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
        QUICHE_DLOG(INFO) << "Skipping object because it is after the end of "
                          << "the subgroup";
        OnMalformedTrack(full_track_name);
        return;
      }
      // If last_object has stream-ending status, it should have been caught by
      // the fin_after_this check above.
      QUICHE_DCHECK(
          last_object.metadata.status != MoqtObjectStatus::kEndOfGroup &&
          last_object.metadata.status != MoqtObjectStatus::kEndOfTrack);
      if (last_object.metadata.location.object > metadata.location.object) {
        QUICHE_DLOG(INFO) << "Skipping object because it decreases the "
                          << "object ID in the subgroup.";
        return;
      }
    }
    if (metadata.status == MoqtObjectStatus::kEndOfGroup ||
        metadata.status == MoqtObjectStatus::kEndOfTrack) {
      // Anticipate stream FIN.
      last_object_in_stream = true;
    }
    std::shared_ptr<quiche::QuicheMemSlice> slice;
    if (!object.empty()) {
      slice = std::make_shared<quiche::QuicheMemSlice>(
          quiche::QuicheMemSlice::Copy(object));
    }
    auto [it, inserted] = subgroup.try_emplace(
        metadata.location.object,
        CachedObject{metadata, slice, last_object_in_stream});
    if (!inserted) {
      duplicate_object = &it->second;
    }
  }
  if (duplicate_object != nullptr) {
    if (metadata.IsMalformed(duplicate_object->metadata)) {
      // Something besides the arrival time and extension headers changed.
      OnMalformedTrack(full_track_name);
      return;
    }
    // TODO(b/467718801): Fix this when the class supports partial object
    // delivery. When objects are complete, we can simply compare payloads.
    if (duplicate_object->payload->AsStringView() != object) {
      OnMalformedTrack(full_track_name);
    }
    // No need to update state.
    return;
  }
  // Object is valid. Update state.
  if (next_location_ <= metadata.location) {
    next_location_ = metadata.location.Next();
  }
  if (metadata.location.object >= group.next_object) {
    group.next_object = metadata.location.object + 1;
  }
  switch (metadata.status) {
    case MoqtObjectStatus::kEndOfTrack:
      end_of_track_ = metadata.location;
      ABSL_FALLTHROUGH_INTENDED;
    case MoqtObjectStatus::kEndOfGroup:
      group.complete = true;
      break;
    default:
      break;
  }
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(metadata.location, metadata.subgroup,
                                   metadata.publisher_priority);
    if (last_object_in_stream) {
      listener->OnNewFinAvailable(metadata.location, *(metadata.subgroup));
    }
  }
}

void MoqtRelayTrackPublisher::OnPublishDone(FullTrackName full_track_name) {
  if (full_track_name != track_) {
    QUICHE_BUG(moqt_got_wrong_track) << "Received object for wrong track.";
    return;
  }
  if (is_closing_) {
    return;
  }
  // Reset all the streams so that PUBLISH_DONE kills the subscription.
  // TODO(martinduke): This should vary based on the error code. If it was a
  // clean PUBLISH_DONE, allow the streams to complete.
  for (auto& [group, group_data] : queue_) {
    for (auto& [subgroup, subgroup_data] : group_data.subgroups) {
      for (MoqtObjectListener* listener : listeners_) {
        listener->OnSubgroupAbandoned(group, subgroup, kResetCodeCancelled);
      }
    }
  }
  is_closing_ = true;
  while (!listeners_.empty()) {
    (*listeners_.begin())->OnTrackPublisherGone();
  }
  upstream_ = quiche::QuicheWeakPtr<MoqtSessionInterface>();
  DeleteTrack();
  // No class access below this line!
}

void MoqtRelayTrackPublisher::OnStreamFin(const FullTrackName&,
                                          DataStreamIndex stream) {
  if (is_closing_) {
    return;
  }
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
  if (is_closing_) {
    return;
  }
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnSubgroupAbandoned(stream.group, stream.subgroup,
                                  kResetCodeCancelled);
  }
}

std::optional<PublishedObject> MoqtRelayTrackPublisher::GetCachedObject(
    uint64_t group_id, std::optional<uint64_t> subgroup_id,
    uint64_t min_object_id) const {
  auto group_it = queue_.find(group_id);
  if (group_it == queue_.end()) {
    // Group does not exist.
    return std::nullopt;
  }
  const Group& group = group_it->second;
  if (!subgroup_id.has_value()) {
    auto object_it = group.datagrams.lower_bound(min_object_id);
    if (object_it == group.datagrams.end()) {
      // No object after the last one received.
      return std::nullopt;
    }
    return CachedObjectToPublishedObject(object_it->second);
  }
  auto subgroup_it = group.subgroups.find(*subgroup_id);
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
  if (is_closing_) {
    return;
  }
  if (listeners_.empty()) {
    MoqtSessionInterface* session = upstream_.GetIfAvailable();
    if (session == nullptr) {
      // upstream went away, reject the subscribe.
      listener->OnSubscribeRejected(MoqtRequestErrorInfo{
          RequestErrorCode::kInternalError, std::nullopt,
          "The upstream session was closed before a subscription could be "
          "established."});
      DeleteTrack();
      return;
    }
    MessageParameters parameters;
    // Use default params, not what the subscriber used.
    // TODO(b/478300706): Always forward NEW_GROUP_REQUEST in this case.
    session->Subscribe(track_, this, parameters);
  }
  listeners_.insert(listener);
  // TODO(b/478300706): If there is a NEW_GROUP_REQUEST and we don't have one
  // pending, send it.
  if (got_response_) {
    listener->OnSubscribeAccepted();
  }
}

void MoqtRelayTrackPublisher::RemoveObjectListener(
    MoqtObjectListener* listener) {
  listeners_.erase(listener);
  if (is_closing_) {
    return;
  }
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
  if (!expiration_.has_value() || *expiration_ == quic::QuicTime::Infinite()) {
    return std::nullopt;
  }
  quic::QuicTime now = clock_->Now();
  if (expiration_ < now) {
    // TODO(martinduke): Tear everything down; the track is expired.
    return quic::QuicTimeDelta::Zero();
  }
  return *expiration_ - now;
}

void MoqtRelayTrackPublisher::DeleteTrack() {
  is_closing_ = true;
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
