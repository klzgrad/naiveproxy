// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_CLIENT_H_
#define NET_DNS_DNS_CLIENT_H_

#include <memory>

#include "base/values.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"

namespace net {

class AddressSorter;
class ClientSocketFactory;
struct DnsConfig;
class DnsTransactionFactory;
class NetLog;

// Convenience wrapper which allows easy injection of DnsTransaction into
// HostResolverImpl. Pointers returned by the Get* methods are only guaranteed
// to remain valid until next time SetConfig is called.
class NET_EXPORT DnsClient {
 public:
  virtual ~DnsClient() {}

  // Destroys the current DnsTransactionFactory and creates a new one
  // according to |config|, unless it is invalid or has |unhandled_options|.
  virtual void SetConfig(const DnsConfig& config) = 0;

  // Returns NULL if the current config is not valid.
  virtual const DnsConfig* GetConfig() const = 0;

  // Returns NULL if the current config is not valid.
  virtual DnsTransactionFactory* GetTransactionFactory() = 0;

  // Returns NULL if the current config is not valid.
  virtual AddressSorter* GetAddressSorter() = 0;

  // Does nothing if the current config is not valid.
  virtual void ApplyPersistentData(const base::Value& data) = 0;

  // Returns std::unique_ptr<const Value>(NULL) if the current config is not
  // valid.
  virtual std::unique_ptr<const base::Value> GetPersistentData() const = 0;

  // Creates default client.
  static std::unique_ptr<DnsClient> CreateClient(NetLog* net_log);

  // Creates a client for testing.  Allows using a mock ClientSocketFactory and
  // a deterministic random number generator. |socket_factory| must outlive
  // the returned DnsClient.
  static std::unique_ptr<DnsClient> CreateClientForTesting(
      NetLog* net_log,
      ClientSocketFactory* socket_factory,
      const RandIntCallback& rand_int_callback);
};

}  // namespace net

#endif  // NET_DNS_DNS_CLIENT_H_
