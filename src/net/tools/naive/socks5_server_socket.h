// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_SOCKS5_SERVER_SOCKET_H_
#define NET_TOOLS_NAIVE_SOCKS5_SERVER_SOCKET_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/next_proto.h"
#include "net/socket/stream_socket.h"
#include "net/ssl/ssl_info.h"

namespace net {
struct NetworkTrafficAnnotationTag;

// This StreamSocket is used to setup a SOCKSv5 handshake with a socks client.
// Currently no SOCKSv5 authentication is supported.
class Socks5ServerSocket : public StreamSocket {
 public:
  Socks5ServerSocket(std::unique_ptr<StreamSocket> transport_socket,
                     const NetworkTrafficAnnotationTag& traffic_annotation);

  // On destruction Disconnect() is called.
  ~Socks5ServerSocket() override;

  const HostPortPair& request_endpoint() const;

  // StreamSocket implementation.

  // Does the SOCKS handshake and completes the protocol.
  int Connect(CompletionOnceCallback callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  const NetLogWithSource& NetLog() const override;
  bool WasEverUsed() const override;
  bool WasAlpnNegotiated() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* ssl_info) override;
  void GetConnectionAttempts(ConnectionAttempts* out) const override;
  void ClearConnectionAttempts() override {}
  void AddConnectionAttempts(const ConnectionAttempts& attempts) override {}
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
    STATE_GREET_READ,
    STATE_GREET_READ_COMPLETE,
    STATE_GREET_WRITE,
    STATE_GREET_WRITE_COMPLETE,
    STATE_HANDSHAKE_WRITE,
    STATE_HANDSHAKE_WRITE_COMPLETE,
    STATE_HANDSHAKE_READ,
    STATE_HANDSHAKE_READ_COMPLETE,
    STATE_NONE,
  };

  // Addressing type that can be specified in requests or responses.
  enum SocksEndPointAddressType {
    kEndPointDomain = 0x03,
    kEndPointResolvedIPv4 = 0x01,
    kEndPointResolvedIPv6 = 0x04,
  };

  enum SocksCommandType {
    kCommandConnect = 0x01,
    kCommandBind = 0x02,
    kCommandUDPAssociate = 0x03,
  };

  static const unsigned int kGreetReadHeaderSize;
  static const unsigned int kReadHeaderSize;
  static const char kSOCKS5Version;
  static const char kSOCKS5Reserved;
  static const char kAuthMethodNone;
  static const char kAuthMethodNoAcceptable;
  static const char kReplySuccess;
  static const char kReplyCommandNotSupported;

  void DoCallback(int result);
  void OnIOComplete(int result);
  void OnReadWriteComplete(CompletionOnceCallback callback, int result);

  int DoLoop(int last_io_result);
  int DoGreetWrite();
  int DoGreetWriteComplete(int result);
  int DoGreetRead();
  int DoGreetReadComplete(int result);
  int DoHandshakeRead();
  int DoHandshakeReadComplete(int result);
  int DoHandshakeWrite();
  int DoHandshakeWriteComplete(int result);

  CompletionRepeatingCallback io_callback_;

  // Stores the underlying socket.
  std::unique_ptr<StreamSocket> transport_;

  State next_state_;

  // Stores the callback to the layer above, called on completing Connect().
  CompletionOnceCallback user_callback_;

  // This IOBuffer is used by the class to read and write
  // SOCKS handshake data. The length contains the expected size to
  // read or write.
  scoped_refptr<IOBuffer> handshake_buf_;

  // While writing, this buffer stores the complete write handshake data.
  // While reading, it stores the handshake information received so far.
  std::string buffer_;

  // This becomes true when the SOCKS handshake has completed and the
  // overlying connection is free to communicate.
  bool completed_handshake_;

  // These contain the bytes received / sent by the SOCKS handshake.
  size_t bytes_received_;
  size_t bytes_sent_;

  size_t greet_read_header_size_;
  size_t read_header_size_;

  bool was_ever_used_;

  SocksEndPointAddressType address_type_;
  int address_size_;

  char auth_method_;
  char reply_;

  HostPortPair request_endpoint_;

  NetLogWithSource net_log_;

  // Traffic annotation for socket control.
  const NetworkTrafficAnnotationTag& traffic_annotation_;

  DISALLOW_COPY_AND_ASSIGN(Socks5ServerSocket);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_SOCKS5_SERVER_SOCKET_H_
