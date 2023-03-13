// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy server specific QuicSession subclass.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_

#include <stdint.h>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/quic/tools/quic_simple_server_stream.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace test {
class QuicSimpleServerSessionPeer;
}  // namespace test

class QuicSimpleServerSession : public QuicServerSessionBase {
 public:
  // A PromisedStreamInfo is an element of the queue to store promised
  // stream which hasn't been created yet. It keeps a mapping between promised
  // stream id with its priority and the headers sent out in PUSH_PROMISE.
  struct PromisedStreamInfo {
   public:
    PromisedStreamInfo(spdy::Http2HeaderBlock request_headers,
                       QuicStreamId stream_id,
                       const spdy::SpdyStreamPrecedence& precedence)
        : request_headers(std::move(request_headers)),
          stream_id(stream_id),
          precedence(precedence),
          is_cancelled(false) {}
    spdy::Http2HeaderBlock request_headers;
    QuicStreamId stream_id;
    spdy::SpdyStreamPrecedence precedence;
    bool is_cancelled;
  };

  // Takes ownership of |connection|.
  QuicSimpleServerSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicSession::Visitor* visitor,
                          QuicCryptoServerStreamBase::Helper* helper,
                          const QuicCryptoServerConfig* crypto_config,
                          QuicCompressedCertsCache* compressed_certs_cache,
                          QuicSimpleServerBackend* quic_simple_server_backend);
  QuicSimpleServerSession(const QuicSimpleServerSession&) = delete;
  QuicSimpleServerSession& operator=(const QuicSimpleServerSession&) = delete;

  ~QuicSimpleServerSession() override;

  // Override base class to detact client sending data on server push stream.
  void OnStreamFrame(const QuicStreamFrame& frame) override;

  void OnCanCreateNewOutgoingStream(bool unidirectional) override;

 protected:
  // QuicSession methods:
  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override;
  QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  QuicSimpleServerStream* CreateOutgoingUnidirectionalStream() override;
  // Override to return true for locally preserved server push stream.
  void HandleFrameOnNonexistentOutgoingStream(QuicStreamId stream_id) override;
  // Override to handle reseting locally preserved streams.
  void HandleRstOnValidNonexistentStream(
      const QuicRstStreamFrame& frame) override;

  // QuicServerSessionBaseMethod:
  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;

  QuicSimpleServerBackend* server_backend() {
    return quic_simple_server_backend_;
  }

  void MaybeInitializeHttp3UnidirectionalStreams() override;

  bool ShouldNegotiateWebTransport() override {
    return quic_simple_server_backend_->SupportsWebTransport();
  }
  HttpDatagramSupport LocalHttpDatagramSupport() override {
    if (ShouldNegotiateWebTransport()) {
      return HttpDatagramSupport::kDraft04;
    }
    return QuicServerSessionBase::LocalHttpDatagramSupport();
  }

 private:
  friend class test::QuicSimpleServerSessionPeer;

  // Create a server push headers block by copying request's headers block.
  // But replace or add these pseudo-headers as they are specific to each
  // request:
  // :authority, :path, :method, :scheme, referer.
  // Copying the rest headers ensures they are the same as the original
  // request, especially cookies.
  spdy::Http2HeaderBlock SynthesizePushRequestHeaders(
      std::string request_url, QuicBackendResponse::ServerPushInfo resource,
      const spdy::Http2HeaderBlock& original_request_headers);

  // Send PUSH_PROMISE frame on headers stream.
  void SendPushPromise(QuicStreamId original_stream_id,
                       QuicStreamId promised_stream_id,
                       spdy::Http2HeaderBlock headers);

  // Fetch response from cache for request headers enqueued into
  // promised_headers_and_streams_ and send them on dedicated stream until
  // reaches max_open_stream_ limit.
  // Called when return value of GetNumOpenOutgoingStreams() changes:
  //    CloseStreamInner();
  //    StreamDraining();
  // Note that updateFlowControlOnFinalReceivedByteOffset() won't change the
  // return value becasue all push streams are impossible to become locally
  // closed. Since a locally preserved stream becomes remotely closed after
  // HandlePromisedPushRequests() starts to process it, and if it is reset
  // locally afterwards, it will be immediately become closed and never get into
  // locally_closed_stream_highest_offset_. So all the streams in this map
  // are not outgoing streams.
  void HandlePromisedPushRequests();

  // Keep track of the highest stream id which has been sent in PUSH_PROMISE.
  QuicStreamId highest_promised_stream_id_;

  // Promised streams which hasn't been created yet because of max_open_stream_
  // limit. New element is added to the end of the queue.
  // Since outgoing stream is created in sequence, stream_id of each element in
  // the queue also increases by 2 from previous one's. The front element's
  // stream_id is always next_outgoing_stream_id_, and the last one is always
  // highest_promised_stream_id_.
  quiche::QuicheCircularDeque<PromisedStreamInfo> promised_streams_;

  QuicSimpleServerBackend* quic_simple_server_backend_;  // Not owned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_
