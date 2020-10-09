// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_CLIENT_PROMISED_INFO_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_CLIENT_PROMISED_INFO_H_

#include <cstddef>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_client_push_promise_index.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session_base.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

namespace test {
class QuicClientPromisedInfoPeer;
}  // namespace test

// QuicClientPromisedInfo tracks the client state of a server push
// stream from the time a PUSH_PROMISE is received until rendezvous
// between the promised response and the corresponding client request
// is complete.
class QUIC_EXPORT_PRIVATE QuicClientPromisedInfo
    : public QuicClientPushPromiseIndex::TryHandle {
 public:
  // Interface to QuicSpdyClientStream
  QuicClientPromisedInfo(QuicSpdyClientSessionBase* session,
                         QuicStreamId id,
                         std::string url);
  QuicClientPromisedInfo(const QuicClientPromisedInfo&) = delete;
  QuicClientPromisedInfo& operator=(const QuicClientPromisedInfo&) = delete;
  virtual ~QuicClientPromisedInfo();

  void Init();

  // Validate promise headers etc. Returns true if headers are valid.
  bool OnPromiseHeaders(const spdy::SpdyHeaderBlock& headers);

  // Store response, possibly proceed with final validation.
  void OnResponseHeaders(const spdy::SpdyHeaderBlock& headers);

  // Rendezvous between this promised stream and a client request that
  // has a matching URL.
  virtual QuicAsyncStatus HandleClientRequest(
      const spdy::SpdyHeaderBlock& headers,
      QuicClientPushPromiseIndex::Delegate* delegate);

  void Cancel() override;

  void Reset(QuicRstStreamErrorCode error_code);

  // Client requests are initially associated to promises by matching
  // URL in the client request against the URL in the promise headers,
  // uing the |promised_by_url| map.  The push can be cross-origin, so
  // the client should validate that the session is authoritative for
  // the promised URL.  If not, it should call |RejectUnauthorized|.
  QuicSpdyClientSessionBase* session() { return session_; }

  // If the promised response contains Vary header, then the fields
  // specified by Vary must match between the client request header
  // and the promise headers (see https://crbug.com//554220).  Vary
  // validation requires the response headers (for the actual Vary
  // field list), the promise headers (taking the role of the "cached"
  // request), and the client request headers.
  spdy::SpdyHeaderBlock* request_headers() { return &request_headers_; }

  spdy::SpdyHeaderBlock* response_headers() { return response_headers_.get(); }

  // After validation, client will use this to access the pushed stream.

  QuicStreamId id() const { return id_; }

  const std::string url() const { return url_; }

  // Return true if there's a request pending matching this push promise.
  bool is_validating() const { return client_request_delegate_ != nullptr; }

 private:
  friend class test::QuicClientPromisedInfoPeer;

  class QUIC_EXPORT_PRIVATE CleanupAlarm : public QuicAlarm::Delegate {
   public:
    explicit CleanupAlarm(QuicClientPromisedInfo* promised)
        : promised_(promised) {}

    void OnAlarm() override;

    QuicClientPromisedInfo* promised_;
  };

  QuicAsyncStatus FinalValidation();

  QuicSpdyClientSessionBase* session_;
  QuicStreamId id_;
  std::string url_;
  spdy::SpdyHeaderBlock request_headers_;
  std::unique_ptr<spdy::SpdyHeaderBlock> response_headers_;
  spdy::SpdyHeaderBlock client_request_headers_;
  QuicClientPushPromiseIndex::Delegate* client_request_delegate_;

  // The promise will commit suicide eventually if it is not claimed by a GET
  // first.
  std::unique_ptr<QuicAlarm> cleanup_alarm_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_CLIENT_PROMISED_INFO_H_
