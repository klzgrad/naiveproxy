// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_
#define NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/auth.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/next_proto.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace net {

class IOBuffer;
class URLRequestContext;
class WebSocketStream;
class WebSocketStreamRequest;
struct WebSocketFrame;
struct WebSocketHandshakeResponseInfo;

struct WebSocketTunnelConfig {
  GURL url;
  AuthCredentials credentials;
};

// Adapts the Chromium WebSocket implementation to the StreamSocket interface.
// The first binary message is a target-address header; the server replies with
// a one-byte status before ordinary binary frames carry the TCP payload.
class WebSocketTunnelSocket : public StreamSocket {
 public:
  WebSocketTunnelSocket(const GURL& socket_url,
                        const AuthCredentials& credentials,
                        const HostPortPair& target,
                        URLRequestContext* url_request_context,
                        const NetLogWithSource& net_log,
                        const NetworkTrafficAnnotationTag& traffic_annotation);
  ~WebSocketTunnelSocket() override;
  WebSocketTunnelSocket(const WebSocketTunnelSocket&) = delete;
  WebSocketTunnelSocket& operator=(const WebSocketTunnelSocket&) = delete;

  // StreamSocket:
  int Connect(CompletionOnceCallback callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;
  const NetLogWithSource& NetLog() const override;
  bool WasEverUsed() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* ssl_info) override;
  int64_t GetTotalReceivedBytes() const override;
  void ApplySocketTag(const SocketTag& tag) override;

  PaddingType negotiated_padding_type() const {
    return negotiated_padding_type_;
  }

  // Socket:
  int Read(IOBuffer* buf,
           int buf_len,
           CompletionOnceCallback callback) override;
  int Write(IOBuffer* buf,
            int buf_len,
            CompletionOnceCallback callback,
            const NetworkTrafficAnnotationTag& traffic_annotation) override;
  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;

 private:
  class ConnectDelegateImpl;
  enum class State {
    kDisconnected,
    kConnecting,
    kSendingTarget,
    kReadingStatus,
    kConnected,
  };

  void OnConnectSuccess(
      std::unique_ptr<WebSocketStream> stream,
      std::unique_ptr<WebSocketHandshakeResponseInfo> response);
  void OnConnectFailure(int error);
  int BeginSendTarget();
  void OnSendTargetComplete(int result);
  int BeginReadStatus();
  void OnReadFramesComplete(int result);
  int ReadWithBuffer();
  int CopyAvailableData();
  void MaybeSendPong(const WebSocketFrame& ping_frame);
  void OnControlWriteComplete(int result);
  int BeginUserWrite();
  void OnWriteFramesComplete(int result);
  void Fail(int error);
  void CompleteConnect(int error);

  GURL socket_url_;
  AuthCredentials credentials_;
  HostPortPair target_;
  raw_ptr<URLRequestContext> url_request_context_;
  NetLogWithSource net_log_;
  const NetworkTrafficAnnotationTag& traffic_annotation_;
  IPEndPoint peer_address_;
  NextProto negotiated_protocol_ = NextProto::kProtoUnknown;
  PaddingType negotiated_padding_type_ = PaddingType::kNone;

  State state_ = State::kDisconnected;
  bool ever_used_ = false;
  bool read_pending_ = false;
  bool write_pending_ = false;
  size_t read_offset_ = 0;

  std::unique_ptr<WebSocketStreamRequest> request_;
  std::unique_ptr<WebSocketStream> stream_;
  std::vector<std::unique_ptr<WebSocketFrame>> read_frames_;
  std::vector<std::unique_ptr<WebSocketFrame>> write_frames_;
  std::vector<std::unique_ptr<WebSocketFrame>> control_frames_;
  scoped_refptr<IOBuffer> write_payload_;
  scoped_refptr<IOBuffer> control_payload_;

  raw_ptr<IOBuffer> read_user_buffer_;
  int read_user_buffer_len_ = 0;
  CompletionOnceCallback connect_callback_;
  CompletionOnceCallback read_callback_;
  CompletionOnceCallback write_callback_;
  int write_result_size_ = 0;
  bool control_write_pending_ = false;
  bool user_write_queued_ = false;

  base::WeakPtrFactory<WebSocketTunnelSocket> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_
