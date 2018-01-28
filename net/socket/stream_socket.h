// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_STREAM_SOCKET_H_
#define NET_SOCKET_STREAM_SOCKET_H_

#include <stdint.h>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket.h"

namespace net {

class IPEndPoint;
class NetLogWithSource;
class SSLInfo;

class NET_EXPORT_PRIVATE StreamSocket : public Socket {
 public:
  // This is used in DumpMemoryStats() to track the estimate of memory usage of
  // a socket.
  struct NET_EXPORT_PRIVATE SocketMemoryStats {
   public:
    SocketMemoryStats();
    ~SocketMemoryStats();
    // Estimated total memory usage of this socket in bytes.
    size_t total_size;
    // Size of all buffers used by this socket in bytes.
    size_t buffer_size;
    // Number of certs used by this socket.
    size_t cert_count;
    // Total size of certs used by this socket in bytes.
    size_t cert_size;

   private:
    DISALLOW_COPY_AND_ASSIGN(SocketMemoryStats);
  };

  ~StreamSocket() override {}

  // Called to establish a connection.  Returns OK if the connection could be
  // established synchronously.  Otherwise, ERR_IO_PENDING is returned and the
  // given callback will run asynchronously when the connection is established
  // or when an error occurs.  The result is some other error code if the
  // connection could not be established.
  //
  // The socket's Read and Write methods may not be called until Connect
  // succeeds.
  //
  // It is valid to call Connect on an already connected socket, in which case
  // OK is simply returned.
  //
  // Connect may also be called again after a call to the Disconnect method.
  //
  virtual int Connect(const CompletionCallback& callback) = 0;

  // Called to disconnect a socket.  Does nothing if the socket is already
  // disconnected.  After calling Disconnect it is possible to call Connect
  // again to establish a new connection.
  //
  // If IO (Connect, Read, or Write) is pending when the socket is
  // disconnected, the pending IO is cancelled, and the completion callback
  // will not be called.
  virtual void Disconnect() = 0;

  // Called to test if the connection is still alive.  Returns false if a
  // connection wasn't established or the connection is dead.  True is returned
  // if the connection was terminated, but there is unread data in the incoming
  // buffer.
  virtual bool IsConnected() const = 0;

  // Called to test if the connection is still alive and idle.  Returns false
  // if a connection wasn't established, the connection is dead, or there is
  // unread data in the incoming buffer.
  virtual bool IsConnectedAndIdle() const = 0;

  // Copies the peer address to |address| and returns a network error code.
  // ERR_SOCKET_NOT_CONNECTED will be returned if the socket is not connected.
  virtual int GetPeerAddress(IPEndPoint* address) const = 0;

  // Copies the local address to |address| and returns a network error code.
  // ERR_SOCKET_NOT_CONNECTED will be returned if the socket is not bound.
  virtual int GetLocalAddress(IPEndPoint* address) const = 0;

  // Gets the NetLog for this socket.
  virtual const NetLogWithSource& NetLog() const = 0;

  // Set the annotation to indicate this socket was created for speculative
  // reasons.  This call is generally forwarded to a basic TCPClientSocket*,
  // where a UseHistory can be updated.
  virtual void SetSubresourceSpeculation() = 0;
  virtual void SetOmniboxSpeculation() = 0;

  // Returns true if the socket ever had any reads or writes.  StreamSockets
  // layered on top of transport sockets should return if their own Read() or
  // Write() methods had been called, not the underlying transport's.
  virtual bool WasEverUsed() const = 0;

  // TODO(jri): Clean up -- rename to a more general EnableAutoConnectOnWrite.
  // Enables use of TCP FastOpen for the underlying transport socket.
  virtual void EnableTCPFastOpenIfSupported() {}

  // Returns true if ALPN was negotiated during the connection of this socket.
  virtual bool WasAlpnNegotiated() const = 0;

  // Returns the protocol negotiated via ALPN for this socket, or
  // kProtoUnknown will be returned if ALPN is not applicable.
  virtual NextProto GetNegotiatedProtocol() const = 0;

  // Gets the SSL connection information of the socket.  Returns false if
  // SSL was not used by this socket.
  virtual bool GetSSLInfo(SSLInfo* ssl_info) = 0;

  // Overwrites |out| with the connection attempts made in the process of
  // connecting this socket.
  virtual void GetConnectionAttempts(ConnectionAttempts* out) const = 0;

  // Clears the socket's list of connection attempts.
  virtual void ClearConnectionAttempts() = 0;

  // Adds |attempts| to the socket's list of connection attempts.
  virtual void AddConnectionAttempts(const ConnectionAttempts& attempts) = 0;

  // Returns the total number of number bytes read by the socket. This only
  // counts the payload bytes. Transport headers are not counted. Returns
  // 0 if the socket does not implement the function. The count is reset when
  // Disconnect() is called.
  virtual int64_t GetTotalReceivedBytes() const = 0;

  // Dumps memory allocation stats into |stats|. |stats| can be assumed as being
  // default initialized upon entry. Implementations should override fields in
  // |stats|. Default implementation does nothing.
  virtual void DumpMemoryStats(SocketMemoryStats* stats) const {}

 protected:
  // The following class is only used to gather statistics about the history of
  // a socket.  It is only instantiated and used in basic sockets, such as
  // TCPClientSocket* instances.  Other classes that are derived from
  // StreamSocket should forward any potential settings to their underlying
  // transport sockets.
  class UseHistory {
   public:
    UseHistory();
    ~UseHistory();

    // Resets the state of UseHistory and emits histograms for the
    // current state.
    void Reset();

    void set_was_ever_connected();
    void set_was_used_to_convey_data();

    // The next two setters only have any impact if the socket has not yet been
    // used to transmit data.  If called later, we assume that the socket was
    // reused from the pool, and was NOT constructed to service a speculative
    // request.
    void set_subresource_speculation();
    void set_omnibox_speculation();

    bool was_used_to_convey_data() const;

   private:
    // Summarize the statistics for this socket.
    void EmitPreconnectionHistograms() const;
    // Indicate if this was ever connected.
    bool was_ever_connected_;
    // Indicate if this socket was ever used to transmit or receive data.
    bool was_used_to_convey_data_;

    // Indicate if this socket was first created for speculative use, and
    // identify the motivation.
    bool omnibox_speculation_;
    bool subresource_speculation_;
    DISALLOW_COPY_AND_ASSIGN(UseHistory);
  };
};

}  // namespace net

#endif  // NET_SOCKET_STREAM_SOCKET_H_
