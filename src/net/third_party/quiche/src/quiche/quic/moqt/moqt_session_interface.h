// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

using MoqtObjectAckFunction =
    quiche::MultiUseCallback<void(uint64_t group_id, uint64_t object_id,
                                  quic::QuicTimeDelta delta_from_deadline)>;

struct SubscribeOkData {
  quic::QuicTimeDelta expires;
  MoqtDeliveryOrder delivery_order;
  std::optional<Location> largest_location;
  VersionSpecificParameters parameters = VersionSpecificParameters();
};

class SubscribeVisitor {
 public:
  virtual ~SubscribeVisitor() = default;
  // Called when the session receives a response to the SUBSCRIBE, unless it's
  // a SUBSCRIBE_ERROR with a new track_alias. In that case, the session will
  // automatically retry.
  virtual void OnReply(
      const FullTrackName& full_track_name,
      std::variant<SubscribeOkData, MoqtRequestError> response) = 0;
  // Called when the subscription process is far enough that it is possible to
  // send OBJECT_ACK messages; provides a callback to do so. The callback is
  // valid for as long as the session is valid.
  virtual void OnCanAckObjects(MoqtObjectAckFunction ack_function) = 0;
  // Called when an object fragment (or an entire object) is received.
  virtual void OnObjectFragment(const FullTrackName& full_track_name,
                                const PublishedObjectMetadata& metadata,
                                absl::string_view object,
                                bool end_of_message) = 0;
  virtual void OnPublishDone(FullTrackName full_track_name) = 0;
  // Called when the track is malformed per Section 2.5 of
  // draft-ietf-moqt-moq-transport-12. If the application is a relay, it MUST
  // terminate downstream delivery of the track.
  virtual void OnMalformedTrack(const FullTrackName& full_track_name) = 0;

  // End user applications might not care about stream state, but relays will.
  virtual void OnStreamFin(const FullTrackName& full_track_name,
                           DataStreamIndex stream) = 0;
  virtual void OnStreamReset(const FullTrackName& full_track_name,
                             DataStreamIndex stream) = 0;
};

// MoqtSession calls this when a FETCH_OK or FETCH_ERROR is received. The
// destination of the callback owns |fetch_task| and MoqtSession will react
// safely if the owner destroys it.
using FetchResponseCallback =
    quiche::SingleUseCallback<void(std::unique_ptr<MoqtFetchTask> fetch_task)>;

// TODO(martinduke): MoqtOutgoingPublishNamespaceCallback and
// MoqtOutgoingSubscribeNamespaceCallback are deprecated. Remove.

// If |error| is nullopt, this is triggered by a PUBLISH_NAMESPACE_OK.
// Otherwise, it is triggered by PUBLISH_NAMESPACE_ERROR or
// PUBLISH_NAMESPACE_CANCEL. For ERROR or CANCEL, MoqtSession is deleting all
// PUBLISH_NAMESPACE state immediately after calling this callback.
// Alternatively, the application can call PublishNamespaceDone() to delete the
// state.
using MoqtOutgoingPublishNamespaceCallback =
    quiche::MultiUseCallback<void(const TrackNamespace& track_namespace,
                                  std::optional<MoqtRequestError> error)>;

using MoqtOutgoingSubscribeNamespaceCallback = quiche::SingleUseCallback<void(
    TrackNamespace track_namespace, std::optional<RequestErrorCode> error,
    absl::string_view reason)>;

class MoqtSessionInterface {
 public:
  virtual ~MoqtSessionInterface() = default;

  // TODO: move PUBLISH_NAMESPACE logic here.

  // Callbacks for session-level events.
  virtual MoqtSessionCallbacks& callbacks() = 0;

  // Close the session with a fatal error.
  virtual void Error(MoqtError code, absl::string_view error) = 0;

  // Methods below send a SUBSCRIBE for the specified track, and return true if
  // SUBSCRIBE was actually sent.

  // Subscribe from (start_group, start_object) to the end of the track.
  virtual bool SubscribeAbsolute(const FullTrackName& name,
                                 uint64_t start_group, uint64_t start_object,
                                 SubscribeVisitor* visitor,
                                 VersionSpecificParameters parameters) = 0;
  // Subscribe from (start_group, start_object) to the end of end_group.
  virtual bool SubscribeAbsolute(const FullTrackName& name,
                                 uint64_t start_group, uint64_t start_object,
                                 uint64_t end_group, SubscribeVisitor* visitor,
                                 VersionSpecificParameters parameters) = 0;
  // Subscribe to all objects that are larger than the current Largest
  // Group/Object ID.
  virtual bool SubscribeCurrentObject(const FullTrackName& name,
                                      SubscribeVisitor* visitor,
                                      VersionSpecificParameters parameters) = 0;
  // Start with the first group after the current Largest Group/Object ID.
  virtual bool SubscribeNextGroup(const FullTrackName& name,
                                  SubscribeVisitor* visitor,
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
  virtual bool RelativeJoiningFetch(const FullTrackName& name,
                                    SubscribeVisitor* visitor,
                                    uint64_t num_previous_groups,
                                    VersionSpecificParameters parameters) = 0;

  // Sends both a SUBSCRIBE and a joining FETCH, beginning `num_previous_groups`
  // groups before the current group.  `callback` acts the same way as the
  // callback for the regular Fetch() call.
  virtual bool RelativeJoiningFetch(
      const FullTrackName& name, SubscribeVisitor* visitor,
      FetchResponseCallback callback, uint64_t num_previous_groups,
      MoqtPriority priority, std::optional<MoqtDeliveryOrder> delivery_order,
      VersionSpecificParameters parameters) = 0;
  // Send a PUBLISH_NAMESPACE message for |track_namespace|, and call
  // |publish_namespace_callback| when the response arrives. Will fail
  // immediately if there is already an unresolved PUBLISH_NAMESPACE for that
  // namespace.
  virtual void PublishNamespace(TrackNamespace track_namespace,
                                MoqtOutgoingPublishNamespaceCallback callback,
                                VersionSpecificParameters parameters) = 0;
  // Returns true if message was sent, false if there is no PUBLISH_NAMESPACE to
  // cancel.
  virtual bool PublishNamespaceDone(TrackNamespace track_namespace) = 0;

  // TODO(martinduke): Add an API for absolute joining fetch.

  // TODO: Add SubscribeNamespace, UnsubscribeNamespace method.
  // TODO: Add PublishNamespaceCancel method.
  // TODO: Add TrackStatusRequest method.
  // TODO: Add SubscribeUpdate, PublishDone method.

  virtual quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
