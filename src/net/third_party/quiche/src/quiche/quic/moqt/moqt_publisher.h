// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// PublishedObject is a description of an object that is sufficient to publish
// it on a given track.
struct PublishedObject {
  Location sequence;
  MoqtObjectStatus status;
  MoqtPriority publisher_priority;
  quiche::QuicheMemSlice payload;
  quic::QuicTime arrival_time = quic::QuicTime::Zero();
  bool fin_after_this = false;
};

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
  virtual void OnSubscribeRejected(
      MoqtSubscribeErrorReason reason,
      std::optional<uint64_t> track_alias = std::nullopt) = 0;

  // Notifies that an object with the given sequence number has become
  // available.  The object payload itself may be retrieved via GetCachedObject
  // method of the associated track publisher.
  virtual void OnNewObjectAvailable(Location sequence) = 0;
  // Notifies that a pure FIN has arrived following |sequence|. Should not be
  // called unless all objects have already been delivered. If not delivered,
  // instead set the fin_after_this flag in the PublishedObject.
  virtual void OnNewFinAvailable(Location sequence) = 0;
  // Notifies that the a stream is being abandoned (via RESET_STREAM) before
  // all objects are delivered.
  virtual void OnSubgroupAbandoned(
      Location sequence, webtransport::StreamErrorCode error_code) = 0;

  // No further object will be published for the given group, usually due to a
  // timeout. The owner of the Listener may want to reset the relevant streams.
  virtual void OnGroupAbandoned(uint64_t group_id) = 0;

  // Notifies that the Publisher is being destroyed, so no more objects are
  // coming.
  virtual void OnTrackPublisherGone() = 0;
};

// A handle representing a fetch in progress.  The fetch in question can be
// cancelled by deleting the object.
class MoqtFetchTask {
 public:
  using ObjectsAvailableCallback = quiche::MultiUseCallback<void()>;
  // If the fields are not correct (e.g. end_of_track is less than start) it
  // will result in QUICHE_BUG. The request_id field will be ignored.
  using FetchResponseCallback = quiche::SingleUseCallback<void(
      std::variant<MoqtFetchOk, MoqtFetchError>)>;

  virtual ~MoqtFetchTask() = default;

  // Potential results of a GetNextObject() call.
  enum GetNextObjectResult {
    // The next object is available, and is placed into the reference specified
    // by the caller.
    kSuccess,
    // The next object is not yet available (equivalent of EAGAIN).
    kPending,
    // The end of fetch has been reached.
    kEof,
    // The fetch has failed; the error is available via GetStatus().
    kError,
  };

  // Returns the next object received via the fetch, if available. MUST NOT
  // return an object with status kObjectDoesNotExist.
  virtual GetNextObjectResult GetNextObject(PublishedObject& output) = 0;

  // Sets the callback that is called when GetNextObject() has previously
  // returned kPending, but now a new object (or potentially an error or an
  // end-of-fetch) is available. The application is responsible for calling
  // GetNextObject() until it gets kPending; no further callback will occur
  // until then.
  // If an object is available immediately, the callback will be called
  // immediately.
  virtual void SetObjectAvailableCallback(
      ObjectsAvailableCallback callback) = 0;
  // One of these callbacks is called as soon as the data publisher has enough
  // information for either FETCH_OK or FETCH_ERROR.
  // If the appropriate response is already available, the callback will be
  // called immediately.
  virtual void SetFetchResponseCallback(FetchResponseCallback callback) = 0;

  // Returns the error if fetch has completely failed, and OK otherwise.
  virtual absl::Status GetStatus() = 0;
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
  // sequence.object that matches (sequence.group, sequence.subgroup).
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
  // kGroupDoesNotExist/kObjectDoesNotExist has to be returned instead;
  // otherwise, the corresponding QUIC streams will be stuck waiting for objects
  // that will never arrive.
  virtual std::optional<PublishedObject> GetCachedObject(
      Location sequence) const = 0;

  // Returns a full list of objects available in the cache, to be used for
  // SUBSCRIBEs with a backfill. Returned in order of worsening priority.
  virtual std::vector<Location> GetCachedObjectsInRange(Location start,
                                                        Location end) const = 0;

  // TODO: add an API to fetch past objects that are out of cache and might
  // require an upstream request to fill the relevant cache again. This is
  // currently done since the specification does not clearly describe how this
  // is supposed to be done, especially with respect to such things as
  // backpressure.

  // Registers a listener with the track.  The listener will be notified of all
  // newly arriving objects. The pointer to the listener must be valid until
  // removed.
  virtual void AddObjectListener(MoqtObjectListener* listener) = 0;
  virtual void RemoveObjectListener(MoqtObjectListener* listener) = 0;

  virtual absl::StatusOr<MoqtTrackStatusCode> GetTrackStatus() const = 0;

  // Returns the largest (group, object) pair that has been published so far.
  // This method may only be called if
  // DoesTrackStatusImplyHavingData(GetTrackStatus()) is true.
  virtual Location GetLargestLocation() const = 0;

  // Returns the forwarding preference of the track.
  // This method may only be called if
  // DoesTrackStatusImplyHavingData(GetTrackStatus()) is true.
  virtual MoqtForwardingPreference GetForwardingPreference() const = 0;

  // Returns the current forwarding priority of the track.
  virtual MoqtPriority GetPublisherPriority() const = 0;

  // Returns the publisher-preferred delivery order for the track.
  virtual MoqtDeliveryOrder GetDeliveryOrder() const = 0;

  // Performs a fetch for the specified range of objects.
  virtual std::unique_ptr<MoqtFetchTask> Fetch(
      Location start, uint64_t end_group, std::optional<uint64_t> end_object,
      MoqtDeliveryOrder order) = 0;
};

// MoqtPublisher is an interface to a publisher that allows it to publish
// multiple tracks.
class MoqtPublisher {
 public:
  virtual ~MoqtPublisher() = default;

  virtual absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> GetTrack(
      const FullTrackName& track_name) = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_
