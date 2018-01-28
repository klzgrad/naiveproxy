// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/fuzzed_socket_factory.h"

#include "base/logging.h"
#include "base/test/fuzzed_data_provider.h"
#include "net/base/address_list.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_change_notifier.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/fuzzed_datagram_client_socket.h"
#include "net/socket/fuzzed_socket.h"
#include "net/socket/ssl_client_socket.h"

namespace net {

class NetLog;

namespace {

// SSLClientSocket implementation that always fails to connect.
class FailingSSLClientSocket : public SSLClientSocket {
 public:
  FailingSSLClientSocket() {}
  ~FailingSSLClientSocket() override {}

  // Socket implementation:
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  int SetReceiveBufferSize(int32_t size) override { return OK; }
  int SetSendBufferSize(int32_t size) override { return OK; }

  // StreamSocket implementation:
  int Connect(const CompletionCallback& callback) override {
    return ERR_FAILED;
  }

  void Disconnect() override {}
  bool IsConnected() const override { return false; }
  bool IsConnectedAndIdle() const override { return false; }

  int GetPeerAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  int GetLocalAddress(IPEndPoint* address) const override {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  const NetLogWithSource& NetLog() const override { return net_log_; }

  void SetSubresourceSpeculation() override {}
  void SetOmniboxSpeculation() override {}

  bool WasEverUsed() const override { return false; }

  void EnableTCPFastOpenIfSupported() override {}

  bool WasAlpnNegotiated() const override { return false; }

  NextProto GetNegotiatedProtocol() const override { return kProtoUnknown; }

  bool GetSSLInfo(SSLInfo* ssl_info) override { return false; }

  void GetConnectionAttempts(ConnectionAttempts* out) const override {
    out->clear();
  }

  void ClearConnectionAttempts() override {}

  void AddConnectionAttempts(const ConnectionAttempts& attempts) override {}

  int64_t GetTotalReceivedBytes() const override { return 0; }

  // SSLSocket implementation:
  int ExportKeyingMaterial(const base::StringPiece& label,
                           bool has_context,
                           const base::StringPiece& context,
                           unsigned char* out,
                           unsigned int outlen) override {
    NOTREACHED();
    return 0;
  }

  // SSLClientSocket implementation:
  void GetSSLCertRequestInfo(SSLCertRequestInfo* cert_request_info) override {}

  ChannelIDService* GetChannelIDService() const override {
    NOTREACHED();
    return nullptr;
  }

  Error GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                 TokenBindingType tb_type,
                                 std::vector<uint8_t>* out) override {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  crypto::ECPrivateKey* GetChannelIDKey() const override {
    NOTREACHED();
    return nullptr;
  }

 private:
  NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(FailingSSLClientSocket);
};

}  // namespace

FuzzedSocketFactory::FuzzedSocketFactory(
    base::FuzzedDataProvider* data_provider)
    : data_provider_(data_provider), fuzz_connect_result_(true) {}

FuzzedSocketFactory::~FuzzedSocketFactory() {}

std::unique_ptr<DatagramClientSocket>
FuzzedSocketFactory::CreateDatagramClientSocket(
    DatagramSocket::BindType bind_type,
    const RandIntCallback& rand_int_cb,
    NetLog* net_log,
    const NetLogSource& source) {
  return std::make_unique<FuzzedDatagramClientSocket>(data_provider_);
}

std::unique_ptr<StreamSocket> FuzzedSocketFactory::CreateTransportClientSocket(
    const AddressList& addresses,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetLog* net_log,
    const NetLogSource& source) {
  std::unique_ptr<FuzzedSocket> socket(
      new FuzzedSocket(data_provider_, net_log));
  socket->set_fuzz_connect_result(fuzz_connect_result_);
  // Just use the first address.
  socket->set_remote_address(*addresses.begin());
  return std::move(socket);
}

std::unique_ptr<SSLClientSocket> FuzzedSocketFactory::CreateSSLClientSocket(
    std::unique_ptr<ClientSocketHandle> transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    const SSLClientSocketContext& context) {
  return std::make_unique<FailingSSLClientSocket>();
}

void FuzzedSocketFactory::ClearSSLSessionCache() {}

}  // namespace net
