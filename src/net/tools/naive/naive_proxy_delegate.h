// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_PROXY_DELEGATE_H_
#define NET_TOOLS_NAIVE_NAIVE_PROXY_DELEGATE_H_

#include <cstdint>
#include <map>
#include <string>

#include "base/strings/string_piece.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_delegate.h"
#include "net/base/proxy_server.h"
#include "net/proxy_resolution/proxy_retry_info.h"
#include "net/tools/naive/naive_protocol.h"
#include "url/gurl.h"

namespace net {

void InitializeNonindexCodes();
// |unique_bits| SHOULD have relatively unique values.
void FillNonindexHeaderValue(uint64_t unique_bits, char* buf, int len);

class ProxyInfo;
class HttpRequestHeaders;
class HttpResponseHeaders;

enum class PaddingSupport {
  kUnknown = 0,
  kCapable,
  kIncapable,
};

class NaiveProxyDelegate : public ProxyDelegate {
 public:
  explicit NaiveProxyDelegate(const HttpRequestHeaders& extra_headers);
  ~NaiveProxyDelegate() override;

  void OnResolveProxy(const GURL& url,
                      const std::string& method,
                      const ProxyRetryInfoMap& proxy_retry_info,
                      ProxyInfo* result) override {}
  void OnFallback(const ProxyServer& bad_proxy, int net_error) override {}

  // This only affects h2 proxy client socket.
  void OnBeforeTunnelRequest(const ProxyServer& proxy_server,
                             HttpRequestHeaders* extra_headers) override;

  Error OnTunnelHeadersReceived(
      const ProxyServer& proxy_server,
      const HttpResponseHeaders& response_headers) override;

  PaddingSupport GetProxyServerPaddingSupport(const ProxyServer& proxy_server);

 private:
  const HttpRequestHeaders& extra_headers_;
  std::map<ProxyServer, PaddingSupport> padding_state_by_server_;
};

class ClientPaddingDetectorDelegate {
 public:
  virtual ~ClientPaddingDetectorDelegate() = default;

  virtual void SetClientPaddingSupport(PaddingSupport padding_support) = 0;
};

class PaddingDetectorDelegate : public ClientPaddingDetectorDelegate {
 public:
  PaddingDetectorDelegate(NaiveProxyDelegate* naive_proxy_delegate,
                          const ProxyServer& proxy_server,
                          ClientProtocol client_protocol);
  ~PaddingDetectorDelegate() override;

  bool IsPaddingSupportKnown();
  Direction GetPaddingDirection();
  void SetClientPaddingSupport(PaddingSupport padding_support) override;

 private:
  PaddingSupport GetClientPaddingSupport();
  PaddingSupport GetServerPaddingSupport();

  NaiveProxyDelegate* naive_proxy_delegate_;
  const ProxyServer& proxy_server_;
  ClientProtocol client_protocol_;

  PaddingSupport detected_client_padding_support_;
  // The result is only cached during one connection, so it's still dynamically
  // updated in the following connections after server changes support.
  PaddingSupport cached_server_padding_support_;
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROXY_DELEGATE_H_
