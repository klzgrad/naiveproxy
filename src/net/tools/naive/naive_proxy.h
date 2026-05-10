// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_PROXY_H_
#define NET_TOOLS_NAIVE_NAIVE_PROXY_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/network_isolation_key.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/ssl/ssl_config.h"
#include "net/tools/naive/naive_connection.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/preamble_getter.h"

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
             int tunnel_timeout,
             int idle_timeout,
             RedirectResolver* resolver,
             HttpNetworkSession* session,
             const NetworkTrafficAnnotationTag& traffic_annotation,
             const std::vector<PaddingType>& supported_padding_types);
  ~NaiveProxy();
  NaiveProxy(const NaiveProxy&) = delete;
  NaiveProxy& operator=(const NaiveProxy&) = delete;

 private:
  enum class State {
    kAccept,
    kAcceptComplete,
    kPreamble,
    kPreambleComplete,
    kConnect,
    kNone,
  };

  struct Tunnel {
    Tunnel();
    ~Tunnel();

    NetworkAnonymizationKey nak = NetworkAnonymizationKey::CreateTransient();
    base::TimeTicks deadline;
    std::unique_ptr<PreambleGetter> url_getter;
  };

  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoAccept();
  int DoAcceptComplete(int result);
  int DoPreamble();
  int DoPreambleComplete(int result);
  int DoConnect();
  void OnConnectComplete(unsigned int connection_id, int result);
  void HandleConnectResult(NaiveConnection* connection, int result);

  void DoRun(NaiveConnection* connection);
  void OnRunComplete(unsigned int connection_id, int result);
  void HandleRunResult(NaiveConnection* connection, int result);

  void Close(unsigned int connection_id, int reason);

  NaiveConnection* FindConnection(unsigned int connection_id);
  NaiveProxyDelegate* naive_proxy_delegate() const;
  bool WillCreateSession(const NetworkAnonymizationKey& nak) const;
  void CleanUpIdleConnections();

  std::unique_ptr<ServerSocket> listen_socket_;
  ClientProtocol protocol_;
  std::string listen_user_;
  std::string listen_pass_;
  int concurrency_;
  base::TimeDelta tunnel_timeout_;
  base::TimeDelta idle_timeout_;
  ProxyInfo proxy_info_;
  RedirectResolver* resolver_;
  HttpNetworkSession* session_;
  NetLogWithSource net_log_;

  unsigned int next_id_;

  State next_state_;
  CompletionRepeatingCallback io_callback_;
  std::unique_ptr<StreamSocket> accepted_socket_;

  std::vector<Tunnel> tunnels_;

  std::map<unsigned int, std::unique_ptr<NaiveConnection>> connection_by_id_;

  const NetworkTrafficAnnotationTag& traffic_annotation_;

  std::vector<PaddingType> supported_padding_types_;

  base::RepeatingTimer cleanup_timer_;

  base::WeakPtrFactory<NaiveProxy> weak_ptr_factory_{this};
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROXY_H_
