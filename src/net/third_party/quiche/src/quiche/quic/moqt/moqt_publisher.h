// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/base/nullability.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// MoqtObjectListener is an interface for any entity that is listening for
// incoming objects for a given track.
class MoqtObjectListener {
 public:
  virtual ~MoqtObjectListener() = default;

  // Called when the publisher is sure that it can serve the subscription. This
  // could happen synchronously or asynchronously.Details necessary for the
  // SUBSCRIBE_OK can be obtained from the MoqtTrackPublisher.
  virtual void OnSubscribeAccepted() = 0;
  // Called when the publisher is sure that it cannot serve the subscription.
  // This could happen synchronously or asynchronously.
  virtual void OnSubscribeRejected(MoqtSubscribeErrorReason reason) = 0;

  // Notifies that a new object is available on the track.  The object payload
  // itself may be retrieved via GetCachedObject method of the associated track
  // publisher.
  virtual void OnNewObjectAvailable(Location sequence, uint64_t subgroup,
                                    MoqtPriority publisher_priority) = 0;
  // Notifies that a pure FIN has arrived following |sequence|. Should not be
  // called unless all objects have already been delivered. If not delivered,
  // instead set the fin_after_this flag in the PublishedObject.
  virtual void OnNewFinAvailable(Location final_object_in_subgroup,
                                 uint64_t subgroup_id) = 0;
  // Notifies that the a stream is being abandoned (via RESET_STREAM) before
  // all objects are delivered.
  virtual void OnSubgroupAbandoned(
      uint64_t group, uint64_t subgroup,
      webtransport::StreamErrorCode error_code) = 0;

  // No further object will be published for the given group, usually due to a
  // timeout. The owner of the Listener may want to reset the relevant streams.
  virtual void OnGroupAbandoned(uint64_t group_id) = 0;

  // Notifies that the Publisher is being destroyed, so no more objects are
  // coming.
  virtual void OnTrackPublisherGone() = 0;
};

// MoqtTrackPublisher is an application-side API for an MoQT publisher
// of a single individual track.
class MoqtTrackPublisher {
 public:
  virtual ~MoqtTrackPublisher() = default;

  // Returns the full name of the associated track.
  virtual const FullTrackName& GetTrackName() const = 0;

  // GetCachedObject lets the MoQT stack access the objects that are available
  // in the track's built-in local cache. Retrieves the first object ID >=
  // min_object that matches (sequence.group, sequence.subgroup).
  //
  // This implementation of MoQT does not store any objects within the MoQT
  // stack itself, at least until the object is fully serialized and passed to
  // the QUIC stack. Instead, it relies on individual tracks having a shared
  // cache for recent objects, and objects are always pulled from that cache
  // whenever they are sent.  Once an object is not available via the cache, it
  // can no longer be sent; this ensures that objects are not buffered forever.
  //
  // This method returns nullopt if the object is not currently available, but
  // might become available in the future.  If the object is gone forever,
  // kEndOfGroup/kObjectDoesNotExist has to be returned instead;
  // otherwise, the corresponding QUIC streams will be stuck waiting for objects
  // that will never arrive.
  virtual std::optional<PublishedObject> GetCachedObject(
      uint64_t group, uint64_t subgroup, uint64_t min_object) const = 0;

  // Registers a listener with the track.  The listener will be notified of all
  // newly arriving objects. The pointer to the listener must be valid until
  // removed.
  virtual void AddObjectListener(MoqtObjectListener* listener) = 0;
  virtual void RemoveObjectListener(MoqtObjectListener* listener) = 0;

  // Methods to return various track properties. Returns nullopt if the value is
  // not yet available. Guaranteed to be non-null if an object is available
  // and/or OnSubscribeAccepted() has been called.
  // Track alias is not present because MoqtSession always uses locally
  // generated values.
  virtual std::optional<Location> largest_location() const = 0;
  virtual std::optional<MoqtForwardingPreference> forwarding_preference()
      const = 0;
  virtual std::optional<MoqtDeliveryOrder> delivery_order() const = 0;
  virtual std::optional<quic::QuicTimeDelta> expiration() const = 0;

  // Performs a fetch for the specified range of objects.
  virtual std::unique_ptr<MoqtFetchTask> StandaloneFetch(
      Location start, Location end, std::optional<MoqtDeliveryOrder> order) = 0;
  virtual std::unique_ptr<MoqtFetchTask> RelativeFetch(
      uint64_t group_diff, std::optional<MoqtDeliveryOrder> order) = 0;
  virtual std::unique_ptr<MoqtFetchTask> AbsoluteFetch(
      uint64_t group, std::optional<MoqtDeliveryOrder> order) = 0;
};

// MoqtPublisher is an interface to a publisher that allows it to publish
// multiple tracks.
class MoqtPublisher {
 public:
  virtual ~MoqtPublisher() = default;

  // Called by MoqtSession based on messages arriving on the wire.
  virtual absl_nullable std::shared_ptr<MoqtTrackPublisher> GetTrack(
      const FullTrackName& track_name) = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_
