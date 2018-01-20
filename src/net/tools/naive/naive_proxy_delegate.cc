// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_proxy_delegate.h"

#include <string>

#include "base/logging.h"
#include "base/rand_util.h"
#include "net/base/proxy_string_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/third_party/quiche/src/quiche/spdy/core/hpack/hpack_constants.h"

namespace net {
namespace {
bool g_nonindex_codes_initialized;
uint8_t g_nonindex_codes[17];
}  // namespace

void InitializeNonindexCodes() {
  if (g_nonindex_codes_initialized)
    return;
  g_nonindex_codes_initialized = true;
  unsigned i = 0;
  for (const auto& symbol : spdy::HpackHuffmanCodeVector()) {
    if (symbol.id >= 0x20 && symbol.id <= 0x7f && symbol.length >= 8) {
      g_nonindex_codes[i++] = symbol.id;
      if (i >= sizeof(g_nonindex_codes))
        break;
    }
  }
  CHECK(i == sizeof(g_nonindex_codes));
}

void FillNonindexHeaderValue(uint64_t unique_bits, char* buf, int len) {
  DCHECK(g_nonindex_codes_initialized);
  int first = len < 16 ? len : 16;
  for (int i = 0; i < first; i++) {
    buf[i] = g_nonindex_codes[unique_bits & 0b1111];
    unique_bits >>= 4;
  }
  for (int i = first; i < len; i++) {
    buf[i] = g_nonindex_codes[16];
  }
}

NaiveProxyDelegate::NaiveProxyDelegate(const HttpRequestHeaders& extra_headers)
    : extra_headers_(extra_headers) {
  InitializeNonindexCodes();
}

NaiveProxyDelegate::~NaiveProxyDelegate() = default;

void NaiveProxyDelegate::OnBeforeTunnelRequest(
    const ProxyServer& proxy_server,
    HttpRequestHeaders* extra_headers) {
  if (proxy_server.is_direct() || proxy_server.is_socks())
    return;

  // Sends client-side padding header regardless of server support
  std::string padding(base::RandInt(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(), &padding[0], padding.size());
  extra_headers->SetHeader("padding", padding);

  // Enables Fast Open in H2/H3 proxy client socket once the state of server
  // padding support is known.
  if (padding_state_by_server_[proxy_server] != PaddingSupport::kUnknown) {
    extra_headers->SetHeader("fastopen", "1");
  }
  extra_headers->MergeFrom(extra_headers_);
}

Error NaiveProxyDelegate::OnTunnelHeadersReceived(
    const ProxyServer& proxy_server,
    const HttpResponseHeaders& response_headers) {
  if (proxy_server.is_direct() || proxy_server.is_socks())
    return OK;

  // Detects server padding support, even if it changes dynamically.
  bool padding = response_headers.HasHeader("padding");
  auto new_state =
      padding ? PaddingSupport::kCapable : PaddingSupport::kIncapable;
  auto& padding_state = padding_state_by_server_[proxy_server];
  if (padding_state == PaddingSupport::kUnknown || padding_state != new_state) {
    LOG(INFO) << "Padding capability of " << ProxyServerToProxyUri(proxy_server)
              << (padding ? " detected" : " undetected");
  }
  padding_state = new_state;
  return OK;
}

PaddingSupport NaiveProxyDelegate::GetProxyServerPaddingSupport(
    const ProxyServer& proxy_server) {
  // Not possible to detect padding capability given underlying protocol.
  if (proxy_server.is_direct() || proxy_server.is_socks())
    return PaddingSupport::kIncapable;

  return padding_state_by_server_[proxy_server];
}

PaddingDetectorDelegate::PaddingDetectorDelegate(
    NaiveProxyDelegate* naive_proxy_delegate,
    const ProxyServer& proxy_server,
    ClientProtocol client_protocol)
    : naive_proxy_delegate_(naive_proxy_delegate),
      proxy_server_(proxy_server),
      client_protocol_(client_protocol),
      detected_client_padding_support_(PaddingSupport::kUnknown),
      cached_server_padding_support_(PaddingSupport::kUnknown) {}

PaddingDetectorDelegate::~PaddingDetectorDelegate() = default;

bool PaddingDetectorDelegate::IsPaddingSupportKnown() {
  auto c = GetClientPaddingSupport();
  auto s = GetServerPaddingSupport();
  return c != PaddingSupport::kUnknown && s != PaddingSupport::kUnknown;
}

Direction PaddingDetectorDelegate::GetPaddingDirection() {
  auto c = GetClientPaddingSupport();
  auto s = GetServerPaddingSupport();
  // Padding support must be already detected at this point.
  CHECK_NE(c, PaddingSupport::kUnknown);
  CHECK_NE(s, PaddingSupport::kUnknown);
  if (c == PaddingSupport::kCapable && s == PaddingSupport::kIncapable) {
    return kServer;
  }
  if (c == PaddingSupport::kIncapable && s == PaddingSupport::kCapable) {
    return kClient;
  }
  return kNone;
}

void PaddingDetectorDelegate::SetClientPaddingSupport(
    PaddingSupport padding_support) {
  detected_client_padding_support_ = padding_support;
}

PaddingSupport PaddingDetectorDelegate::GetClientPaddingSupport() {
  // Not possible to detect padding capability given underlying protocol.
  if (client_protocol_ == ClientProtocol::kSocks5) {
    return PaddingSupport::kIncapable;
  } else if (client_protocol_ == ClientProtocol::kRedir) {
    return PaddingSupport::kIncapable;
  }

  return detected_client_padding_support_;
}

PaddingSupport PaddingDetectorDelegate::GetServerPaddingSupport() {
  if (cached_server_padding_support_ != PaddingSupport::kUnknown)
    return cached_server_padding_support_;
  cached_server_padding_support_ =
      naive_proxy_delegate_->GetProxyServerPaddingSupport(proxy_server_);
  return cached_server_padding_support_;
}

}  // namespace net
