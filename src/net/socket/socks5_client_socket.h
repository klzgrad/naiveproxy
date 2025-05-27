// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKS5_CLIENT_SOCKET_H_
#define NET_SOCKET_SOCKS5_CLIENT_SOCKET_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "net/base/address_list.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/stream_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace net {

class DrainableIOBuffer;
class GrowableIOBuffer;

// This StreamSocket is used to setup a SOCKSv5 handshake with a socks proxy.
// Currently no SOCKSv5 authentication is supported.
class NET_EXPORT_PRIVATE SOCKS5ClientSocket : public StreamSocket {
 public:
  // |destination| contains the hostname and port to which the socket above will
  // communicate to via the SOCKS layer.
  //
  // Although SOCKS 5 supports 3 different modes of addressing, we will
  // always pass it a hostname. This means the DNS resolving is done
  // proxy side.
  SOCKS5ClientSocket(std::unique_ptr<StreamSocket> transport_socket,
                     const HostPortPair& destination,
                     const NetworkTrafficAnnotationTag& traffic_annotation);

  SOCKS5ClientSocket(const SOCKS5ClientSocket&) = delete;
  SOCKS5ClientSocket& operator=(const SOCKS5ClientSocket&) = delete;

  // On destruction Disconnect() is called.
  ~SOCKS5ClientSocket() override;

  // StreamSocket implementation.

  // Does the SOCKS handshake and completes the protocol.
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
    STATE_GREET_WRITE,
    STATE_GREET_WRITE_COMPLETE,
    STATE_GREET_READ,
    STATE_GREET_READ_COMPLETE,
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

  static const unsigned int kGreetReadHeaderSize;
  static const unsigned int kWriteHeaderSize;
  static const unsigned int kReadHeaderSize;
  static const uint8_t kSOCKS5Version;
  static const uint8_t kTunnelCommand;
  static const uint8_t kNullByte;

  void DoCallback(int result);
  void OnIOComplete(int result);
  void OnReadWriteComplete(CompletionOnceCallback callback, int result);

  int DoLoop(int last_io_result);
  int DoHandshakeRead();
  int DoHandshakeReadComplete(int result);
  int DoHandshakeWrite();
  int DoHandshakeWriteComplete(int result);
  int DoGreetRead();
  int DoGreetReadComplete(int result);
  int DoGreetWrite();
  int DoGreetWriteComplete(int result);

  // Creates a DrainableIOBuffer containing the SOCKS handshake.
  scoped_refptr<DrainableIOBuffer> BuildHandshakeWriteBuffer() const;

  CompletionRepeatingCallback io_callback_;

  // Stores the underlying socket.
  std::unique_ptr<StreamSocket> transport_socket_;

  State next_state_ = STATE_NONE;

  // Stores the callback to the layer above, called on completing Connect().
  CompletionOnceCallback user_callback_;

  // The write buffer used for both the handshake and greet data. Once entirely
  // written, it's time to advance to the next phase.
  scoped_refptr<DrainableIOBuffer> write_buf_;

  // The read buffer used for both the handshake and greet data.
  scoped_refptr<GrowableIOBuffer> read_buf_;

  // This becomes true when the SOCKS handshake has completed and the
  // overlying connection is free to communicate.
  bool completed_handshake_ = false;

  bool was_ever_used_ = false;

  const HostPortPair destination_;

  NetLogWithSource net_log_;

  // Traffic annotation for socket control.
  NetworkTrafficAnnotationTag traffic_annotation_;
};

}  // namespace net

#endif  // NET_SOCKET_SOCKS5_CLIENT_SOCKET_H_
