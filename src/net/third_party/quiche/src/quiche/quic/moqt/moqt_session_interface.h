// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

// If |error_message| is nullopt, this is triggered by an ANNOUNCE_OK.
// Otherwise, it is triggered by ANNOUNCE_ERROR or ANNOUNCE_CANCEL. For
// ERROR or CANCEL, MoqtSession is deleting all ANNOUNCE state immediately
// after calling this callback. Alternatively, the application can call
// Unannounce() to delete the state.
using MoqtOutgoingAnnounceCallback = quiche::MultiUseCallback<void(
    FullTrackName track_namespace,
    std::optional<MoqtAnnounceErrorReason> error)>;

using MoqtOutgoingSubscribeAnnouncesCallback = quiche::SingleUseCallback<void(
    FullTrackName track_namespace, std::optional<RequestErrorCode> error,
    absl::string_view reason)>;

class MoqtSessionInterface {
 public:
  virtual ~MoqtSessionInterface() = default;

  // TODO: move ANNOUNCE logic here.

  // Callbacks for session-level events.
  virtual MoqtSessionCallbacks& callbacks() = 0;

  // Close the session with a fatal error.
  virtual void Error(MoqtError code, absl::string_view error) = 0;

  // Methods below send a SUBSCRIBE for the specified track, and return true if
  // SUBSCRIBE was actually sent.

  // Subscribe from (start_group, start_object) to the end of the track.
  virtual bool SubscribeAbsolute(const FullTrackName& name,
                                 uint64_t start_group, uint64_t start_object,
                                 SubscribeRemoteTrack::Visitor* visitor,
                                 VersionSpecificParameters parameters) = 0;
  // Subscribe from (start_group, start_object) to the end of end_group.
  virtual bool SubscribeAbsolute(const FullTrackName& name,
                                 uint64_t start_group, uint64_t start_object,
                                 uint64_t end_group,
                                 SubscribeRemoteTrack::Visitor* visitor,
                                 VersionSpecificParameters parameters) = 0;
  // Subscribe to all objects that are larger than the current Largest
  // Group/Object ID.
  virtual bool SubscribeCurrentObject(const FullTrackName& name,
                                      SubscribeRemoteTrack::Visitor* visitor,
                                      VersionSpecificParameters parameters) = 0;
  // Start with the first group after the current Largest Group/Object ID.
  virtual bool SubscribeNextGroup(const FullTrackName& name,
                                  SubscribeRemoteTrack::Visitor* visitor,
                                  VersionSpecificParameters parameters) = 0;

  // If an argument is nullopt, there is no change to the current value.
  virtual bool SubscribeUpdate(const FullTrackName& name,
                               std::optional<Location> start,
                               std::optional<uint64_t> end_group,
                               std::optional<MoqtPriority> subscriber_priority,
                               std::optional<bool> forward,
                               VersionSpecificParameters parameters) = 0;

  // Sends an UNSUBSCRIBE message and removes all of the state related to the
  // subscription.  Returns false if the subscription is not found.
  virtual void Unsubscribe(const FullTrackName& name) = 0;

  // Sends a FETCH for a pre-specified object range.  Once a FETCH_OK or a
  // FETCH_ERROR is received, `callback` is called with a MoqtFetchTask that can
  // be used to process the FETCH further.  To cancel a FETCH, simply destroy
  // the MoqtFetchTask.
  virtual bool Fetch(const FullTrackName& name, FetchResponseCallback callback,
                     Location start, uint64_t end_group,
                     std::optional<uint64_t> end_object, MoqtPriority priority,
                     std::optional<MoqtDeliveryOrder> delivery_order,
                     VersionSpecificParameters parameters) = 0;

  // Sends both a SUBSCRIBE and a joining FETCH, beginning `num_previous_groups`
  // groups before the current group. The Fetch will not be flow controlled,
  // instead using |visitor| to deliver fetched objects when they arrive. Gaps
  // in the FETCH will not be filled by with ObjectDoesNotExist. If the FETCH
  // fails for any reason, the application will not receive a notification; it
  // will just appear to be missing objects.
  virtual bool JoiningFetch(const FullTrackName& name,
                            SubscribeRemoteTrack::Visitor* visitor,
                            uint64_t num_previous_groups,
                            VersionSpecificParameters parameters) = 0;

  // Sends both a SUBSCRIBE and a joining FETCH, beginning `num_previous_groups`
  // groups before the current group.  `callback` acts the same way as the
  // callback for the regular Fetch() call.
  virtual bool JoiningFetch(const FullTrackName& name,
                            SubscribeRemoteTrack::Visitor* visitor,
                            FetchResponseCallback callback,
                            uint64_t num_previous_groups, MoqtPriority priority,
                            std::optional<MoqtDeliveryOrder> delivery_order,
                            VersionSpecificParameters parameters) = 0;

  // TODO: Add SubscribeAnnounces, UnsubscribeAnnounces method.
  // TODO: Add Announce, Unannounce method.
  // TODO: Add AnnounceCancel method.
  // TODO: Add TrackStatusRequest method.
  // TODO: Add SubscribeUpdate, SubscribeDone method.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
