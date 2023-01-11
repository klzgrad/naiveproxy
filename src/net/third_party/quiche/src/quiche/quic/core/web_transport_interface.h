// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header contains interfaces that abstract away different backing
// protocols for WebTransport.

#ifndef QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_
#define QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_

#include <cstddef>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_datagram_queue.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// Visitor that gets notified about events related to a WebTransport stream.
class QUIC_EXPORT_PRIVATE WebTransportStreamVisitor {
 public:
  virtual ~WebTransportStreamVisitor() {}

  // Called whenever the stream has readable data available.
  virtual void OnCanRead() = 0;
  // Called whenever the stream is not write-blocked and can accept new data.
  virtual void OnCanWrite() = 0;

  // Called when RESET_STREAM is received for the stream.
  virtual void OnResetStreamReceived(WebTransportStreamError error) = 0;
  // Called when STOP_SENDING is received for the stream.
  virtual void OnStopSendingReceived(WebTransportStreamError error) = 0;
  // Called when the write side of the stream is closed and all of the data sent
  // has been acknowledged ("Data Recvd" state of RFC 9000).
  virtual void OnWriteSideInDataRecvdState() = 0;
};

// A stream (either bidirectional or unidirectional) that is contained within a
// WebTransport session.
class QUIC_EXPORT_PRIVATE WebTransportStream {
 public:
  struct QUIC_EXPORT_PRIVATE ReadResult {
    // Number of bytes actually read.
    size_t bytes_read;
    // Whether the FIN has been received; if true, no further data will arrive
    // on the stream, and the stream object can be soon potentially garbage
    // collected.
    bool fin;
  };

  virtual ~WebTransportStream() {}

  // Reads at most |buffer_size| bytes into |buffer|.
  ABSL_MUST_USE_RESULT virtual ReadResult Read(char* buffer,
                                               size_t buffer_size) = 0;
  // Reads all available data and appends it to the end of |output|.
  ABSL_MUST_USE_RESULT virtual ReadResult Read(std::string* output) = 0;
  // Writes |data| into the stream.  Returns true on success.
  ABSL_MUST_USE_RESULT virtual bool Write(absl::string_view data) = 0;
  // Sends the FIN on the stream.  Returns true on success.
  ABSL_MUST_USE_RESULT virtual bool SendFin() = 0;

  // Indicates whether it is possible to write into stream right now.
  virtual bool CanWrite() const = 0;
  // Indicates the number of bytes that can be read from the stream.
  virtual size_t ReadableBytes() const = 0;

  // An ID that is unique within the session.  Those are not exposed to the user
  // via the web API, but can be used internally for bookkeeping and
  // diagnostics.
  virtual QuicStreamId GetStreamId() const = 0;

  // Resets the stream with the specified error code.
  virtual void ResetWithUserCode(WebTransportStreamError error) = 0;
  virtual void ResetDueToInternalError() = 0;
  virtual void SendStopSending(WebTransportStreamError error) = 0;
  // Called when the owning object has been garbage-collected.
  virtual void MaybeResetDueToStreamObjectGone() = 0;

  virtual WebTransportStreamVisitor* visitor() = 0;
  virtual void SetVisitor(
      std::unique_ptr<WebTransportStreamVisitor> visitor) = 0;
};

// Visitor that gets notified about events related to a WebTransport session.
class QUIC_EXPORT_PRIVATE WebTransportVisitor {
 public:
  virtual ~WebTransportVisitor() {}

  // Notifies the visitor when the session is ready to exchange application
  // data.
  virtual void OnSessionReady(const spdy::Http2HeaderBlock& headers) = 0;

  // Notifies the visitor when the session has been closed.
  virtual void OnSessionClosed(WebTransportSessionError error_code,
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
class QUIC_EXPORT_PRIVATE WebTransportSession {
 public:
  virtual ~WebTransportSession() {}

  // Closes the WebTransport session in question with the specified |error_code|
  // and |error_message|.
  virtual void CloseSession(WebTransportSessionError error_code,
                            absl::string_view error_message) = 0;

  // Return the earliest incoming stream that has been received by the session
  // but has not been accepted.  Returns nullptr if there are no incoming
  // streams.
  virtual WebTransportStream* AcceptIncomingBidirectionalStream() = 0;
  virtual WebTransportStream* AcceptIncomingUnidirectionalStream() = 0;

  // Returns true if flow control allows opening a new stream.
  virtual bool CanOpenNextOutgoingBidirectionalStream() = 0;
  virtual bool CanOpenNextOutgoingUnidirectionalStream() = 0;
  // Opens a new WebTransport stream, or returns nullptr if that is not possible
  // due to flow control.
  virtual WebTransportStream* OpenOutgoingBidirectionalStream() = 0;
  virtual WebTransportStream* OpenOutgoingUnidirectionalStream() = 0;

  virtual MessageStatus SendOrQueueDatagram(
      quiche::QuicheMemSlice datagram) = 0;
  // Returns a conservative estimate of the largest datagram size that the
  // session would be able to send.
  virtual QuicByteCount GetMaxDatagramSize() const = 0;
  // Sets the largest duration that a datagram can spend in the queue before
  // being silently dropped.
  virtual void SetDatagramMaxTimeInQueue(QuicTime::Delta max_time_in_queue) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_
