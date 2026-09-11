// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_UNI_STREAM_H_
#define QUICHE_QUIC_MOQT_MOQT_UNI_STREAM_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_object_subscriber.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class OutgoingSubgroupStreamPeer;
class MoqtSessionPeer;
}

// A base class for locally initiated unidirectional streams, which can serve
// either a Subgroup or a FETCH response. It contains most of the machinery for
// managing the WebTransport stream.
class OutgoingUniStream : public webtransport::StreamVisitor {
 public:
  OutgoingUniStream(MoqtFramer framer,
                    webtransport::Stream* absl_nonnull stream,
                    webtransport::StreamPriority priority,
                    uint64_t track_identifier)
      : stream_(*stream),
        priority_(priority),
        track_identifier_(track_identifier),
        framer_(framer) {
    stream_.SetPriority(priority_);
  }
  virtual ~OutgoingUniStream() = default;

  // webtransport::StreamVisitor implementation.
  void OnCanRead() override {}  // Write-only.
  // OnCanWrite() deferred to children.
  virtual void OnResetStreamReceived(webtransport::StreamErrorCode) override {}
  // OnStopSendingReceived() deferred to children.
  void OnWriteSideInDataRecvdState() override {}

  // Recomputes the send order and updates it for the associated stream.
  void UpdatePriority(MoqtPriority subscriber_priority);

 protected:
  webtransport::Stream& stream() { return stream_; }
  std::optional<PublishedObjectMetadata>& last_object() { return last_object_; }
  void set_last_object(PublishedObjectMetadata metadata) {
    last_object_ = std::move(metadata);
  }

  // Writes an object to the stream. Returns false if the write failed. The
  // caller should reset the stream if that happens.
  bool WriteObjectToStream(PublishedObject& object, MoqtDataStreamType type);

 private:
  webtransport::Stream& stream_;  // Always valid because it owns this object.
  webtransport::StreamPriority priority_;
  uint64_t track_identifier_;  // track alias or fetch request ID.

  MoqtFramer framer_;
  // Used to compute the object ID diff and pass metadata for partial objects.
  // If nullopt, the stream header has not been written yet.
  std::optional<PublishedObjectMetadata> last_object_;
};

// This interface provides information about the subscription.
class LivePublisherInterface {
 public:
  virtual ~LivePublisherInterface() = default;
  virtual bool InWindow(Location) = 0;
  virtual bool alternate_delivery_timeout() = 0;
  virtual const quic::QuicClock* clock() = 0;
  virtual quic::QuicTimeDelta delivery_timeout() = 0;
  virtual quic::QuicAlarmFactory* alarm_factory() = 0;
  // Called when the first byte of an object is written to the stream.
  virtual void OnObjectSent(Location) = 0;
  virtual void OnStreamTimeout(DataStreamIndex) = 0;
  virtual void OnSubgroupAbandoned(uint64_t group, uint64_t subgroup,
                                   webtransport::StreamErrorCode) = 0;
  virtual void OnDataStreamDestroyed(DataStreamIndex) = 0;
};

// This is for subscriptions only. FETCH uses its own construct.
class QUICHE_EXPORT OutgoingSubgroupStream : public OutgoingUniStream {
 public:
  // |visitor| is owned by the subscription, so the WeakPtr also serves as a
  // liveness token.
  OutgoingSubgroupStream(
      MoqtFramer framer, webtransport::Stream* absl_nonnull stream,
      DataStreamIndex index, uint64_t first_object,
      quiche::QuicheWeakPtr<LivePublisherInterface> visitor,
      std::shared_ptr<MoqtTrackPublisher> absl_nonnull track_publisher,
      webtransport::StreamPriority priority, uint64_t track_alias,
      MoqtTraceRecorder* absl_nonnull trace_recorder);
  ~OutgoingSubgroupStream();

  // webtransport::StreamVisitor overrides.
  void OnCanWrite() override;
  void OnStopSendingReceived(webtransport::StreamErrorCode error_code) override;

  class DeliveryTimeoutDelegate
      : public quic::QuicAlarm::DelegateWithoutContext {
   public:
    explicit DeliveryTimeoutDelegate(OutgoingSubgroupStream* stream)
        : stream_(stream) {}
    void OnAlarm() override;

   private:
    OutgoingSubgroupStream* stream_;
  };

  // Sends a pure FIN on the stream, if the last object sent matches
  // |last_object|. Otherwise, does nothing.
  void Fin(Location last_object);
  // Reset can be called directly on the stream, with no need to involve the
  // visitor.

  // Creates and sets an alarm for the given deadline. Does nothing if the
  // alarm is already created.
  void CreateAndSetAlarm(quic::QuicTime deadline);

 private:
  friend class DeliveryTimeoutDelegate;
  friend class test::OutgoingSubgroupStreamPeer;

  // Sends objects on the stream, starting with `next_object_`, until the
  // stream becomes write-blocked or closed. Can reset the stream, destroying
  // the class, on a write error.
  void SendObjects();

  DataStreamIndex index_;
  quiche::QuicheWeakPtr<LivePublisherInterface> visitor_;

  MoqtDataStreamType type_;
  uint64_t track_alias_;
  std::shared_ptr<MoqtTrackPublisher> publisher_;
  // Minimum object ID that should go out next. The session doesn't know the
  // exact ID of the next object in the stream because the next object could
  // be in a different subgroup or simply be skipped.
  uint64_t next_object_;
  // Number of payload bytes from next_object_ that has already been written
  // to the stream.
  uint64_t already_delivered_ = 0;

  // If this data stream is for SUBSCRIBE, reset it if an object has been
  // excessively delayed per Section 7.1.1.2.
  std::unique_ptr<quic::QuicAlarm> delivery_timeout_alarm_;
};

using FetchStreamCloseCallback = quiche::SingleUseCallback<void()>;

class QUICHE_EXPORT OutgoingFetchStream : public OutgoingUniStream {
 public:
  OutgoingFetchStream(MoqtFramer framer,
                      webtransport::Stream* absl_nonnull stream,
                      uint64_t request_id,
                      webtransport::StreamPriority priority,
                      std::unique_ptr<MoqtFetchTask> incoming_objects,
                      FetchStreamCloseCallback close_callback,
                      MoqtTraceRecorder* absl_nonnull trace_recorder);
  ~OutgoingFetchStream();

  // webtransport::StreamVisitor implementation.
  void OnCanWrite() override;
  void OnStopSendingReceived(webtransport::StreamErrorCode error_code) override;

 private:
  std::unique_ptr<MoqtFetchTask> incoming_objects_;
  FetchStreamCloseCallback close_callback_;
};

class SessionToUniStreamInterface {
 public:
  virtual ~SessionToUniStreamInterface() = default;
  virtual bool deliver_partial_objects() const = 0;
  virtual void OnMalformedTrack(ObjectSubscriber* name) = 0;
  virtual quiche::QuicheWeakPtr<ObjectSubscriber> GetSubscribe(
      uint64_t track_alias) = 0;
  virtual quiche::QuicheWeakPtr<ObjectSubscriber> GetFetch(
      uint64_t request_id) = 0;
  virtual void Error(MoqtError error_code, absl::string_view reason) = 0;
};

class QUICHE_EXPORT IncomingDataStream : public webtransport::StreamVisitor,
                                         public MoqtDataParserVisitor {
 public:
  IncomingDataStream(MoqtStreamTypeParser type_parser,
                     SessionToUniStreamInterface* absl_nonnull session,
                     const quic::QuicClock* absl_nonnull clock)
      : stream_(type_parser.stream()),
        parser_(std::move(type_parser), this),
        session_(session),
        clock_(clock) {}
  IncomingDataStream(webtransport::Stream* absl_nonnull stream,
                     SessionToUniStreamInterface* absl_nonnull session,
                     const quic::QuicClock* absl_nonnull clock)
      : IncomingDataStream(MoqtStreamTypeParser(stream), session, clock) {}
  ~IncomingDataStream();

  // webtransport::StreamVisitor implementation.
  void OnCanRead() override;
  void OnCanWrite() override {}
  void OnResetStreamReceived(webtransport::StreamErrorCode) override {}
  void OnStopSendingReceived(webtransport::StreamErrorCode /*error*/) override {
  }
  void OnWriteSideInDataRecvdState() override {}

  // MoqtParserVisitor implementation.
  // TODO: Handle a stream FIN.
  void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                       bool end_of_message) override;
  void OnFin() override { fin_received_ = true; }
  void OnParsingError(MoqtError error_code, absl::string_view reason) override;

  webtransport::Stream* stream() const { return stream_; }

  void MaybeReadOneObject();

 private:
  friend class test::MoqtSessionPeer;
  bool IsFetch() const {
    return parser_.stream_type().has_value() &&
           parser_.stream_type()->IsFetch();
  }

  uint64_t next_object_id_ = 0;
  bool no_more_objects_ = false;  // EndOfGroup or EndOfTrack was received.
  std::optional<DataStreamIndex> index_;  // Only set for subscribe.
  bool fin_received_ = false;
  webtransport::Stream* stream_;
  SubscribeVisitor* visitor_ = nullptr;
  // Once the subscribe ID is identified, set it here.
  quiche::QuicheWeakPtr<ObjectSubscriber> track_;
  MoqtDataParser parser_;
  std::string partial_object_;
  uint64_t bytes_received_this_object_ = 0;
  SessionToUniStreamInterface* session_;
  const quic::QuicClock* absl_nonnull clock_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_UNI_STREAM_H_
