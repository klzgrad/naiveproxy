// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
#define QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"

namespace moqt {

// A track to which the peer might subscribe.
class LocalTrack {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;

    // Requests that application re-publish objects from {start_group,
    // start_object} to the latest object. If the return value is nullopt, the
    // subscribe is valid and the application will deliver the object and
    // the session will send SUBSCRIBE_OK. If the return has a value, the value
    // is the error message (the session will send SUBSCRIBE_ERROR). Via this
    // API, the application decides if a partially fulfillable
    // SUBSCRIBE_REQUEST results in an error or not.
    virtual std::optional<absl::string_view> OnSubscribeRequestForPast(
        const SubscribeWindow& window) = 0;
  };
  // |visitor| must not be nullptr.
  LocalTrack(const FullTrackName& full_track_name, uint64_t track_alias,
             Visitor* visitor)
      : full_track_name_(full_track_name),
        track_alias_(track_alias),
        visitor_(visitor) {}
  // Creates a LocalTrack that does not start at sequence (0,0)
  LocalTrack(const FullTrackName& full_track_name, uint64_t track_alias,
             Visitor* visitor, FullSequence next_sequence)
      : full_track_name_(full_track_name),
        track_alias_(track_alias),
        next_sequence_(next_sequence),
        visitor_(visitor) {}

  const FullTrackName& full_track_name() const { return full_track_name_; }

  uint64_t track_alias() const { return track_alias_; }

  Visitor* visitor() { return visitor_; }

  bool ShouldSend(uint64_t group, uint64_t object) const {
    return windows_.SequenceIsSubscribed(group, object);
  }

  void AddWindow(SubscribeWindow window) { windows_.AddWindow(window); }

  // Returns the largest observed sequence, but increments the object sequence
  // by one.
  const FullSequence& next_sequence() const { return next_sequence_; }

  FullSequence& next_sequence_mutable() { return next_sequence_; }

  bool HasSubscriber() const { return !windows_.IsEmpty(); }

 private:
  // This only needs to track subscriptions to current and future objects;
  // requests for objects in the past are forwarded to the application.
  const FullTrackName full_track_name_;
  const uint64_t track_alias_;
  // The sequence numbers from this track to which the peer is subscribed.
  MoqtSubscribeWindows windows_;
  FullSequence next_sequence_ = {0, 0};
  Visitor* visitor_;
};

// A track on the peer to which the session has subscribed.
class RemoteTrack {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;
    // Called when the session receives a response to the SUBSCRIBE_REQUEST.
    virtual void OnReply(
        const FullTrackName& full_track_name,
        std::optional<absl::string_view> error_reason_phrase) = 0;
    virtual void OnObjectFragment(const FullTrackName& full_track_name,
                                  uint32_t stream_id, uint64_t group_sequence,
                                  uint64_t object_sequence,
                                  uint64_t object_send_order,
                                  absl::string_view object,
                                  bool end_of_message) = 0;
    // TODO(martinduke): Add final sequence numbers
  };
  RemoteTrack(const FullTrackName& full_track_name, Visitor* visitor)
      : full_track_name_(full_track_name), visitor_(visitor) {}

  const FullTrackName& full_track_name() { return full_track_name_; }

  std::optional<uint64_t> track_alias() const { return track_alias_; }

  Visitor* visitor() { return visitor_; }

  void set_track_alias(uint64_t track_alias) { track_alias_ = track_alias; }

 private:
  // TODO: There is no accounting for the number of outstanding subscribes,
  // because we can't match track names to individual subscribes.
  FullTrackName full_track_name_;
  std::optional<uint64_t> track_alias_;
  Visitor* visitor_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
