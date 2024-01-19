// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_
#define QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "quiche/common/capsule.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

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

 private:
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
};

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_ENCAPSULATED_ENCAPSULATED_WEB_TRANSPORT_H_
