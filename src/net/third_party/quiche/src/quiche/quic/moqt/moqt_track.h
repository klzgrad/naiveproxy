// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
#define QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"

namespace moqt {

// A track on the peer to which the session has subscribed.
class RemoteTrack {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;
    // Called when the session receives a response to the SUBSCRIBE, unless it's
    // a SUBSCRIBE_ERROR with a new track_alias. In that case, the session will
    // automatically retry.
    virtual void OnReply(
        const FullTrackName& full_track_name,
        std::optional<absl::string_view> error_reason_phrase) = 0;
    virtual void OnObjectFragment(
        const FullTrackName& full_track_name, uint64_t group_sequence,
        uint64_t object_sequence, MoqtPriority publisher_priority,
        MoqtObjectStatus object_status,
        MoqtForwardingPreference forwarding_preference,
        absl::string_view object, bool end_of_message) = 0;
    // TODO(martinduke): Add final sequence numbers
  };
  RemoteTrack(const FullTrackName& full_track_name, uint64_t track_alias,
              Visitor* visitor)
      : full_track_name_(full_track_name),
        track_alias_(track_alias),
        visitor_(visitor) {}

  const FullTrackName& full_track_name() { return full_track_name_; }

  uint64_t track_alias() const { return track_alias_; }

  Visitor* visitor() { return visitor_; }

  // When called while processing the first object in the track, sets the
  // forwarding preference to the value indicated by the incoming encoding.
  // Otherwise, returns true if the incoming object does not violate the rule
  // that the preference is consistent.
  bool CheckForwardingPreference(MoqtForwardingPreference preference);

 private:
  // TODO: There is no accounting for the number of outstanding subscribes,
  // because we can't match track names to individual subscribes.
  const FullTrackName full_track_name_;
  const uint64_t track_alias_;
  Visitor* visitor_;
  std::optional<MoqtForwardingPreference> forwarding_preference_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
