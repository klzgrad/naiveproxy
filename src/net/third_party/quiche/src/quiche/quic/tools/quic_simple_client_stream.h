// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_

#include <functional>
#include <utility>

#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

class QuicSimpleClientStream : public QuicSpdyClientStream {
 public:
  QuicSimpleClientStream(QuicStreamId id, QuicSpdyClientSession* session,
                         StreamType type, bool drop_response_body)
      : QuicSpdyClientStream(id, session, type),
        drop_response_body_(drop_response_body) {}
  void OnBodyAvailable() override;

  void set_on_interim_headers(
      quiche::MultiUseCallback<void(const quiche::HttpHeaderBlock&)>
          on_interim_headers) {
    on_interim_headers_ = std::move(on_interim_headers);
  }

 protected:
  bool ParseAndValidateStatusCode() override;

 private:
  quiche::MultiUseCallback<void(const quiche::HttpHeaderBlock&)>
      on_interim_headers_;
  const bool drop_response_body_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
