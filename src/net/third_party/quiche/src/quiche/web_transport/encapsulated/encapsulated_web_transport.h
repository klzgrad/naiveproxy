// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_
#define QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/capsule.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"
#include "quiche/web_transport/web_transport_priority_scheduler.h"

namespace webtransport {

constexpr bool IsUnidirectionalId(StreamId id) { return id & 0b10; }
constexpr bool IsBidirectionalId(StreamId id) {
  return !IsUnidirectionalId(id);
}
constexpr bool IsIdOpenedBy(StreamId id, Perspective perspective) {
  return (id & 0b01) ^ (perspective == Perspective::kClient);
}

using FatalErrorCallback = quiche::SingleUseCallback<void(absl::string_view)>;

// Implementation of the WebTransport over HTTP/2 protocol; works over any
// arbitrary bidirectional bytestream that can be prefixed with HTTP headers.
// Specification: https://datatracker.ietf.org/doc/draft-ietf-webtrans-http2/
class QUICHE_EXPORT EncapsulatedSession
    : public webtransport::Session,
      public quiche::WriteStreamVisitor,
      public quiche::ReadStreamVisitor,
      public quiche::CapsuleParser::Visitor {
 public:
  // The state machine of the transport.
  enum State {
    // The transport object has been created, but
    // InitializeClient/InitializeServer has not been called yet.
    kUninitialized,
    // The client has sent its own headers, but haven't received a response yet.
    kWaitingForHeaders,
    // Both the client and the server headers have been processed.
    kSessionOpen,
    // The session close has been requested, but the CLOSE capsule hasn't been
    // sent yet.
    kSessionClosing,
    // The session has been closed; no further data will be exchanged.
    kSessionClosed,
  };

  // The `fatal_error_callback` implies that any state related to the session
  // should be torn down after it's been called.
  EncapsulatedSession(Perspective perspective,
                      FatalErrorCallback fatal_error_callback);

  // WebTransport uses HTTP headers in a similar way to how QUIC uses SETTINGS;
  // thus, the headers are necessary to initialize the session.
  void InitializeClient(std::unique_ptr<SessionVisitor> visitor,
                        quiche::HttpHeaderBlock& outgoing_headers,
                        quiche::WriteStream* writer,
                        quiche::ReadStream* reader);
  void InitializeServer(std::unique_ptr<SessionVisitor> visitor,
                        const quiche::HttpHeaderBlock& incoming_headers,
                        quiche::HttpHeaderBlock& outgoing_headers,
                        quiche::WriteStream* writer,
                        quiche::ReadStream* reader);
  void ProcessIncomingServerHeaders(const quiche::HttpHeaderBlock& headers);

  // webtransport::Session implementation.
  void CloseSession(SessionErrorCode error_code,
                    absl::string_view error_message) override;
  Stream* AcceptIncomingBidirectionalStream() override;
  Stream* AcceptIncomingUnidirectionalStream() override;
  bool CanOpenNextOutgoingBidirectionalStream() override;
  bool CanOpenNextOutgoingUnidirectionalStream() override;
  Stream* OpenOutgoingBidirectionalStream() override;
  Stream* OpenOutgoingUnidirectionalStream() override;
  DatagramStatus SendOrQueueDatagram(absl::string_view datagram) override;
  uint64_t GetMaxDatagramSize() const override;
  void SetDatagramMaxTimeInQueue(absl::Duration max_time_in_queue) override;
  Stream* GetStreamById(StreamId id) override;
  DatagramStats GetDatagramStats() override;
  SessionStats GetSessionStats() override;
  void NotifySessionDraining() override;
  void SetOnDraining(quiche::SingleUseCallback<void()> callback) override;

  // quiche::WriteStreamVisitor implementation.
  void OnCanWrite() override;
  // quiche::ReadStreamVisitor implementation.
  void OnCanRead() override;
  // quiche::CapsuleParser::Visitor implementation.
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

  State state() const { return state_; }

  // Cleans up the state for all of the streams that have been closed.  QUIC
  // uses timers to safely delete closed streams while minimizing the risk that
  // something on stack holds an active pointer to them; WebTransport over
  // HTTP/2 does not have any timers in it, making that approach inapplicable
  // here. This class does automatically run garbage collection at the end of
  // every OnCanRead() call (since it's a top-level entrypoint that is likely to
  // come directly from I/O handler), but if the application does not happen to
  // read data frequently, manual calls to this function may be requried.
  void GarbageCollectStreams();

 private:
  // If the amount of data buffered in the socket exceeds the amount specified
  // here, CanWrite() will start returning false.
  static constexpr size_t kDefaultMaxBufferedStreamData = 16 * 1024;

  class InnerStream : public Stream {
   public:
    InnerStream(EncapsulatedSession* session, StreamId id);
    InnerStream(const InnerStream&) = delete;
    InnerStream(InnerStream&&) = delete;
    InnerStream& operator=(const InnerStream&) = delete;
    InnerStream& operator=(InnerStream&&) = delete;

    // ReadStream implementation.
    ABSL_MUST_USE_RESULT ReadResult Read(absl::Span<char> output) override;
    ABSL_MUST_USE_RESULT ReadResult Read(std::string* output) override;
    size_t ReadableBytes() const override;
    PeekResult PeekNextReadableRegion() const override;
    bool SkipBytes(size_t bytes) override;

    // WriteStream implementation.
    absl::Status Writev(absl::Span<const absl::string_view> data,
                        const quiche::StreamWriteOptions& options) override;
    bool CanWrite() const override;

    // TerminableStream implementation.
    void AbruptlyTerminate(absl::Status error) override;

    // Stream implementation.
    StreamId GetStreamId() const override { return id_; }
    StreamVisitor* visitor() override { return visitor_.get(); }
    void SetVisitor(std::unique_ptr<StreamVisitor> visitor) override {
      visitor_ = std::move(visitor);
    }

    void ResetWithUserCode(StreamErrorCode error) override;
    void SendStopSending(StreamErrorCode error) override;

    void ResetDueToInternalError() override { ResetWithUserCode(0); }
    void MaybeResetDueToStreamObjectGone() override { ResetWithUserCode(0); }

    void CloseReadSide(std::optional<StreamErrorCode> error);
    void CloseWriteSide(std::optional<StreamErrorCode> error);
    bool CanBeGarbageCollected() const {
      return read_side_closed_ && write_side_closed_;
    }

    bool HasPendingWrite() const { return !pending_write_.empty(); }
    void FlushPendingWrite();

    void ProcessCapsule(const quiche::Capsule& capsule);

   private:
    // Struct for storing data that can potentially either stored inside the
    // object or inside some other object on the stack. Here is roughly how this
    // works:
    //   1. A read is enqueued with `data` pointing to a temporary buffer, and
    //      `storage` being empty.
    //   2. Visitor::OnCanRead() is called, potentially causing the user to
    //      consume the data from the temporary buffer directly.
    //   3. If user does not consume data immediately, it's copied to `storage`
    //      (and the pointer to `data` is updated) so that it can be read later.
    struct IncomingRead {
      absl::string_view data;
      std::string storage;

      size_t size() const { return data.size(); }
    };

    // Tries to send `data`; may send less if limited by flow control.
    [[nodiscard]] size_t WriteInner(absl::Span<const absl::string_view> data,
                                    bool fin);

    EncapsulatedSession* session_;
    StreamId id_;
    std::unique_ptr<StreamVisitor> visitor_;
    quiche::QuicheCircularDeque<IncomingRead> incoming_reads_;
    std::string pending_write_;
    bool read_side_closed_;
    bool write_side_closed_;
    bool reset_frame_sent_ = false;
    bool stop_sending_sent_ = false;
    bool fin_received_ = false;
    bool fin_consumed_ = false;
    bool fin_buffered_ = false;
  };

  struct BufferedClose {
    SessionErrorCode error_code = 0;
    std::string error_message;
  };

  Perspective perspective_;
  State state_ = kUninitialized;
  std::unique_ptr<SessionVisitor> visitor_ = nullptr;
  FatalErrorCallback fatal_error_callback_;
  quiche::SingleUseCallback<void()> draining_callback_;
  quiche::WriteStream* writer_ = nullptr;  // Not owned.
  quiche::ReadStream* reader_ = nullptr;   // Not owned.
  quiche::QuicheBufferAllocator* allocator_ =
      quiche::SimpleBufferAllocator::Get();
  quiche::CapsuleParser capsule_parser_;

  size_t max_stream_data_buffered_ = kDefaultMaxBufferedStreamData;

  PriorityScheduler scheduler_;
  absl::node_hash_map<StreamId, InnerStream>
      streams_;  // Streams unregister themselves with scheduler on deletion,
                 // and thus have to be above it.
  quiche::QuicheCircularDeque<StreamId> incoming_bidirectional_streams_;
  quiche::QuicheCircularDeque<StreamId> incoming_unidirectional_streams_;
  std::vector<StreamId> streams_to_garbage_collect_;
  StreamId next_outgoing_bidi_stream_;
  StreamId next_outgoing_unidi_stream_;

  bool session_close_notified_ = false;
  bool fin_sent_ = false;

  BufferedClose buffered_session_close_;
  quiche::QuicheCircularDeque<quiche::QuicheBuffer> control_capsule_queue_;

  void OpenSession();
  absl::Status SendFin(absl::string_view data);
  void OnSessionClosed(SessionErrorCode error_code,
                       const std::string& error_message);
  void OnFatalError(absl::string_view error_message);
  void OnWriteError(absl::Status error);

  bool IsOutgoing(StreamId id) { return IsIdOpenedBy(id, perspective_); }
  bool IsIncoming(StreamId id) { return !IsOutgoing(id); }

  template <typename CapsuleType>
  void SendControlCapsule(CapsuleType capsule) {
    control_capsule_queue_.push_back(quiche::SerializeCapsule(
        quiche::Capsule(std::move(capsule)), allocator_));
    OnCanWrite();
  }

  Stream* AcceptIncomingStream(quiche::QuicheCircularDeque<StreamId>& queue);
  Stream* OpenOutgoingStream(StreamId& counter);
  void ProcessStreamCapsule(const quiche::Capsule& capsule, StreamId stream_id);
};

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_
