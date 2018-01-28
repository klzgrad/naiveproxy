// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_handshake_stream_create_helper.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "net/socket/client_socket_handle.h"
#include "net/websockets/websocket_basic_handshake_stream.h"

namespace net {

WebSocketHandshakeStreamCreateHelper::WebSocketHandshakeStreamCreateHelper(
    WebSocketStream::ConnectDelegate* connect_delegate,
    const std::vector<std::string>& requested_subprotocols)
    : requested_subprotocols_(requested_subprotocols),
      connect_delegate_(connect_delegate),
      request_(nullptr) {
  DCHECK(connect_delegate_);
}

WebSocketHandshakeStreamCreateHelper::~WebSocketHandshakeStreamCreateHelper() =
    default;

std::unique_ptr<WebSocketHandshakeStreamBase>
WebSocketHandshakeStreamCreateHelper::CreateBasicStream(
    std::unique_ptr<ClientSocketHandle> connection,
    bool using_proxy) {
  DCHECK(request_) << "set_request() must be called";

  // The list of supported extensions and parameters is hard-coded.
  // TODO(ricea): If more extensions are added, consider a more flexible
  // method.
  std::vector<std::string> extensions(
      1, "permessage-deflate; client_max_window_bits");
  auto stream = std::make_unique<WebSocketBasicHandshakeStream>(
      std::move(connection), connect_delegate_, using_proxy,
      requested_subprotocols_, extensions, request_);
  OnBasicStreamCreated(stream.get());
  request_->OnHandshakeStreamCreated(stream.get());
  return std::move(stream);
}

void WebSocketHandshakeStreamCreateHelper::OnBasicStreamCreated(
    WebSocketBasicHandshakeStream* stream) {}

}  // namespace net
