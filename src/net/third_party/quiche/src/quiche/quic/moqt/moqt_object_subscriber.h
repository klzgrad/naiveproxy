// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(martinduke): Rename this file to moqt_object_subscriber.h

#ifndef QUICHE_QUIC_MOQT_MOQT_TRACK_H_
#define QUICHE_QUIC_MOQT_MOQT_TRACK_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtSessionPeer;
class LiveSubscriberPeer;
}  // namespace test

// State common to both SUBSCRIBE and FETCH upstream.
class ObjectSubscriber {
 public:
  ObjectSubscriber(const FullTrackName& full_track_name, uint64_t id,
                   const MessageParameters& parameters,
                   MoqtBidiStreamBase* request_stream)
      : full_track_name_(full_track_name),
        request_id_(id),
        request_stream_(request_stream),
        parameters_(parameters),
        weak_ptr_factory_(this) {}
  virtual ~ObjectSubscriber() {}

  const FullTrackName& full_track_name() const { return full_track_name_; }
  // If REQUEST_ERROR arrives after OK or an object, it is a protocol violation.
  virtual void OnObjectOrOk() { error_is_allowed_ = false; }
  bool ErrorIsAllowed() const { return error_is_allowed_; }

  uint64_t request_id() const { return request_id_; }

  // Is the object one that was requested?
  virtual bool InWindow(Location sequence) const = 0;

  quiche::QuicheWeakPtr<ObjectSubscriber> weak_ptr() {
    return weak_ptr_factory_.Create();
  }

  virtual bool is_fetch() const = 0;

  // A REQUEST_UPDATE changes any field that is present in |parameters|.
  void Update(const MessageParameters& parameters) {
    parameters_.Update(parameters);
  }

  MoqtBidiStreamBase* request_stream() { return request_stream_; }

  const MessageParameters& const_parameters() const { return parameters_; }

 protected:
  MessageParameters& parameters() { return parameters_; }

 private:
  const FullTrackName full_track_name_;
  const uint64_t request_id_;
  MoqtBidiStreamBase* request_stream_;
  MessageParameters parameters_;
  // If false, an object or OK message has been received, so any ERROR message
  // is a protocol violation.
  bool error_is_allowed_ = true;

  // Must be last.
  quiche::QuicheWeakPtrFactory<ObjectSubscriber> weak_ptr_factory_;
};

// A track on the peer to which the session has subscribed.
class LiveSubscriber : public ObjectSubscriber {
 public:
  // Returns the existing subscription, if present.
  using AddCallback = quiche::SingleUseCallback<bool(LiveSubscriber*)>;
  using RemoveCallback = quiche::SingleUseCallback<void(LiveSubscriber*)>;
  LiveSubscriber(const MoqtSubscribe& subscribe, SubscribeVisitor* visitor,
                 MoqtBidiStreamBase* request_stream)
      : ObjectSubscriber(subscribe.full_track_name, subscribe.request_id,
                         subscribe.parameters, request_stream),
        visitor_(visitor) {}
  LiveSubscriber(const MoqtPublish& publish, SubscribeVisitor* visitor,
                 MoqtBidiStreamBase* request_stream)
      : ObjectSubscriber(publish.full_track_name, publish.request_id,
                         publish.parameters, request_stream),
        visitor_(visitor) {
    track_alias_.emplace(publish.track_alias);
  }
  ~LiveSubscriber() override;

  void OnObjectOrOk(const SubscribeOkData& data);
  void OnObjectOrOk() override { ObjectSubscriber::OnObjectOrOk(); }
  std::optional<uint64_t> track_alias() const { return track_alias_; }
  // Returns false if the callback returns false, meaning the session has been
  // destroyed.
  void set_track_alias(uint64_t track_alias) {
    track_alias_.emplace(track_alias);
  }
  void OnStreamOpened();
  void OnStreamClosed(bool fin_received, std::optional<DataStreamIndex> index);
  void OnPublishDone(uint64_t stream_count, const quic::QuicClock* clock,
                     quic::QuicAlarmFactory* alarm_factory);

  // The application can request a Joining FETCH but also for FETCH objects to
  // be delivered via LiveSubscriber::Visitor::OnObjectFragment(). When
  // this occurs, the session passes the FetchTask here to handle incoming
  // FETCH objects to pipe directly into the visitor.
  void OnJoiningFetchReady(std::unique_ptr<MoqtFetchTask> fetch_task);

  bool is_fetch() const override { return false; }

  bool InWindow(Location location) const override {
    return const_parameters().forward() &&
           (!const_parameters().subscription_filter.has_value() ||
            const_parameters().subscription_filter->InWindow(location));
  }

  MoqtPriority default_publisher_priority() const {
    return default_publisher_priority_;
  }

  quic::QuicTimeDelta publisher_delivery_timeout() const {
    return publisher_delivery_timeout_;
  }

  SubscribeVisitor* visitor() const { return visitor_; }
  SubscribeVisitor* ReleaseVisitor() {
    SubscribeVisitor* temp = visitor_;
    visitor_ = nullptr;
    return temp;
  }
  void set_visitor(SubscribeVisitor* visitor) { visitor_ = visitor; }

  void SendObjectAck(uint64_t group_id, uint64_t object_id,
                     quic::QuicTimeDelta delta_from_deadline);

 private:
  friend class test::MoqtSessionPeer;
  friend class test::LiveSubscriberPeer;

  class PublishDoneDelegate : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    PublishDoneDelegate(quiche::QuicheWeakPtr<ObjectSubscriber> subscribe)
        : subscribe_(subscribe) {}

    void OnAlarm() override {
      ObjectSubscriber* subscribe = subscribe_.GetIfAvailable();
      if (subscribe == nullptr) {
        return;
      }
      subscribe->request_stream()->Reset(kResetCodeCancelled);
    }

   private:
    quiche::QuicheWeakPtr<ObjectSubscriber> subscribe_;
  };

  void MaybeSetPublishDoneAlarm();
  bool all_streams_closed() const {
    return total_streams_.has_value() && *total_streams_ == streams_closed_;
  }

  quic::QuicTimeDelta publisher_delivery_timeout_ = kDefaultDeliveryTimeout;
  MoqtPriority default_publisher_priority_ = kDefaultPublisherPriority;
  bool dynamic_groups_ = kDefaultDynamicGroups;
  void FetchObjects();
  std::unique_ptr<MoqtFetchTask> fetch_task_;
  // If nonzero, fetch_task_ is in mid-object.
  uint64_t fetch_object_offset_ = 0;

  std::optional<const uint64_t> track_alias_;
  SubscribeVisitor* visitor_;
  int currently_open_streams_ = 0;
  // Every stream that has received FIN or RESET_STREAM.
  uint64_t streams_closed_ = 0;
  // Value assigned on PUBLISH_DONE. Can destroy subscription state if
  // streams_closed_ == total_streams_.
  std::optional<uint64_t> total_streams_;
  std::unique_ptr<quic::QuicAlarm> publish_done_alarm_ = nullptr;
  const quic::QuicClock* clock_ = nullptr;
};

// This is a callback to MoqtSession::IncomingDataStream. Called when the
// FetchTask has its object cache empty, on creation, and whenever the
// application reads it.
using CanReadCallback = quiche::MultiUseCallback<void()>;

// If the application destroys the FetchTask, this is a signal to MoqtSession to
// cancel the FETCH and STOP_SENDING the stream.
using TaskDestroyedCallback = quiche::SingleUseCallback<void()>;

// Class for upstream FETCH. It will notify the application using |callback|
// when a FETCH_OK or REQUEST_ERROR is received.
using RemoveFetchCallback = quiche::SingleUseCallback<void()>;
class UpstreamFetch : public ObjectSubscriber {
 public:
  // Standalone Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, const StandaloneFetch standalone,
                FetchResponseCallback callback,
                RemoveFetchCallback delete_callback)
      : ObjectSubscriber(standalone.full_track_name, fetch.request_id,
                         fetch.parameters, /*request_stream=*/nullptr),
        group_order_(fetch.parameters.group_order.value_or(
            MoqtDeliveryOrder::kAscending)),
        start_(standalone.start_location),
        end_(standalone.end_location),
        subscriber_priority_(fetch.parameters.subscriber_priority.value_or(
            kDefaultSubscriberPriority)),
        ok_callback_(std::move(callback)),
        remove_callback_(std::move(delete_callback)) {}
  // Relative Joining Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, FullTrackName full_track_name,
                FetchResponseCallback callback,
                RemoveFetchCallback delete_callback)
      : ObjectSubscriber(full_track_name, fetch.request_id, fetch.parameters,
                         /*request_stream=*/nullptr),
        group_order_(fetch.parameters.group_order.value_or(
            MoqtDeliveryOrder::kAscending)),
        relative_groups_(
            std::get<JoiningFetchRelative>(fetch.fetch).joining_start),
        subscriber_priority_(fetch.parameters.subscriber_priority.value_or(
            kDefaultSubscriberPriority)),
        ok_callback_(std::move(callback)),
        remove_callback_(std::move(delete_callback)) {}
  // Absolute Joining Fetch constructor
  UpstreamFetch(const MoqtFetch& fetch, FullTrackName full_track_name,
                JoiningFetchAbsolute absolute_joining,
                FetchResponseCallback callback,
                RemoveFetchCallback delete_callback)
      : ObjectSubscriber(full_track_name, fetch.request_id, fetch.parameters,
                         /*request_stream=*/nullptr),
        group_order_(fetch.parameters.group_order.value_or(
            MoqtDeliveryOrder::kAscending)),
        start_(Location(absolute_joining.joining_start, 0)),
        subscriber_priority_(fetch.parameters.subscriber_priority.value_or(
            kDefaultSubscriberPriority)),
        ok_callback_(std::move(callback)),
        remove_callback_(std::move(delete_callback)) {}
  UpstreamFetch(const UpstreamFetch&) = delete;
  ~UpstreamFetch();

  bool InWindow(Location location) const override {
    return (location >= start_ && location <= end_);
  }

  // Called when the data stream is destroyed.
  void OnStreamClosed() { Destroy(); }

  void Destroy() {
    if (remove_callback_) {
      RemoveFetchCallback callback = std::move(remove_callback_);
      remove_callback_ = nullptr;
      std::move(callback)();
    }
  }

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
      return next_object_.has_value() &&
             payload_length_ < next_object_->payload_length;
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

    uint64_t payload_offset() const { return payload_offset_; }
    uint64_t payload_length() const { return payload_length_; }

   private:
    Location largest_location_;
    absl::Status status_ = absl::OkStatus();
    TaskDestroyedCallback task_destroyed_callback_;

    // Object delivery state. The payload_length member is used to track the
    // payload bytes not yet received. The application receives a
    // PublishedObject that is constructed from next_object_ and payload_.
    std::optional<MoqtObject> next_object_;
    quiche::QuicheCircularDeque<quiche::QuicheMemSlice> payload_;
    // The starting point of payload_. Data is deleted as it is delivered.
    uint64_t payload_offset_ = 0;
    // Total data delivered for this object.
    uint64_t payload_length_ = 0;

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

  // Arrival of FETCH_OK/REQUEST_ERROR.
  void OnFetchResult(Location largest_location, absl::Status status,
                     TaskDestroyedCallback callback);

  UpstreamFetchTask* task() { return task_.GetIfAvailable(); }

  // Manage the relationship with the data stream.
  void OnStreamOpened(CanReadCallback callback);

  bool is_fetch() const override { return true; }

 private:
  MoqtDeliveryOrder group_order_;
  Location start_ = Location(0, 0);
  Location end_ = Location(kMaxGroupId, kMaxObjectId);
  std::optional<uint64_t> relative_groups_;
  MoqtPriority subscriber_priority_;
  // The last object received on the stream.
  std::optional<Location> last_location_;
  // The highest location received on the stream.
  std::optional<Location> highest_location_;
  bool last_group_is_finished_ = false;  // Received EndOfGroup.
  std::optional<Location> end_of_track_;  // Received EndOfTrack

  quiche::QuicheWeakPtr<UpstreamFetchTask> task_;

  // Before FetchTask is created, an incoming stream will register the callback
  // here instead.
  CanReadCallback can_read_callback_;

  // Initial values from Fetch() call.
  FetchResponseCallback ok_callback_;  // Will be destroyed on FETCH_OK.
  RemoveFetchCallback remove_callback_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_TRACK_H_
