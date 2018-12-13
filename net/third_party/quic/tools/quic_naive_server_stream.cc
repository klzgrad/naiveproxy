// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/tools/quic_naive_server_stream.h"

#include <list>
#include <utility>

#include "net/third_party/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/tools/quic_simple_server_session.h"

namespace quic {

QuicNaiveServerStream::QuicNaiveServerStream(QuicStreamId id,
                                             QuicSpdySession* session,
                                             QuicSimpleServerBackend* backend)
    : QuicSpdyServerStreamBase(id, session), backend_(backend) {}

QuicNaiveServerStream::~QuicNaiveServerStream() {
  backend_->OnDeleteStream(this);
}

void QuicNaiveServerStream::set_naive_id(unsigned int id) {
  naive_id_ = id;
}

unsigned int QuicNaiveServerStream::naive_id() {
  return naive_id_;
}

void QuicNaiveServerStream::OnInitialHeadersComplete(
    bool fin,
    size_t frame_len,
    const QuicHeaderList& header_list) {
  QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  backend_->OnReadHeaders(this, header_list);
  ConsumeHeaderList();
}

void QuicNaiveServerStream::OnTrailingHeadersComplete(
    bool fin,
    size_t frame_len,
    const QuicHeaderList& header_list) {
  QUIC_BUG << "Server does not support receiving Trailers.";
  spdy::SpdyHeaderBlock headers;
  headers[":status"] = "500";
  WriteHeaders(std::move(headers), /*fin=*/true, nullptr);
}

void QuicNaiveServerStream::OnDataAvailable() {
  while (HasBytesToRead()) {
    struct iovec iov;
    if (GetReadableRegions(&iov, 1) == 0) {
      // No more data to read.
      break;
    }
    backend_->OnReadData(this, iov.iov_base, iov.iov_len);
  }
  if (sequencer()->IsClosed()) {
    OnFinRead();
  } else {
    sequencer()->SetUnblocked();
  }
}

void QuicNaiveServerStream::PushResponse(
    spdy::SpdyHeaderBlock push_request_headers) {
  QUIC_NOTREACHED();
}

QuicConnectionId QuicNaiveServerStream::connection_id() const {
  return spdy_session()->connection_id();
}

QuicStreamId QuicNaiveServerStream::stream_id() const {
  return id();
}

QuicString QuicNaiveServerStream::peer_host() const {
  return spdy_session()->peer_address().host().ToString();
}

void QuicNaiveServerStream::OnResponseBackendComplete(
    const QuicBackendResponse* response,
    std::list<QuicBackendResponse::ServerPushInfo> resources) {}

}  // namespace quic
