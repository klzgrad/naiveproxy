// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header contains interfaces that abstract away different backing
// protocols for WebTransport.

#ifndef QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_H_
#define QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// The dependencies of this API should be kept minimal and independent of
// specific transport implementations.
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_stream.h"

namespace webtransport {

// A numeric ID uniquely identifying a WebTransport stream. Note that by design,
// those IDs are not available in the Web API, and the IDs do not necessarily
// match between client and server perspective, since there may be a proxy
// between them.
using StreamId = uint32_t;
// Application-specific error code used for resetting either the read or the
// write half of the stream.
using StreamErrorCode = uint32_t;
// Application-specific error code used for closing a WebTransport session.
using SessionErrorCode = uint32_t;

// An outcome of a datagram send call.
enum class DatagramStatusCode {
  // Datagram has been successfully sent or placed into the datagram queue.
  kSuccess,
  // Datagram has not been sent since the underlying QUIC connection is blocked
  // by the congestion control.  Note that this can only happen if the queue is
  // full.
  kBlocked,
  // Datagram has not been sent since it is too large to fit into a single
  // UDP packet.
  kTooBig,
  // An unspecified internal error.
  kInternalError,
};

// An outcome of a datagram send call, in both enum and human-readable form.
struct QUICHE_EXPORT DatagramStatus {
  explicit DatagramStatus(DatagramStatusCode code, std::string error_message)
      : code(code), error_message(std::move(error_message)) {}

  DatagramStatusCode code;
  std::string error_message;
};

enum class StreamType {
  kUnidirectional,
  kBidirectional,
};

// Based on
// https://w3c.github.io/webtransport/#dictdef-webtransportdatagramstats.
struct QUICHE_EXPORT DatagramStats {
  uint64_t expired_outgoing;
  uint64_t lost_outgoing;

  // droppedIncoming is not present, since in the C++ API, we immediately
  // deliver datagrams via callback, meaning there is no queue where things
  // would be dropped.
};

// Based on https://w3c.github.io/webtransport/#web-transport-stats
// Note that this is currently not a complete implementation of that API, as
// some of those still need to be clarified in
// https://github.com/w3c/webtransport/issues/537
struct QUICHE_EXPORT SessionStats {
  absl::Duration min_rtt;
  absl::Duration smoothed_rtt;
  absl::Duration rtt_variation;

  uint64_t estimated_send_rate_bps;  // In bits per second.

  DatagramStats datagram_stats;
};

// The stream visitor is an application-provided object that gets notified about
// events related to a WebTransport stream.  The visitor object is owned by the
// stream itself, meaning that if the stream is ever fully closed, the visitor
// will be garbage-collected.
class QUICHE_EXPORT StreamVisitor : public quiche::ReadStreamVisitor,
                                    public quiche::WriteStreamVisitor {
 public:
  virtual ~StreamVisitor() {}

  // Called when RESET_STREAM is received for the stream.
  virtual void OnResetStreamReceived(StreamErrorCode error) = 0;
  // Called when STOP_SENDING is received for the stream.
  virtual void OnStopSendingReceived(StreamErrorCode error) = 0;
  // Called when the write side of the stream is closed and all of the data sent
  // has been acknowledged ("Data Recvd" state of RFC 9000).  Primarily used by
  // the state machine of the Web API.
  virtual void OnWriteSideInDataRecvdState() = 0;
};

// A stream (either bidirectional or unidirectional) that is contained within a
// WebTransport session.
class QUICHE_EXPORT Stream : public quiche::ReadStream,
                             public quiche::WriteStream,
                             public quiche::TerminableStream {
 public:
  virtual ~Stream() {}

  // An ID that is unique within the session.  Those are not exposed to the user
  // via the web API, but can be used internally for bookkeeping and
  // diagnostics.
  virtual StreamId GetStreamId() const = 0;

  // Resets the read or the write side of the stream with the specified error
  // code.
  virtual void ResetWithUserCode(StreamErrorCode error) = 0;
  virtual void SendStopSending(StreamErrorCode error) = 0;

  // A general-purpose stream reset method that may be used when a specific
  // error code is not available.
  virtual void ResetDueToInternalError() = 0;
  // If the stream has not been already reset, reset the stream. This is
  // primarily used in the JavaScript API when the stream object has been
  // garbage collected.
  virtual void MaybeResetDueToStreamObjectGone() = 0;

  virtual StreamVisitor* visitor() = 0;
  virtual void SetVisitor(std::unique_ptr<StreamVisitor> visitor) = 0;
};

// Visitor that gets notified about events related to a WebTransport session.
class QUICHE_EXPORT SessionVisitor {
 public:
  virtual ~SessionVisitor() {}

  // Notifies the visitor when the session is ready to exchange application
  // data.
  virtual void OnSessionReady() = 0;

  // Notifies the visitor when the session has been closed.
  virtual void OnSessionClosed(SessionErrorCode error_code,
                               const std::string& error_message) = 0;

  // Notifies the visitor when a new stream has been received.  The stream in
  // question can be retrieved using AcceptIncomingBidirectionalStream() or
  // AcceptIncomingUnidirectionalStream().
  virtual void OnIncomingBidirectionalStreamAvailable() = 0;
  virtual void OnIncomingUnidirectionalStreamAvailable() = 0;

  // Notifies the visitor when a new datagram has been received.
  virtual void OnDatagramReceived(absl::string_view datagram) = 0;

  // Notifies the visitor that a new outgoing stream can now be created.
  virtual void OnCanCreateNewOutgoingBidirectionalStream() = 0;
  virtual void OnCanCreateNewOutgoingUnidirectionalStream() = 0;
};

// An abstract interface for a WebTransport session.
//
// *** AN IMPORTANT NOTE ABOUT STREAM LIFETIMES ***
// Stream objects are managed internally by the underlying QUIC stack, and can
// go away at any time due to the peer resetting the stream. Because of that,
// any pointers to the stream objects returned by this class MUST NEVER be
// retained long-term, except inside the stream visitor (the stream visitor is
// owned by the stream object). If you need to store a reference to a stream,
// consider one of the two following options:
//   (1) store a stream ID,
//   (2) store a weak pointer to the stream visitor, and then access the stream
//       via the said visitor (the visitor is guaranteed to be alive as long as
//       the stream is alive).
class QUICHE_EXPORT Session {
 public:
  virtual ~Session() {}

  // Closes the WebTransport session in question with the specified |error_code|
  // and |error_message|.
  virtual void CloseSession(SessionErrorCode error_code,
                            absl::string_view error_message) = 0;

  // Return the earliest incoming stream that has been received by the session
  // but has not been accepted.  Returns nullptr if there are no incoming
  // streams.  See the class note regarding the lifetime of the returned stream
  // object.
  virtual Stream* AcceptIncomingBidirectionalStream() = 0;
  virtual Stream* AcceptIncomingUnidirectionalStream() = 0;

  // Returns true if flow control allows opening a new stream.
  //
  // IMPORTANT: See the class note regarding the lifetime of the returned stream
  // object.
  virtual bool CanOpenNextOutgoingBidirectionalStream() = 0;
  virtual bool CanOpenNextOutgoingUnidirectionalStream() = 0;

  // Opens a new WebTransport stream, or returns nullptr if that is not possible
  // due to flow control.  See the class note regarding the lifetime of the
  // returned stream object.
  //
  // IMPORTANT: See the class note regarding the lifetime of the returned stream
  // object.
  virtual Stream* OpenOutgoingBidirectionalStream() = 0;
  virtual Stream* OpenOutgoingUnidirectionalStream() = 0;

  // Returns the WebTransport stream with the corresponding ID.
  //
  // IMPORTANT: See the class note regarding the lifetime of the returned stream
  // object.
  virtual Stream* GetStreamById(StreamId id) = 0;

  virtual DatagramStatus SendOrQueueDatagram(absl::string_view datagram) = 0;
  // Returns a conservative estimate of the largest datagram size that the
  // session would be able to send.
  virtual uint64_t GetMaxDatagramSize() const = 0;
  // Sets the largest duration that a datagram can spend in the queue before
  // being silently dropped.
  virtual void SetDatagramMaxTimeInQueue(absl::Duration max_time_in_queue) = 0;

  // Returns stats that generally follow the semantics of W3C WebTransport API.
  virtual DatagramStats GetDatagramStats() = 0;
  virtual SessionStats GetSessionStats() = 0;

  // Sends a DRAIN_WEBTRANSPORT_SESSION capsule or an equivalent signal to the
  // peer indicating that the session is draining.
  virtual void NotifySessionDraining() = 0;
  // Notifies that either the session itself (DRAIN_WEBTRANSPORT_SESSION
  // capsule), or the underlying connection (HTTP GOAWAY) is being drained by
  // the peer.
  virtual void SetOnDraining(quiche::SingleUseCallback<void()> callback) = 0;
};

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_H_
