// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "net/tools/naive/naive_proxy_delegate.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "net/base/proxy_string_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/third_party/quiche/src/quiche/http2/hpack/hpack_constants.h"

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

NaiveProxyDelegate::NaiveProxyDelegate(
    const HttpRequestHeaders& extra_headers,
    const std::vector<PaddingType>& supported_padding_types)
    : extra_headers_(extra_headers) {
  InitializeNonindexCodes();

  std::vector<std::string_view> padding_type_strs;
  for (PaddingType padding_type : supported_padding_types) {
    padding_type_strs.push_back(ToString(padding_type));
  }
  extra_headers_.SetHeader(kPaddingTypeRequestHeader,
                           base::JoinString(padding_type_strs, ", "));
}

NaiveProxyDelegate::~NaiveProxyDelegate() = default;

Error NaiveProxyDelegate::OnBeforeTunnelRequest(
    const ProxyChain& proxy_chain,
    size_t chain_index,
    HttpRequestHeaders* extra_headers) {
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (proxy_chain.is_direct())
    return OK;
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(chain_index);
  if (proxy_server.is_socks())
    return OK;

  // Only the last server is attempted for padding
  // because proxy chaining will corrupt the padding.
  if (chain_index != proxy_chain.length() - 1)
    return OK;

  // Sends client-side padding header regardless of server support
  std::string padding(base::RandInt(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(), &padding[0], padding.size());
  extra_headers->SetHeader(kPaddingHeader, padding);

  // Enables Fast Open in H2/H3 proxy client socket once the state of server
  // padding support is known.
  if (padding_type_by_server_[proxy_server].has_value()) {
    extra_headers->SetHeader("fastopen", "1");
  }
  extra_headers->MergeFrom(extra_headers_);

  return OK;
}

std::optional<PaddingType> NaiveProxyDelegate::ParsePaddingHeaders(
    const HttpResponseHeaders& headers) {
  bool has_padding = headers.HasHeader(kPaddingHeader);
  std::optional<std::string> padding_type_reply =
      headers.GetNormalizedHeader(kPaddingTypeReplyHeader);

  if (!padding_type_reply.has_value()) {
    // Backward compatibility with before kVariant1 when the padding-version
    // header does not exist.
    if (has_padding) {
      return PaddingType::kVariant1;
    } else {
      return PaddingType::kNone;
    }
  }
  std::optional<PaddingType> padding_type =
      ParsePaddingType(*padding_type_reply);
  if (!padding_type.has_value()) {
    LOG(ERROR) << "Received invalid padding type: " << *padding_type_reply;
  }
  return padding_type;
}

Error NaiveProxyDelegate::OnTunnelHeadersReceived(
    const ProxyChain& proxy_chain,
    size_t chain_index,
    const HttpResponseHeaders& response_headers) {
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (proxy_chain.is_direct())
    return OK;
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(chain_index);
  if (proxy_server.is_socks())
    return OK;

  // Only the last server is attempted for padding
  // because proxy chaining will corrupt the padding.
  if (chain_index != proxy_chain.length() - 1)
    return OK;

  // Detects server padding support, even if it changes dynamically.
  std::optional<PaddingType> new_padding_type =
      ParsePaddingHeaders(response_headers);
  if (!new_padding_type.has_value()) {
    return ERR_INVALID_RESPONSE;
  }
  std::optional<PaddingType>& padding_type =
      padding_type_by_server_[proxy_server];
  if (!padding_type.has_value() || padding_type != new_padding_type) {
    LOG(INFO) << ProxyServerToProxyUri(proxy_server)
              << " negotiated padding type: "
              << ToReadableString(*new_padding_type);
    padding_type = new_padding_type;
  }
  return OK;
}

std::optional<PaddingType> NaiveProxyDelegate::GetProxyChainPaddingType(
    const ProxyChain& proxy_chain) {
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (proxy_chain.is_direct())
    return PaddingType::kNone;
  if (proxy_chain.Last().is_socks())
    return PaddingType::kNone;
  return padding_type_by_server_[proxy_chain.Last()];
}

PaddingDetectorDelegate::PaddingDetectorDelegate(
    NaiveProxyDelegate* naive_proxy_delegate,
    const ProxyChain& proxy_chain,
    ClientProtocol client_protocol)
    : naive_proxy_delegate_(naive_proxy_delegate),
      proxy_chain_(proxy_chain),
      client_protocol_(client_protocol) {}

PaddingDetectorDelegate::~PaddingDetectorDelegate() = default;

void PaddingDetectorDelegate::SetClientPaddingType(PaddingType padding_type) {
  detected_client_padding_type_ = padding_type;
}

std::optional<PaddingType> PaddingDetectorDelegate::GetClientPaddingType() {
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (client_protocol_ == ClientProtocol::kSocks5) {
    return PaddingType::kNone;
  } else if (client_protocol_ == ClientProtocol::kRedir) {
    return PaddingType::kNone;
  }

  return detected_client_padding_type_;
}

std::optional<PaddingType> PaddingDetectorDelegate::GetServerPaddingType() {
  if (cached_server_padding_type_.has_value())
    return cached_server_padding_type_;
  cached_server_padding_type_ =
      naive_proxy_delegate_->GetProxyChainPaddingType(proxy_chain_);
  return cached_server_padding_type_;
}

}  // namespace net
