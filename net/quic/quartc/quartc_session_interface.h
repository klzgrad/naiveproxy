// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_SESSION_INTERFACE_H_
#define NET_QUIC_QUARTC_QUARTC_SESSION_INTERFACE_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/quartc/quartc_stream_interface.h"

namespace net {

// Structure holding stats exported by a QuartcSession.
struct QUIC_EXPORT_PRIVATE QuartcSessionStats {
  // Bandwidth estimate in bits per second.
  int64_t bandwidth_estimate_bits_per_second;
};

// Given a PacketTransport, provides a way to send and receive separate streams
// of reliable, in-order, encrypted data. For example, this can build on top of
// a WebRTC IceTransport for sending and receiving data over QUIC.
class QUIC_EXPORT_PRIVATE QuartcSessionInterface {
 public:
  virtual ~QuartcSessionInterface() {}

  virtual void StartCryptoHandshake() = 0;

  // Only needed when using SRTP with QuicTransport
  // Key Exporter interface from RFC 5705
  // Arguments are:
  // label               -- the exporter label.
  //                        part of the RFC defining each exporter usage (IN)
  // context/context_len -- a context to bind to for this connection;
  //                        optional, can be NULL, 0 (IN)
  // use_context         -- whether to use the context value
  //                        (needed to distinguish no context from
  //                        zero-length ones).
  // result              -- where to put the computed value
  // result_len          -- the length of the computed value
  virtual bool ExportKeyingMaterial(const std::string& label,
                                    const uint8_t* context,
                                    size_t context_len,
                                    bool used_context,
                                    uint8_t* result,
                                    size_t result_len) = 0;

  // For forward-compatibility. More parameters could be added through the
  // struct without changing the API.
  struct OutgoingStreamParameters {};

  virtual QuartcStreamInterface* CreateOutgoingStream(
      const OutgoingStreamParameters& params) = 0;

  // If the given stream is still open, sends a reset frame to cancel it.
  // Note:  This method cancels a stream by QuicStreamId rather than by pointer
  // (or by a method on QuartcStreamInterface) because QuartcSession (and not
  // the caller) owns the streams.  Streams may finish and be deleted before the
  // caller tries to cancel them, rendering the caller's pointers invalid.
  virtual void CancelStream(QuicStreamId stream_id) = 0;

  // This method verifies if a stream is still open and stream pointer can be
  // used. When true is returned, the interface pointer is good for making a
  // call immediately on the same thread, but may be rendered invalid by ANY
  // other QUIC activity.
  virtual bool IsOpenStream(QuicStreamId stream_id) = 0;

  // Gets stats associated with this Quartc session.
  virtual QuartcSessionStats GetStats() = 0;

  // Send and receive packets, like a virtual UDP socket. For example, this
  // could be implemented by WebRTC's IceTransport.
  class PacketTransport {
   public:
    virtual ~PacketTransport() {}

    // Called by the QuartcPacketWriter to check if the underneath transport is
    // writable. True if packets written are expected to be sent.  False if
    // packets will be dropped.
    virtual bool CanWrite() = 0;

    // Called by the QuartcPacketWriter when writing packets to the network.
    // Return the number of written bytes. Return 0 if the write is blocked.
    virtual int Write(const char* buffer, size_t buf_len) = 0;
  };

  // Called when CanWrite() changes from false to true.
  virtual void OnTransportCanWrite() = 0;

  // Called when a packet has been received and should be handled by the
  // QuicConnection.
  virtual bool OnTransportReceived(const char* data, size_t data_len) = 0;

  // Callbacks called by the QuartcSession to notify the user of the
  // QuartcSession of certain events.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the crypto handshake is complete.
    virtual void OnCryptoHandshakeComplete() = 0;

    // Called when a new stream is received from the remote endpoint.
    virtual void OnIncomingStream(QuartcStreamInterface* stream) = 0;

    // Called when the connection is closed. This means all of the streams will
    // be closed and no new streams can be created.
    // TODO(zhihuang): Create mapping from integer error code to WebRTC error
    // code.
    virtual void OnConnectionClosed(int error_code, bool from_remote) = 0;

    // TODO(zhihuang): Add proof verification.
  };

  // The |delegate| is not owned by QuartcSession.
  virtual void SetDelegate(Delegate* delegate) = 0;
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_SESSION_INTERFACE_H_
