// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_
#define NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/auth.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
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

// Adapts a Chromium WebSocketStream to StreamSocket. The first binary message
// selects the target; the server replies with one status byte before payload.
class WebSocketTunnelSocket : public StreamSocket {
 public:
  WebSocketTunnelSocket(const GURL& url,
                        const AuthCredentials& credentials,
                        const HostPortPair& target,
                        URLRequestContext* context,
                        const NetLogWithSource& net_log,
                        const NetworkTrafficAnnotationTag& annotation);
  ~WebSocketTunnelSocket() override;

  int Connect(CompletionOnceCallback callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;
  const NetLogWithSource& NetLog() const override;
  bool WasEverUsed() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* info) override;
  int64_t GetTotalReceivedBytes() const override;
  void ApplySocketTag(const SocketTag& tag) override;
  int Read(IOBuffer* buf,
           int buf_len,
           CompletionOnceCallback callback) override;
  int Write(IOBuffer* buf,
           int buf_len,
           CompletionOnceCallback callback,
           const NetworkTrafficAnnotationTag& annotation) override;
  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;

  PaddingType negotiated_padding_type() const {
    return padding_type_;
  }

 private:
  class ConnectDelegate;
  enum class State { kDisconnected, kConnecting, kConnected };

  void OnConnectSuccess(
      std::unique_ptr<WebSocketStream> stream,
      std::unique_ptr<WebSocketHandshakeResponseInfo> response);
  void OnConnectFailure(int error);
  void SendTarget();
  void OnTargetSent(int result);
  int ReadFrames();
  void OnReadFrames(int result);
  int CopyData();
  void ProcessControlFrames();
  int BeginWrite();
  void OnWriteComplete(int result);
  void SendPong();
  void OnPongComplete(int result);
  void Post(base::OnceClosure task);
  void CompleteConnect(int result);
  void CompleteRead(int result);
  void CompleteWrite(int result);
  void Fail(int error);

  GURL url_;
  AuthCredentials credentials_;
  HostPortPair target_;
  raw_ptr<URLRequestContext> context_;
  NetLogWithSource net_log_;
  const NetworkTrafficAnnotationTag& annotation_;
  IPEndPoint peer_address_;
  NextProto protocol_ = NextProto::kProtoUnknown;
  PaddingType padding_type_ = PaddingType::kNone;
  State state_ = State::kDisconnected;
  bool ever_used_ = false;
  bool read_pending_ = false;
  bool write_pending_ = false;
  bool pong_pending_ = false;
  bool pong_write_pending_ = false;
  bool user_write_queued_ = false;
  size_t read_offset_ = 0;
  std::unique_ptr<WebSocketStreamRequest> request_;
  std::unique_ptr<WebSocketStream> stream_;
  std::vector<std::unique_ptr<WebSocketFrame>> read_frames_;
  std::vector<std::unique_ptr<WebSocketFrame>> write_frames_;
  std::vector<std::unique_ptr<WebSocketFrame>> pong_frames_;
  scoped_refptr<IOBuffer> write_payload_;
  scoped_refptr<IOBuffer> pong_payload_;
  raw_ptr<IOBuffer> read_buffer_ = nullptr;
  int read_buffer_len_ = 0;
  int write_size_ = 0;
  CompletionOnceCallback connect_callback_;
  CompletionOnceCallback read_callback_;
  CompletionOnceCallback write_callback_;
  base::WeakPtrFactory<WebSocketTunnelSocket> weak_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_SOCKET_H_
