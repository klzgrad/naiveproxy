// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_PUBLISHER_H_

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

// PublishedObject is a description of an object that is sufficient to publish
// it on a given track.
struct PublishedObject {
  FullSequence sequence;
  MoqtObjectStatus status;
  quiche::QuicheMemSlice payload;
};

// MoqtObjectListener is an interface for any entity that is listening for
// incoming objects for a given track.
class MoqtObjectListener {
 public:
  virtual ~MoqtObjectListener() = default;

  // Notifies that an object with the given sequence number has become
  // available.  The object payload itself may be retrieved via GetCachedObject
  // method of the associated track publisher.
  virtual void OnNewObjectAvailable(FullSequence sequence) = 0;
};

// MoqtTrackPublisher is an application-side API for an MoQT publisher
// of a single individual track.
class MoqtTrackPublisher {
 public:
  virtual ~MoqtTrackPublisher() = default;

  // Returns the full name of the associated track.
  virtual const FullTrackName& GetTrackName() const = 0;

  // GetCachedObject lets the MoQT stack access the objects that are available
  // in the track's built-in local cache.
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
      FullSequence sequence) const = 0;

  // Returns a full list of objects available in the cache, to be used for
  // SUBSCRIBEs with a backfill.
  virtual std::vector<FullSequence> GetCachedObjectsInRange(
      FullSequence start, FullSequence end) const = 0;

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

  // Returns the largest sequence pair that has been published so far.
  // This method may only be called if
  // DoesTrackStatusImplyHavingData(GetTrackStatus()) is true.
  virtual FullSequence GetLargestSequence() const = 0;

  // Returns the forwarding preference of the track.
  // This method may only be called if
  // DoesTrackStatusImplyHavingData(GetTrackStatus()) is true.
  virtual MoqtForwardingPreference GetForwardingPreference() const = 0;

  // Returns the current forwarding priority of the track.
  virtual MoqtPriority GetPublisherPriority() const = 0;

  // Returns the publisher-preferred delivery order for the track.
  virtual MoqtDeliveryOrder GetDeliveryOrder() const = 0;
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
