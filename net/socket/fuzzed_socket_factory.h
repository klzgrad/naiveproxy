// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_FUZZED_SOCKET_FACTORY_H_
#define NET_SOCKET_FUZZED_SOCKET_FACTORY_H_

#include <memory>

#include "base/macros.h"
#include "net/socket/client_socket_factory.h"

namespace base {
class FuzzedDataProvider;
}

namespace net {

// A socket factory that creates FuzzedSockets that share the same
// FuzzedDataProvider. To behave consistently, the read operations on all
// sockets must be the same, and in the same order (both on each socket, and
// between sockets).
//
// Currently doesn't support SSL sockets - just returns sockets that
// synchronously fail to connect when trying to create either type of socket.
// TODO(mmenke): Add support for ssl sockets.
// TODO(mmenke): add fuzzing for generation of valid cryptographically signed
// messages.
class FuzzedSocketFactory : public ClientSocketFactory {
 public:
  // |data_provider| must outlive the FuzzedSocketFactory, and all sockets it
  // creates. Other objects can also continue to consume |data_provider|, as
  // long as their calls into it are made on the CLientSocketFactory's thread
  // and the calls are deterministic.
  explicit FuzzedSocketFactory(base::FuzzedDataProvider* data_provider);
  ~FuzzedSocketFactory() override;

  std::unique_ptr<DatagramClientSocket> CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      const RandIntCallback& rand_int_cb,
      NetLog* net_log,
      const NetLogSource& source) override;

  std::unique_ptr<StreamSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log,
      const NetLogSource& source) override;

  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      const SSLClientSocketContext& context) override;

  void ClearSSLSessionCache() override;

  // Sets whether Connect()ions on returned sockets can be asynchronously
  // delayed or outright fail. Defaults to true.
  void set_fuzz_connect_result(bool v) { fuzz_connect_result_ = v; }

 private:
  base::FuzzedDataProvider* data_provider_;
  bool fuzz_connect_result_;

  DISALLOW_COPY_AND_ASSIGN(FuzzedSocketFactory);
};

}  // namespace net

#endif  // NET_SOCKET_FUZZED_SOCKET_FACTORY_H_
