// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_HTTP_PROXY_SERVER_SOCKET_H_
#define NET_TOOLS_NAIVE_HTTP_PROXY_SERVER_SOCKET_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/http/http_request_headers.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/next_proto.h"
#include "net/socket/stream_socket.h"
#include "net/ssl/ssl_info.h"
#include "net/tools/naive/naive_protocol.h"

namespace net {
struct NetworkTrafficAnnotationTag;
class ClientPaddingDetectorDelegate;

// This StreamSocket is used to setup a HTTP CONNECT tunnel.
class HttpProxyServerSocket : public StreamSocket {
 public:
  HttpProxyServerSocket(
      std::unique_ptr<StreamSocket> transport_socket,
      const std::string& user,
      const std::string& pass,
      ClientPaddingDetectorDelegate* padding_detector_delegate,
      const NetworkTrafficAnnotationTag& traffic_annotation,
      const std::vector<PaddingType>& supported_padding_types);
  HttpProxyServerSocket(const HttpProxyServerSocket&) = delete;
  HttpProxyServerSocket& operator=(const HttpProxyServerSocket&) = delete;

  // On destruction Disconnect() is called.
  ~HttpProxyServerSocket() override;

  const HostPortPair& request_endpoint() const;

  // StreamSocket implementation.

  int Connect(CompletionOnceCallback callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  const NetLogWithSource& NetLog() const override;
  bool WasEverUsed() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* ssl_info) override;
  int64_t GetTotalReceivedBytes() const override;
  void ApplySocketTag(const SocketTag& tag) override;

  // Socket implementation.
  int Read(IOBuffer* buf,
           int buf_len,
           CompletionOnceCallback callback) override;
  int Write(IOBuffer* buf,
            int buf_len,
            CompletionOnceCallback callback,
            const NetworkTrafficAnnotationTag& traffic_annotation) override;

  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;

  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;

 private:
  enum State {
    STATE_HEADER_READ,
    STATE_HEADER_READ_COMPLETE,
    STATE_HEADER_WRITE,
    STATE_HEADER_WRITE_COMPLETE,
    STATE_NONE,
  };

  void DoCallback(int result);
  void OnIOComplete(int result);
  void OnReadWriteComplete(CompletionOnceCallback callback, int result);

  int DoLoop(int last_io_result);
  int DoHeaderWrite();
  int DoHeaderWriteComplete(int result);
  int DoHeaderRead();
  int DoHeaderReadComplete(int result);

  std::optional<PaddingType> ParsePaddingHeaders(
      const HttpRequestHeaders& headers);

  CompletionRepeatingCallback io_callback_;

  // Stores the underlying socket.
  std::unique_ptr<StreamSocket> transport_;
  ClientPaddingDetectorDelegate* padding_detector_delegate_;

  State next_state_;

  // Stores the callback to the layer above, called on completing Connect().
  CompletionOnceCallback user_callback_;

  // This IOBuffer is used by the class to read and write
  // SOCKS handshake data. The length contains the expected size to
  // read or write.
  scoped_refptr<IOBuffer> handshake_buf_;

  std::string buffer_;
  bool completed_handshake_;
  bool was_ever_used_;
  int header_write_size_;

  std::string basic_auth_;

  HostPortPair request_endpoint_;

  NetLogWithSource net_log_;

  // Traffic annotation for socket control.
  const NetworkTrafficAnnotationTag& traffic_annotation_;

  std::vector<PaddingType> supported_padding_types_;
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_HTTP_PROXY_SERVER_SOCKET_H_
