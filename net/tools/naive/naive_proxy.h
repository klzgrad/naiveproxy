// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_PROXY_H_
#define NET_TOOLS_NAIVE_NAIVE_PROXY_H_

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_repeating_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/third_party/quic/tools/quic_simple_server_backend.h"
#include "net/tools/naive/naive_connection.h"

namespace spdy {
class SpdyHeaderBlock;
}  // namespace spdy

namespace quic {
class QuicNaiveServerStream;
class QuicHeaderList;
}  // namespace quic

namespace net {

class ClientSocketHandle;
class HttpNetworkSession;
class NaiveConnection;
class ServerSocket;
class StreamSocket;
struct NetworkTrafficAnnotationTag;

class NaiveProxy : public quic::QuicSimpleServerBackend {
 public:
  NaiveProxy(std::unique_ptr<ServerSocket> server_socket,
             NaiveConnection::Protocol protocol,
             bool use_proxy,
             HttpNetworkSession* session,
             const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveProxy() override;

  // Implements quic::QuicSimpleServerBackend
  bool InitializeBackend(const std::string& backend_url) override;
  bool IsBackendInitialized() const override;
  void FetchResponseFromBackend(
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& incoming_body,
      quic::QuicSimpleServerBackend::RequestHandler* quic_stream) override;
  void CloseBackendResponseStream(
      quic::QuicSimpleServerBackend::RequestHandler* quic_stream) override;

  void OnReadHeaders(quic::QuicNaiveServerStream* stream,
                     const quic::QuicHeaderList& header_list) override;
  void OnReadData(quic::QuicNaiveServerStream* stream) override;
  void OnDeleteStream(quic::QuicNaiveServerStream* stream) override;

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

  NaiveConnection* FindConnection(int connection_id);

  std::unique_ptr<ServerSocket> listen_socket_;
  NaiveConnection::Protocol protocol_;
  bool use_proxy_;
  HttpNetworkSession* session_;
  NetLogWithSource net_log_;

  unsigned int last_id_;

  std::unique_ptr<StreamSocket> accepted_socket_;

  std::map<unsigned int, std::unique_ptr<NaiveConnection>> connection_by_id_;

  const NetworkTrafficAnnotationTag& traffic_annotation_;

  base::WeakPtrFactory<NaiveProxy> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NaiveProxy);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROXY_H_
