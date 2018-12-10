// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_SOCKET_POOL_H_
#define NET_DNS_DNS_SOCKET_POOL_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"

namespace net {

class ClientSocketFactory;
class DatagramClientSocket;
class IPEndPoint;
class NetLog;
struct NetLogSource;
class StreamSocket;

// A DnsSocketPool is an abstraction layer around a ClientSocketFactory that
// allows preallocation, reuse, or other strategies to manage sockets connected
// to DNS servers.
class NET_EXPORT_PRIVATE DnsSocketPool {
 public:
  virtual ~DnsSocketPool();

  // Creates a DnsSocketPool that implements the default strategy for managing
  // sockets.  (This varies by platform; see DnsSocketPoolImpl in
  // dns_socket_pool.cc for details.)
  static std::unique_ptr<DnsSocketPool> CreateDefault(
      ClientSocketFactory* factory,
      const RandIntCallback& rand_int_callback);

  // Creates a DnsSocketPool that implements a "null" strategy -- no sockets are
  // preallocated, allocation requests are satisfied by calling the factory
  // directly, and returned sockets are deleted immediately.
  static std::unique_ptr<DnsSocketPool> CreateNull(
      ClientSocketFactory* factory,
      const RandIntCallback& rand_int_callback);

  // Initializes the DnsSocketPool.  |nameservers| is the list of nameservers
  // for which the DnsSocketPool will manage sockets; |net_log| is the NetLog
  // used when constructing sockets with the factory.
  //
  // Initialize may not be called more than once, and must be called before
  // calling AllocateSocket or FreeSocket.
  virtual void Initialize(
      const std::vector<IPEndPoint>* nameservers,
      NetLog* net_log) = 0;

  // Allocates a socket that is already connected to the nameserver referenced
  // by |server_index|.  May return a std::unique_ptr to NULL if no sockets are
  // available to reuse and the factory fails to produce a socket (or produces
  // one on which Connect fails).
  virtual std::unique_ptr<DatagramClientSocket> AllocateSocket(
      unsigned server_index) = 0;

  // Frees a socket allocated by AllocateSocket.  |server_index| must be the
  // same index passed to AllocateSocket.
  virtual void FreeSocket(unsigned server_index,
                          std::unique_ptr<DatagramClientSocket> socket) = 0;

  // Creates a StreamSocket from the factory for a transaction over TCP. These
  // sockets are not pooled.
  std::unique_ptr<StreamSocket> CreateTCPSocket(unsigned server_index,
                                                const NetLogSource& source);

 protected:
  DnsSocketPool(ClientSocketFactory* socket_factory,
                const RandIntCallback& rand_int_callback);

  void InitializeInternal(
      const std::vector<IPEndPoint>* nameservers,
      NetLog* net_log);

  std::unique_ptr<DatagramClientSocket> CreateConnectedSocket(
      unsigned server_index);

  // Returns a random int in the specified range.
  int GetRandomInt(int min, int max);

 private:
  ClientSocketFactory* socket_factory_;
  const RandIntCallback rand_int_callback_;
  NetLog* net_log_;
  const std::vector<IPEndPoint>* nameservers_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(DnsSocketPool);
};

} // namespace net

#endif // NET_DNS_DNS_SOCKET_POOL_H_
