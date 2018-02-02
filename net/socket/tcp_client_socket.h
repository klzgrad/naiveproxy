// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TCP_CLIENT_SOCKET_H_
#define NET_SOCKET_TCP_CLIENT_SOCKET_H_

#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_socket.h"

namespace net {

class NetLog;
struct NetLogSource;
class SocketPerformanceWatcher;

// A client socket that uses TCP as the transport layer.
class NET_EXPORT TCPClientSocket : public StreamSocket {
 public:
  // The IP address(es) and port number to connect to.  The TCP socket will try
  // each IP address in the list until it succeeds in establishing a
  // connection.
  TCPClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      net::NetLog* net_log,
      const net::NetLogSource& source);

  // Adopts the given, connected socket and then acts as if Connect() had been
  // called. This function is used by TCPServerSocket and for testing.
  TCPClientSocket(std::unique_ptr<TCPSocket> connected_socket,
                  const IPEndPoint& peer_address);

  ~TCPClientSocket() override;

  // Binds the socket to a local IP address and port.
  int Bind(const IPEndPoint& address);

  // StreamSocket implementation.
  int Connect(const CompletionCallback& callback) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool IsConnectedAndIdle() const override;
  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;
  const NetLogWithSource& NetLog() const override;
  void SetSubresourceSpeculation() override;
  void SetOmniboxSpeculation() override;
  bool WasEverUsed() const override;
  void EnableTCPFastOpenIfSupported() override;
  bool WasAlpnNegotiated() const override;
  NextProto GetNegotiatedProtocol() const override;
  bool GetSSLInfo(SSLInfo* ssl_info) override;

  // Socket implementation.
  // Multiple outstanding requests are not supported.
  // Full duplex mode (reading and writing at the same time) is supported.
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override;
  int ReadIfReady(IOBuffer* buf,
                  int buf_len,
                  const CompletionCallback& callback) override;
  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override;
  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;

  virtual bool SetKeepAlive(bool enable, int delay);
  virtual bool SetNoDelay(bool no_delay);

  void GetConnectionAttempts(ConnectionAttempts* out) const override;
  void ClearConnectionAttempts() override;
  void AddConnectionAttempts(const ConnectionAttempts& attempts) override;
  int64_t GetTotalReceivedBytes() const override;

 private:
  // State machine for connecting the socket.
  enum ConnectState {
    CONNECT_STATE_CONNECT,
    CONNECT_STATE_CONNECT_COMPLETE,
    CONNECT_STATE_NONE,
  };

  // A helper method shared by Read() and ReadIfReady(). If |read_if_ready| is
  // set to true, ReadIfReady() will be used instead of Read().
  int ReadCommon(IOBuffer* buf,
                 int buf_len,
                 const CompletionCallback& callback,
                 bool read_if_ready);

  // State machine used by Connect().
  int DoConnectLoop(int result);
  int DoConnect();
  int DoConnectComplete(int result);

  // Helper used by Disconnect(), which disconnects minus resetting
  // current_address_index_ and bind_address_.
  void DoDisconnect();

  void DidCompleteConnect(int result);
  void DidCompleteRead(const CompletionCallback& callback, int result);
  void DidCompleteWrite(const CompletionCallback& callback, int result);
  void DidCompleteReadWrite(const CompletionCallback& callback, int result);

  int OpenSocket(AddressFamily family);

  // Emits histograms for TCP metrics, at the time the socket is
  // disconnected.
  void EmitTCPMetricsHistogramsOnDisconnect();

  // Socket performance statistics (such as RTT) are reported to the
  // |socket_performance_watcher_|. May be nullptr.
  // |socket_performance_watcher_| is owned by |socket_|. If non-null,
  // |socket_performance_watcher_| is guaranteed to be destroyed when |socket_|
  // is destroyed.
  SocketPerformanceWatcher* socket_performance_watcher_;

  std::unique_ptr<TCPSocket> socket_;

  // Local IP address and port we are bound to. Set to NULL if Bind()
  // wasn't called (in that case OS chooses address/port).
  std::unique_ptr<IPEndPoint> bind_address_;

  // The list of addresses we should try in order to establish a connection.
  AddressList addresses_;

  // Where we are in above list. Set to -1 if uninitialized.
  int current_address_index_;

  // External callback; called when connect is complete.
  CompletionCallback connect_callback_;

  // The next state for the Connect() state machine.
  ConnectState next_connect_state_;

  // This socket was previously disconnected and has not been re-connected.
  bool previously_disconnected_;

  // Record of connectivity and transmissions, for use in speculative connection
  // histograms.
  UseHistory use_history_;

  // Failed connection attempts made while trying to connect this socket.
  ConnectionAttempts connection_attempts_;

  // Total number of bytes received by the socket.
  int64_t total_received_bytes_;

  DISALLOW_COPY_AND_ASSIGN(TCPClientSocket);
};

}  // namespace net

#endif  // NET_SOCKET_TCP_CLIENT_SOCKET_H_
