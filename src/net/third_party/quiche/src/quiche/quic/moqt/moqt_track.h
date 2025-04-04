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

using MoqtObjectAckFunction =
    quiche::MultiUseCallback<void(uint64_t group_id, uint64_t object_id,
                                  quic::QuicTimeDelta delta_from_deadline)>;

// State common to both SUBSCRIBE and FETCH upstream.
class RemoteTrack {
 public:
  RemoteTrack(const FullTrackName& full_track_name, uint64_t id,
              SubscribeWindow window)
      : full_track_name_(full_track_name),
        subscribe_id_(id),
        window_(window),
        weak_ptr_factory_(this) {}
  virtual ~RemoteTrack() = default;

  FullTrackName full_track_name() const { return full_track_name_; }
  // If FETCH_ERROR or SUBSCRIBE_ERROR arrives after OK or an object, it is a
  // protocol violation.
  virtual void OnObjectOrOk() { error_is_allowed_ = false; }
  bool ErrorIsAllowed() const { return error_is_allowed_; }

  // When called while processing the first object in the track, sets the
  // data stream type to the value indicated by the incoming encoding.
  // Otherwise, returns true if the incoming object does not violate the rule
  // that the type is consistent.
  bool CheckDataStreamType(MoqtDataStreamType type);

  bool is_fetch() const {
    return data_stream_type_.has_value() &&
           *data_stream_type_ == MoqtDataStreamType::kStreamHeaderFetch;
  }

  uint64_t subscribe_id() const { return subscribe_id_; }

  // Is the object one that was requested?
  bool InWindow(FullSequence sequence) const {
    return window_.InWindow(sequence);
  }

  void ChangeWindow(SubscribeWindow& window) { window_ = window; }

  quiche::QuicheWeakPtr<RemoteTrack> weak_ptr() {
    return weak_ptr_factory_.Create();
  }

 private:
  const FullTrackName full_track_name_;
  const uint64_t subscribe_id_;
  SubscribeWindow window_;
  std::optional<MoqtDataStreamType> data_stream_type_;
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
        std::optional<FullSequence> largest_id,
        std::optional<absl::string_view> error_reason_phrase) = 0;
    // Called when the subscription process is far enough that it is possible to
    // send OBJECT_ACK messages; provides a callback to do so. The callback is
    // valid for as long as the session is valid.
    virtual void OnCanAckObjects(MoqtObjectAckFunction ack_function) = 0;
    // Called when an object fragment (or an entire object) is received.
    virtual void OnObjectFragment(
        const FullTrackName& full_track_name, FullSequence sequence,
        MoqtPriority publisher_priority, MoqtObjectStatus object_status,
        absl::string_view object, bool end_of_message) = 0;
    // TODO(martinduke): Add final sequence numbers
  };
  SubscribeRemoteTrack(const MoqtSubscribe& subscribe, Visitor* visitor)
      : RemoteTrack(subscribe.full_track_name, subscribe.subscribe_id,
                    SubscribeWindow(subscribe.start_group.value_or(0),
                                    subscribe.start_object.value_or(0),
                                    subscribe.end_group.value_or(UINT64_MAX),
                                    UINT64_MAX)),
        track_alias_(subscribe.track_alias),
        visitor_(visitor),
        subscribe_(std::make_unique<MoqtSubscribe>(subscribe)) {}

  void OnObjectOrOk() override {
    subscribe_.reset();  // No SUBSCRIBE_ERROR, no need to store this anymore.
    RemoteTrack::OnObjectOrOk();
  }
  uint64_t track_alias() const { return track_alias_; }
  Visitor* visitor() { return visitor_; }
  MoqtSubscribe& GetSubscribe() {
    return *subscribe_;
    // This class will soon be destroyed, so there's no need to null the
    // unique_ptr;
  }
  // Returns false if the forwarding preference is changing on the track.
  bool OnObject(bool is_datagram) {
    OnObjectOrOk();
    if (!is_datagram_.has_value()) {
      is_datagram_ = is_datagram;
      return true;
    } else {
      return (is_datagram_ == is_datagram);
    }
  }

 private:
  const uint64_t track_alias_;
  Visitor* visitor_;
  std::optional<bool> is_datagram_;
  // For convenience, store the subscribe message if it has to be re-sent with
  // a new track alias.
  std::unique_ptr<MoqtSubscribe> subscribe_;
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
  UpstreamFetch(const MoqtFetch& fetch, FetchResponseCallback callback)
      : RemoteTrack(fetch.full_track_name, fetch.subscribe_id,
                    SubscribeWindow(
                        fetch.start_object,
                        FullSequence(fetch.end_group,
                                     fetch.end_object.value_or(UINT64_MAX)))),
        ok_callback_(std::move(callback)) {
    // Immediately set the data stream type.
    CheckDataStreamType(MoqtDataStreamType::kStreamHeaderFetch);
  }
  UpstreamFetch(const UpstreamFetch&) = delete;
  ~UpstreamFetch();

  class UpstreamFetchTask : public MoqtFetchTask {
   public:
    // If the UpstreamFetch is destroyed, it will call OnStreamAndFetchClosed
    // which sets the TaskDestroyedCallback to nullptr. Thus, |callback| can
    // assume that UpstreamFetch is valid.
    UpstreamFetchTask(FullSequence largest_id, absl::Status status,
                      TaskDestroyedCallback callback)
        : largest_id_(largest_id),
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
    absl::Status GetStatus() override { return status_; };
    FullSequence GetLargestId() const override { return largest_id_; }

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
    FullSequence largest_id_;
    absl::Status status_;
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
  void OnFetchResult(FullSequence largest_id, absl::Status status,
                     TaskDestroyedCallback callback);

  UpstreamFetchTask* task() { return task_.GetIfAvailable(); }

  // Manage the relationship with the data stream.
  void OnStreamOpened(CanReadCallback callback);

 private:
  quiche::QuicheWeakPtr<UpstreamFetchTask> task_;

  // Before FetchTask is created, an incoming stream will register the callback
  // here instead.
  CanReadCallback can_read_callback_;

  // Initial values from Fetch() call.
  FetchResponseCallback ok_callback_;  // Will be destroyed on FETCH_OK.
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SUBSCRIPTION_H_
