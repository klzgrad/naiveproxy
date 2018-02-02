// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A toy server specific QuicSession subclass.

#ifndef NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_SESSION_H_
#define NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_SESSION_H_

#include <stdint.h>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_server_session_base.h"
#include "net/quic/core/quic_spdy_session.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/tools/quic/quic_http_response_cache.h"
#include "net/tools/quic/quic_simple_server_stream.h"

namespace net {

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
    PromisedStreamInfo(SpdyHeaderBlock request_headers,
                       QuicStreamId stream_id,
                       SpdyPriority priority)
        : request_headers(std::move(request_headers)),
          stream_id(stream_id),
          priority(priority),
          is_cancelled(false) {}
    SpdyHeaderBlock request_headers;
    QuicStreamId stream_id;
    SpdyPriority priority;
    bool is_cancelled;
  };

  // Takes ownership of |connection|.
  QuicSimpleServerSession(const QuicConfig& config,
                          QuicConnection* connection,
                          QuicSession::Visitor* visitor,
                          QuicCryptoServerStream::Helper* helper,
                          const QuicCryptoServerConfig* crypto_config,
                          QuicCompressedCertsCache* compressed_certs_cache,
                          QuicHttpResponseCache* response_cache);

  ~QuicSimpleServerSession() override;

  // When a stream is marked draining, it will decrease the number of open
  // streams. If it is an outgoing stream, try to open a new stream to send
  // remaing push responses.
  void StreamDraining(QuicStreamId id) override;

  // Override base class to detact client sending data on server push stream.
  void OnStreamFrame(const QuicStreamFrame& frame) override;

  // Send out PUSH_PROMISE for all |resources| promised stream id in each frame
  // will increase by 2 for each item in |resources|.
  // And enqueue HEADERS block in those PUSH_PROMISED for sending push response
  // later.
  virtual void PromisePushResources(
      const std::string& request_url,
      const std::list<QuicHttpResponseCache::ServerPushInfo>& resources,
      QuicStreamId original_stream_id,
      const SpdyHeaderBlock& original_request_headers);

 protected:
  // QuicSession methods:
  QuicSpdyStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicSimpleServerStream* CreateOutgoingDynamicStream() override;
  // Closing an outgoing stream can reduce open outgoing stream count, try
  // to handle queued promised streams right now.
  void CloseStreamInner(QuicStreamId stream_id, bool locally_reset) override;
  // Override to return true for locally preserved server push stream.
  void HandleFrameOnNonexistentOutgoingStream(QuicStreamId stream_id) override;
  // Override to handle reseting locally preserved streams.
  void HandleRstOnValidNonexistentStream(
      const QuicRstStreamFrame& frame) override;

  // QuicServerSessionBaseMethod:
  QuicCryptoServerStreamBase* CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;

  QuicHttpResponseCache* response_cache() { return response_cache_; }

 private:
  friend class test::QuicSimpleServerSessionPeer;

  // Create a server push headers block by copying request's headers block.
  // But replace or add these pseudo-headers as they are specific to each
  // request:
  // :authority, :path, :method, :scheme, referer.
  // Copying the rest headers ensures they are the same as the original
  // request, especially cookies.
  SpdyHeaderBlock SynthesizePushRequestHeaders(
      std::string request_url,
      QuicHttpResponseCache::ServerPushInfo resource,
      const SpdyHeaderBlock& original_request_headers);

  // Send PUSH_PROMISE frame on headers stream.
  void SendPushPromise(QuicStreamId original_stream_id,
                       QuicStreamId promised_stream_id,
                       SpdyHeaderBlock headers);

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
  QuicDeque<PromisedStreamInfo> promised_streams_;

  QuicHttpResponseCache* response_cache_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(QuicSimpleServerSession);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_SESSION_H_
