// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
#define QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtSessionPeer;
class SubscribeRemoteTrackPeer;
}  // namespace test

using MoqtObjectAckFunction =
    quiche::MultiUseCallback<void(uint64_t group_id, uint64_t object_id,
                                  quic::QuicTimeDelta delta_from_deadline)>;

// State common to both SUBSCRIBE and FETCH upstream.
class RemoteTrack {
 public:
  RemoteTrack(const FullTrackName& full_track_name, uint64_t id,
              SubscribeWindow window, MoqtPriority priority)
      : full_track_name_(full_track_name),
        request_id_(id),
        subscriber_priority_(priority),
        window_(window),
        weak_ptr_factory_(this) {}
  virtual ~RemoteTrack() = default;

  FullTrackName full_track_name() const { return full_track_name_; }
  // If FETCH_ERROR or SUBSCRIBE_ERROR arrives after OK or an object, it is a
  // protocol violation.
  virtual void OnObjectOrOk() { error_is_allowed_ = false; }
  bool ErrorIsAllowed() const { return error_is_allowed_; }

  // Makes sure the data stream type is consistent with the track type.
  bool CheckDataStreamType(MoqtDataStreamType type);

  uint64_t request_id() const { return request_id_; }

  // Is the object one that was requested?
  bool InWindow(Location sequence) const { return window_.InWindow(sequence); }

  quiche::QuicheWeakPtr<RemoteTrack> weak_ptr() {
    return weak_ptr_factory_.Create();
  }

  const SubscribeWindow& window() const { return window_; }

  MoqtPriority subscriber_priority() const { return subscriber_priority_; }
  void set_subscriber_priority(MoqtPriority priority) {
    subscriber_priority_ = priority;
  }

  virtual bool is_fetch() const = 0;

 protected:
  SubscribeWindow& window_mutable() { return window_; };

 private:
  const FullTrackName full_track_name_;
  const uint64_t request_id_;
  MoqtPriority subscriber_priority_;
  SubscribeWindow window_;
  // If false, an object or OK message has been received, so any ERROR message
  // is a protocol violation.
  bool error_is_allowed_ = true;

  // Must be last.
  quiche::QuicheWeakPtrFactory<RemoteTrack> weak_ptr_factory_;
};

// A track on the peer to which the session has subscribed.
class SubscribeRemoteTrack : public RemoteTrack {
 public:
  // TODO: Separate this out (as it's used by the application) and give it a
  // name like MoqtTrackSubscriber,
  class Visitor {
   public:
    virtual ~Visitor() = default;
    // Called when the session receives a response to the SUBSCRIBE, unless it's
    // a SUBSCRIBE_ERROR with a new track_alias. In that case, the session will
    // automatically retry.
    virtual void OnReply(
        const FullTrackName& full_track_name,
        std::optional<Location> largest_location,
        std::optional<absl::string_view> error_reason_phrase) = 0;
    // Called when the subscription process is far enough that it is possible to
    // send OBJECT_ACK messages; provides a callback to do so. The callback is
    // valid for as long as the session is valid.
    virtual void OnCanAckObjects(MoqtObjectAckFunction ack_function) = 0;
    // Called when an object fragment (or an entire object) is received.
    virtual void OnObjectFragment(const FullTrackName& full_track_name,
                                  const PublishedObjectMetadata& metadata,
                                  absl::string_view object,
                                  bool end_of_message) = 0;
    virtual void OnSubscribeDone(FullTrackName full_track_name) = 0;
    // Called when the track is malformed per Section 2.5 of
    // draft-ietf-moqt-moq-transport-12. If the application is a relay, it MUST
    // terminate downstream delivery of the track.
    virtual void OnMalformedTrack(const FullTrackName& full_track_name) = 0;
  };
  SubscribeRemoteTrack(const MoqtSubscribe& subscribe, Visitor* visitor)
      : RemoteTrack(subscribe.full_track_name, subscribe.request_id,
                    SubscribeWindow(subscribe.start.value_or(Location()),
                                    subscribe.end_group),
                    subscribe.subscriber_priority),
        forward_(subscribe.forward),
        visitor_(visitor),
        delivery_timeout_(subscribe.parameters.delivery_timeout) {}
  ~SubscribeRemoteTrack() override {
    if (subscribe_done_alarm_ != nullptr) {
      subscribe_done_alarm_->PermanentCancel();
    }
  }

  void OnObjectOrOk() override {
    RemoteTrack::OnObjectOrOk();
  }
  std::optional<uint64_t> track_alias() const { return track_alias_; }
  void set_track_alias(uint64_t track_alias) {
    track_alias_.emplace(track_alias);
  }
  Visitor* visitor() { return visitor_; }

  // Returns false if the forwarding preference is changing on the track.
  bool OnObject(bool is_datagram) {
    OnObjectOrOk();
    if (!is_datagram_.has_value()) {
      is_datagram_ = is_datagram;
      return true;
    }
    return (is_datagram_ == is_datagram);
  }
  // Called on SUBSCRIBE_OK or SUBSCRIBE_UPDATE.
  bool TruncateStart(Location start) {
    return window_mutable().TruncateStart(start);
  }
  // Called on SUBSCRIBE_UPDATE.
  bool TruncateEnd(uint64_t end_group) {
    return window_mutable().TruncateEnd(end_group);
  }
  void OnStreamOpened();
  void OnStreamClosed();
  void OnSubscribeDone(uint64_t stream_count, const quic::QuicClock* clock,
                       std::unique_ptr<quic::QuicAlarm> subscribe_done_alarm);
  bool all_streams_closed() const {
    return total_streams_.has_value() && *total_streams_ == streams_closed_;
  }

  // The application can request a Joining FETCH but also for FETCH objects to
  // be delivered via SubscribeRemoteTrack::Visitor::OnObjectFragment(). When
  // this occurs, the session passes the FetchTask here to handle incoming
  // FETCH objects to pipe directly into the visitor.
  void OnJoiningFetchReady(std::unique_ptr<MoqtFetchTask> fetch_task);

  bool forward() const { return forward_; }
  void set_forward(bool forward) { forward_ = forward; }

  bool is_fetch() const override { return false; }

 private:
  friend class test::MoqtSessionPeer;
  friend class test::SubscribeRemoteTrackPeer;

  void MaybeSetSubscribeDoneAlarm();

  void FetchObjects();
  std::unique_ptr<MoqtFetchTask> fetch_task_;

  std::optional<const uint64_t> track_alias_;
  bool forward_;
  Visitor* visitor_;
  std::optional<bool> is_datagram_;
  int currently_open_streams_ = 0;
  // Every stream that has received FIN or RESET_STREAM.
  uint64_t streams_closed_ = 0;
  // Value assigned on SUBSCRIBE_DONE. Can destroy subscription state if
  // streams_closed_ == total_streams_.
  std::optional<uint64_t> total_streams_;
  // Timer to clean up the track if there are no open streams.
  quic::QuicTimeDelta delivery_timeout_ = quic::QuicTimeDelta::Infinite();
  std::unique_ptr<quic::QuicAlarm> subscribe_done_alarm_ = nullptr;
  const quic::QuicClock* clock_ = nullptr;
};

// MoqtSession calls this when a FETCH_OK or FETCH_ERROR is received. The
// destination of the callback owns |fetch_task| and MoqtSession will react
// safely if the owner destroys it.
using FetchResponseCallback =
    quiche::SingleUseCallback<void(std::unique_ptr<MoqtFetchTask> fetch_task)>;

// This is a callback to MoqtSession::IncomingDataStream. Called when the
// FetchTask has its object cache empty, on creation, and whenever the
// application reads it.
using CanReadCallback = quiche::MultiUseCallback<void()>;

// If the application destroys the FetchTask, this is a signal to MoqtSession to
// cancel the FETCH and STOP_SENDING the stream.
using TaskDestroyedCallback = quiche::SingleUseCallback<void()>;

// Class for upstream FETCH. It will notify the application using |callback|
// when a FETCH_OK or FETCH_ERROR is received.
class UpstreamFetch : public RemoteTrack {
 public:
  // Standalone Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, const StandaloneFetch standalone,
                FetchResponseCallback callback)
      : RemoteTrack(
            standalone.full_track_name, fetch.request_id,
            SubscribeWindow(standalone.start_object, standalone.end_group,
                            standalone.end_object),
            fetch.subscriber_priority),
        ok_callback_(std::move(callback)) {}
  // Relative Joining Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, FullTrackName full_track_name,
                FetchResponseCallback callback)
      : RemoteTrack(full_track_name, fetch.request_id,
                    SubscribeWindow(Location(0, 0)), fetch.subscriber_priority),
        ok_callback_(std::move(callback)) {}
  // Absolute Joining Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, FullTrackName full_track_name,
                JoiningFetchAbsolute absolute_joining,
                FetchResponseCallback callback)
      : RemoteTrack(
            full_track_name, fetch.request_id,
            SubscribeWindow(Location(absolute_joining.joining_start, 0)),
            fetch.subscriber_priority),
        ok_callback_(std::move(callback)) {}
  UpstreamFetch(const UpstreamFetch&) = delete;
  ~UpstreamFetch();

  class UpstreamFetchTask : public MoqtFetchTask {
   public:
    // If the UpstreamFetch is destroyed, it will call OnStreamAndFetchClosed
    // which sets the TaskDestroyedCallback to nullptr. Thus, |callback| can
    // assume that UpstreamFetch is valid.
    UpstreamFetchTask(Location largest_location, absl::Status status,
                      TaskDestroyedCallback callback)
        : largest_location_(largest_location),
          status_(status),
          task_destroyed_callback_(std::move(callback)),
          weak_ptr_factory_(this) {}
    ~UpstreamFetchTask() override;

    // Implementation of MoqtFetchTask.
    GetNextObjectResult GetNextObject(PublishedObject& output) override;
    void SetObjectAvailableCallback(
        ObjectsAvailableCallback callback) override {
      object_available_callback_ = std::move(callback);
    };
    // TODO(martinduke): Implement the new API, but for now, only deliver the
    // FetchTask on FETCH_OK.
    void SetFetchResponseCallback(FetchResponseCallback callback) override {}
    absl::Status GetStatus() override { return status_; };

    quiche::QuicheWeakPtr<UpstreamFetchTask> weak_ptr() {
      return weak_ptr_factory_.Create();
    }

    // MoqtSession should not use this function; use
    // UpstreamFetch::OnStreamOpened() instead, in case the task does not exist
    // yet.
    void set_can_read_callback(CanReadCallback callback) {
      can_read_callback_ = std::move(callback);
      can_read_callback_();  // Accept the first object.
    }

    // Called when the data stream receives a new object.
    void NewObject(const MoqtObject& message);
    void AppendPayloadToObject(absl::string_view payload);
    // MoqtSession calls this for a hint if the object has been read.
    bool HasObject() const { return next_object_.has_value(); }
    bool NeedsMorePayload() const {
      return next_object_.has_value() && next_object_->payload_length > 0;
    }
    // MoqtSession calls NotifyNewObject() after NewObject() because it has to
    // exit the parser loop before the callback possibly causes another read.
    // Furthermore, NewObject() may be a partial object, and so
    // NotifyNewObject() is called only when the object is complete.
    void NotifyNewObject();

    // Deletes callbacks to session or stream, updates the status. If |error|
    // has no value, will append an EOF to the object stream.
    void OnStreamAndFetchClosed(
        std::optional<webtransport::StreamErrorCode> error,
        absl::string_view reason_phrase);

   private:
    Location largest_location_;
    absl::Status status_ = absl::OkStatus();
    TaskDestroyedCallback task_destroyed_callback_;

    // Object delivery state. The payload_length member is used to track the
    // payload bytes not yet received. The application receives a
    // PublishedObject that is constructed from next_object_ and payload_.
    std::optional<MoqtObject> next_object_;
    // Store payload separately. Will be converted into QuicheMemSlice only when
    // complete, since QuicheMemSlice is immutable.
    quiche::QuicheBuffer payload_;

    // The task should only call object_available_callback_ when the last result
    // was kPending. Otherwise, there can be recursive loops of
    // GetNextObjectResult().
    bool need_object_available_callback_ = true;
    bool eof_ = false;  // The next object is EOF.
    // The Fetch task signals the application when it has new objects.
    ObjectsAvailableCallback object_available_callback_;
    // The Fetch task signals the stream when it has dispensed of an object.
    CanReadCallback can_read_callback_;

    // Must be last.
    quiche::QuicheWeakPtrFactory<UpstreamFetchTask> weak_ptr_factory_;
  };

  // Arrival of FETCH_OK/FETCH_ERROR.
  void OnFetchResult(Location largest_location, MoqtDeliveryOrder group_order,
                     absl::Status status, TaskDestroyedCallback callback);

  UpstreamFetchTask* task() { return task_.GetIfAvailable(); }

  // Manage the relationship with the data stream.
  void OnStreamOpened(CanReadCallback callback);

  bool is_fetch() const override { return true; }

  // Validate that the track is not malformed due to a location violating group
  // order or Object ID order.
  bool LocationIsValid(Location location, MoqtObjectStatus status,
                       bool end_of_message);

 private:
  std::optional<MoqtDeliveryOrder> group_order_;  // nullopt if not yet known.
  std::optional<Location> last_location_;
  bool last_group_is_finished_ = false;  // Received EndOfGroup.
  bool no_more_objects_ = false;         // Received EndOfTrack

  quiche::QuicheWeakPtr<UpstreamFetchTask> task_;

  // Before FetchTask is created, an incoming stream will register the callback
  // here instead.
  CanReadCallback can_read_callback_;

  // Initial values from Fetch() call.
  FetchResponseCallback ok_callback_;  // Will be destroyed on FETCH_OK.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
