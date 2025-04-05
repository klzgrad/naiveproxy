// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_

#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/tools/naive/naive_padding_socket.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy_delegate.h"

namespace net {

class ClientSocketHandle;
class DrainableIOBuffer;
class HttpNetworkSession;
class IOBuffer;
class NetLogWithSource;
class ProxyInfo;
class StreamSocket;
struct NetworkTrafficAnnotationTag;
struct SSLConfig;
class RedirectResolver;
class NetworkAnonymizationKey;

class NaiveConnection {
 public:
  using TimeFunc = base::TimeTicks (*)();

  NaiveConnection(
      unsigned int id,
      ClientProtocol protocol,
      std::unique_ptr<PaddingDetectorDelegate> padding_detector_delegate,
      const ProxyInfo& proxy_info,
      RedirectResolver* resolver,
      HttpNetworkSession* session,
      const NetworkAnonymizationKey& network_anonymization_key,
      const NetLogWithSource& net_log,
      std::unique_ptr<StreamSocket> accepted_socket,
      const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveConnection();
  NaiveConnection(const NaiveConnection&) = delete;
  NaiveConnection& operator=(const NaiveConnection&) = delete;

  unsigned int id() const { return id_; }
  int Connect(CompletionOnceCallback callback);
  void Disconnect();
  int Run(CompletionOnceCallback callback);

 private:
  enum State {
    STATE_CONNECT_CLIENT,
    STATE_CONNECT_CLIENT_COMPLETE,
    STATE_CONNECT_SERVER,
    STATE_CONNECT_SERVER_COMPLETE,
    STATE_NONE,
  };

  enum PaddingState {
    STATE_READ_PAYLOAD_LENGTH_1,
    STATE_READ_PAYLOAD_LENGTH_2,
    STATE_READ_PADDING_LENGTH,
    STATE_READ_PAYLOAD,
    STATE_READ_PADDING,
  };

  void DoCallback(int result);
  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoConnectClient();
  int DoConnectClientComplete(int result);
  int DoConnectServer();
  int DoConnectServerComplete(int result);
  void Pull(Direction from, Direction to);
  void Push(Direction from, Direction to, int size);
  void Disconnect(Direction side);
  bool IsConnected(Direction side);
  void OnBothDisconnected();
  void OnPullError(Direction from, Direction to, int error);
  void OnPushError(Direction from, Direction to, int error);
  void OnPullComplete(Direction from, Direction to, int result);
  void OnPushComplete(Direction from, Direction to, int result);

  unsigned int id_;
  ClientProtocol protocol_;
  std::unique_ptr<PaddingDetectorDelegate> padding_detector_delegate_;
  const ProxyInfo& proxy_info_;
  RedirectResolver* resolver_;
  HttpNetworkSession* session_;
  const NetworkAnonymizationKey& network_anonymization_key_;
  const NetLogWithSource& net_log_;

  CompletionRepeatingCallback io_callback_;
  CompletionOnceCallback connect_callback_;
  CompletionOnceCallback run_callback_;

  State next_state_;

  std::unique_ptr<StreamSocket> client_socket_;
  std::unique_ptr<ClientSocketHandle> server_socket_handle_;

  std::unique_ptr<NaivePaddingSocket> sockets_[kNumDirections];
  scoped_refptr<IOBuffer> read_buffers_[kNumDirections];
  scoped_refptr<DrainableIOBuffer> write_buffers_[kNumDirections];
  int errors_[kNumDirections];
  bool write_pending_[kNumDirections];
  int bytes_passed_without_yielding_[kNumDirections];
  base::TimeTicks yield_after_time_[kNumDirections];

  bool early_pull_pending_;
  bool can_push_to_server_;
  int early_pull_result_;

  bool full_duplex_;

  TimeFunc time_func_;

  // Traffic annotation for socket control.
  const NetworkTrafficAnnotationTag& traffic_annotation_;

  base::WeakPtrFactory<NaiveConnection> weak_ptr_factory_{this};
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_
