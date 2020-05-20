// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_SESSION_H_
#define NET_DNS_DNS_SESSION_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_socket_pool.h"

namespace net {

class DatagramClientSocket;
class NetLog;
struct NetLogSource;
class StreamSocket;

// Session parameters and state shared between DnsTransactions for a specific
// instance/version of a DnsConfig. Also may be used as a key handle for
// per-session state stored elsewhere, e.g. in ResolveContext. A new DnsSession
// should be created for each new DnsConfig for DnsTransactions to be based on
// that new configuration.
//
// Ref-counted so that DnsTransactions can keep working in absence of
// DnsClient or continue operating on the same session after DnsClient has moved
// to a new DnsConfig/DnsSession.
class NET_EXPORT_PRIVATE DnsSession : public base::RefCounted<DnsSession> {
 public:
  typedef base::RepeatingCallback<int()> RandCallback;

  class NET_EXPORT_PRIVATE SocketLease {
   public:
    SocketLease(scoped_refptr<DnsSession> session,
                size_t server_index,
                std::unique_ptr<DatagramClientSocket> socket);
    ~SocketLease();

    size_t server_index() const { return server_index_; }

    DatagramClientSocket* socket() { return socket_.get(); }

   private:
    scoped_refptr<DnsSession> session_;
    size_t server_index_;
    std::unique_ptr<DatagramClientSocket> socket_;

    DISALLOW_COPY_AND_ASSIGN(SocketLease);
  };

  DnsSession(const DnsConfig& config,
             std::unique_ptr<DnsSocketPool> socket_pool,
             const RandIntCallback& rand_int_callback,
             NetLog* net_log);

  const DnsConfig& config() const { return config_; }
  NetLog* net_log() const { return net_log_; }

  // Return the next random query ID.
  uint16_t NextQueryId() const;

  // Allocate a socket, already connected to the server address.
  // When the SocketLease is destroyed, the socket will be freed.
  std::unique_ptr<SocketLease> AllocateSocket(size_t server_index,
                                              const NetLogSource& source);

  // Creates a StreamSocket from the factory for a transaction over TCP. These
  // sockets are not pooled.
  std::unique_ptr<StreamSocket> CreateTCPSocket(size_t server_index,
                                                const NetLogSource& source);

  base::WeakPtr<DnsSession> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  base::WeakPtr<const DnsSession> GetWeakPtr() const {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void InvalidateWeakPtrsForTesting();

 private:
  friend class base::RefCounted<DnsSession>;

  ~DnsSession();

  // Release a socket.
  void FreeSocket(size_t server_index,
                  std::unique_ptr<DatagramClientSocket> socket);

  const DnsConfig config_;
  std::unique_ptr<DnsSocketPool> socket_pool_;
  RandCallback rand_callback_;
  NetLog* net_log_;

  mutable base::WeakPtrFactory<DnsSession> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DnsSession);
};

}  // namespace net

#endif  // NET_DNS_DNS_SESSION_H_
