// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKS_CONNECT_JOB_H_
#define NET_SOCKET_SOCKS_CONNECT_JOB_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/dns/host_resolver.h"
#include "net/socket/connect_job.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class ClientSocketHandle;
class HostPortPair;
class NetLog;
class StreamSocket;
class TransportClientSocketPool;
class TransportSocketParams;

class NET_EXPORT_PRIVATE SOCKSSocketParams
    : public base::RefCounted<SOCKSSocketParams> {
 public:
  SOCKSSocketParams(const scoped_refptr<TransportSocketParams>& proxy_server,
                    bool socks_v5,
                    const HostPortPair& host_port_pair,
                    const NetworkTrafficAnnotationTag& traffic_annotation);

  const scoped_refptr<TransportSocketParams>& transport_params() const {
    return transport_params_;
  }
  const HostResolver::RequestInfo& destination() const { return destination_; }
  bool is_socks_v5() const { return socks_v5_; }

  const NetworkTrafficAnnotationTag traffic_annotation() {
    return traffic_annotation_;
  }

 private:
  friend class base::RefCounted<SOCKSSocketParams>;
  ~SOCKSSocketParams();

  // The transport (likely TCP) connection must point toward the proxy server.
  const scoped_refptr<TransportSocketParams> transport_params_;
  // This is the HTTP destination.
  HostResolver::RequestInfo destination_;
  const bool socks_v5_;

  NetworkTrafficAnnotationTag traffic_annotation_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSSocketParams);
};

// SOCKSConnectJob handles the handshake to a socks server after setting up
// an underlying transport socket.
class NET_EXPORT_PRIVATE SOCKSConnectJob : public ConnectJob {
 public:
  SOCKSConnectJob(const std::string& group_name,
                  RequestPriority priority,
                  const SocketTag& socket_tag,
                  bool respect_limits,
                  const scoped_refptr<SOCKSSocketParams>& params,
                  TransportClientSocketPool* transport_pool,
                  HostResolver* host_resolver,
                  Delegate* delegate,
                  NetLog* net_log);
  ~SOCKSConnectJob() override;

  // ConnectJob methods.
  LoadState GetLoadState() const override;

  // Returns the connection timeout used by SOCKSConnectJobs.
  static base::TimeDelta ConnectionTimeout();

 private:
  enum State {
    STATE_TRANSPORT_CONNECT,
    STATE_TRANSPORT_CONNECT_COMPLETE,
    STATE_SOCKS_CONNECT,
    STATE_SOCKS_CONNECT_COMPLETE,
    STATE_NONE,
  };

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  int DoTransportConnect();
  int DoTransportConnectComplete(int result);
  int DoSOCKSConnect();
  int DoSOCKSConnectComplete(int result);

  // Begins the transport connection and the SOCKS handshake.  Returns OK on
  // success and ERR_IO_PENDING if it cannot immediately service the request.
  // Otherwise, it returns a net error code.
  int ConnectInternal() override;

  void ChangePriorityInternal(RequestPriority priority) override;

  scoped_refptr<SOCKSSocketParams> socks_params_;
  TransportClientSocketPool* const transport_pool_;
  HostResolver* const resolver_;

  State next_state_;
  std::unique_ptr<ClientSocketHandle> transport_socket_handle_;
  std::unique_ptr<StreamSocket> socket_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSConnectJob);
};

}  // namespace net

#endif  // NET_SOCKET_SOCKS_CONNECT_JOB_H_
