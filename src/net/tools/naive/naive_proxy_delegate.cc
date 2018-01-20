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
#include "net/tools/naive/padding_utils.h"

namespace net {

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

base::expected<HttpRequestHeaders, Error>
NaiveProxyDelegate::OnBeforeTunnelRequest(
    const ProxyChain& proxy_chain,
    size_t chain_index,
    OnBeforeTunnelRequestCallback callback) {
  HttpRequestHeaders extra_headers;
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (proxy_chain.is_direct()) {
    return extra_headers;
  }
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(chain_index);
  if (proxy_server.is_socks()) {
    return extra_headers;
  }

  // Only the last server is attempted for padding
  // because proxy chaining will corrupt the padding.
  if (chain_index != proxy_chain.length() - 1) {
    return extra_headers;
  }

  // Sends client-side padding header regardless of server support
  std::string padding(base::RandInt(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(),
                          base::as_writable_byte_span(padding));
  extra_headers.SetHeader(kPaddingHeader, padding);

  // Enables Fast Open in H2/H3 proxy client socket once the state of server
  // padding support is known.
  if (padding_type_by_server_[proxy_server].has_value()) {
    extra_headers.SetHeader("fastopen", "1");
  }
  extra_headers.MergeFrom(extra_headers_);

  return extra_headers;
}

void NaiveProxyDelegate::OnBeforePreambleRequest(
    const ProxyChain& proxy_chain,
    size_t proxy_index,
    size_t preamble_index,
    HttpRequestHeaders& header) const {
  CHECK(!proxy_chain.is_direct());
  CHECK_EQ(proxy_index, proxy_chain.length() - 1);
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(proxy_index);
  CHECK(proxy_server.is_secure_http_like());

  const auto it = preamble_request_headers_by_server_.find(proxy_server);
  if (it == preamble_request_headers_by_server_.end()) {
    return;
  }
  const std::vector<HttpRequestHeaders>& headers = it->second;
  if (preamble_index >= headers.size()) {
    return;
  }
  header = headers[preamble_index];
}

void NaiveProxyDelegate::OnPreambleHeadersReceived(
    const ProxyChain& proxy_chain,
    size_t proxy_index,
    size_t preamble_index,
    scoped_refptr<HttpResponseHeaders> response_headers) {
  CHECK(!proxy_chain.is_direct());
  CHECK_EQ(proxy_index, proxy_chain.length() - 1);
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(proxy_index);
  CHECK(proxy_server.is_secure_http_like());

  std::vector<scoped_refptr<HttpResponseHeaders>>& headers =
      preamble_response_headers_by_server_[proxy_server];
  if (preamble_index >= headers.size()) {
    headers.resize(preamble_index + 1);
  }
  headers[preamble_index] = response_headers;
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
    const HttpResponseHeaders& response_headers,
    CompletionOnceCallback callback) {
  // Not possible to negotiate padding capability given the underlying
  // protocols.
  if (proxy_chain.is_direct()) {
    return OK;
  }
  const ProxyServer& proxy_server = proxy_chain.GetProxyServer(chain_index);
  if (proxy_server.is_socks()) {
    return OK;
  }

  // Only the last server is attempted for padding
  // because proxy chaining will corrupt the padding.
  if (chain_index != proxy_chain.length() - 1) {
    return OK;
  }

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
  if (proxy_chain.is_direct()) {
    return PaddingType::kNone;
  }
  if (proxy_chain.Last().is_socks()) {
    return PaddingType::kNone;
  }
  return padding_type_by_server_[proxy_chain.Last()];
}

void NaiveProxyDelegate::SetPreambleRequestHeaders(
    const ProxyServer& proxy_server,
    size_t preamble_index,
    const HttpRequestHeaders& header) {
  std::vector<HttpRequestHeaders>& headers =
      preamble_request_headers_by_server_[proxy_server];
  if (preamble_index >= headers.size()) {
    headers.resize(preamble_index + 1);
  }
  headers[preamble_index] = header;
}

const HttpResponseHeaders* NaiveProxyDelegate::GetPreambleResponseHeaders(
    const ProxyServer& proxy_server,
    size_t preamble_index) const {
  const auto it = preamble_response_headers_by_server_.find(proxy_server);
  if (it == preamble_response_headers_by_server_.end()) {
    return nullptr;
  }
  const std::vector<scoped_refptr<HttpResponseHeaders>>& headers = it->second;
  if (preamble_index >= headers.size()) {
    return nullptr;
  }
  return headers[preamble_index].get();
}

}  // namespace net
