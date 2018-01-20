// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_PROXY_H_
#define NET_TOOLS_NAIVE_NAIVE_PROXY_H_

#include <map>
#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/network_isolation_key.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/ssl/ssl_config.h"
#include "net/tools/naive/naive_connection.h"
#include "net/tools/naive/naive_protocol.h"

namespace net {

class ClientSocketHandle;
class HttpNetworkSession;
class NaiveConnection;
class ServerSocket;
class StreamSocket;
struct NetworkTrafficAnnotationTag;
class RedirectResolver;

class NaiveProxy {
 public:
  NaiveProxy(std::unique_ptr<ServerSocket> server_socket,
             ClientProtocol protocol,
             const std::string& listen_user,
             const std::string& listen_pass,
             int concurrency,
             RedirectResolver* resolver,
             HttpNetworkSession* session,
             const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveProxy();
  NaiveProxy(const NaiveProxy&) = delete;
  NaiveProxy& operator=(const NaiveProxy&) = delete;

 private:
  void DoAcceptLoop();
  void OnAcceptComplete(int result);
  void HandleAcceptResult(int result);

  void DoConnect();
  void OnConnectComplete(unsigned int connection_id, int result);
  void HandleConnectResult(NaiveConnection* connection, int result);

  void DoRun(NaiveConnection* connection);
  void OnRunComplete(unsigned int connection_id, int result);
  void HandleRunResult(NaiveConnection* connection, int result);

  void Close(unsigned int connection_id, int reason);

  NaiveConnection* FindConnection(unsigned int connection_id);

  std::unique_ptr<ServerSocket> listen_socket_;
  ClientProtocol protocol_;
  std::string listen_user_;
  std::string listen_pass_;
  int concurrency_;
  ProxyInfo proxy_info_;
  SSLConfig server_ssl_config_;
  SSLConfig proxy_ssl_config_;
  RedirectResolver* resolver_;
  HttpNetworkSession* session_;
  NetLogWithSource net_log_;

  unsigned int last_id_;

  std::unique_ptr<StreamSocket> accepted_socket_;

  std::vector<NetworkAnonymizationKey> network_anonymization_keys_;

  std::map<unsigned int, std::unique_ptr<NaiveConnection>> connection_by_id_;

  const NetworkTrafficAnnotationTag& traffic_annotation_;

  base::WeakPtrFactory<NaiveProxy> weak_ptr_factory_{this};
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROXY_H_
