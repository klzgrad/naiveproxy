// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/websocket_tunnel_protocol.h"

#include <string>

#include "base/base64.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/embedder_support/user_agent_utils.h"
#include "net/base/ip_address.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/padding_utils.h"

namespace net {
namespace {

constexpr uint8_t kAddressTypeIPv4 = 1;
constexpr uint8_t kAddressTypeDomain = 3;
constexpr uint8_t kAddressTypeIPv6 = 4;
constexpr size_t kMaxDomainLength = 255;

}  // namespace

HttpRequestHeaders BuildWebSocketTunnelHeaders(
    const AuthCredentials& credentials) {
  HttpRequestHeaders headers;
  headers.SetHeader(HttpRequestHeaders::kUserAgent,
                    embedder_support::GetUserAgent());
  headers.SetHeader(HttpRequestHeaders::kAcceptLanguage, "en-US,en;q=0.9");
  headers.SetHeader(HttpRequestHeaders::kCacheControl, "no-cache");
  headers.SetHeader(HttpRequestHeaders::kPragma, "no-cache");

  InitializeNonindexCodes();
  std::string padding(base::RandIntInclusive(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(),
                          base::as_writable_byte_span(padding));
  headers.SetHeader(kPaddingHeader, padding);
  headers.SetHeader(kPaddingTypeRequestHeader, "1,0");
  if (!credentials.Empty()) {
    headers.SetHeader("Proxy-Authorization",
                      "Basic " + base::Base64Encode(
                          base::UTF16ToUTF8(credentials.username()) + ":" +
                          base::UTF16ToUTF8(credentials.password())));
  }
  return headers;
}

std::optional<std::vector<uint8_t>> EncodeWebSocketTarget(
    const HostPortPair& target) {
  const std::string& host = target.host();
  const std::optional<IPAddress> address = IPAddress::FromIPLiteral(host);
  size_t address_size = 0;
  uint8_t address_type = kAddressTypeDomain;
  if (address) {
    address_size = address->size();
    if (address->IsIPv4()) {
      address_type = kAddressTypeIPv4;
    } else if (address->IsIPv6()) {
      address_type = kAddressTypeIPv6;
    } else {
      return std::nullopt;
    }
  } else if (host.empty() || host.size() > kMaxDomainLength) {
    return std::nullopt;
  }

  std::vector<uint8_t> payload;
  payload.reserve(4 + (address ? address_size : 1 + host.size()));
  payload.push_back(1);  // Tunnel protocol version.
  payload.push_back(address_type);
  if (address) {
    for (uint8_t byte : address->bytes()) {
      payload.push_back(byte);
    }
  } else {
    payload.push_back(base::checked_cast<uint8_t>(host.size()));
    payload.insert(payload.end(), host.begin(), host.end());
  }
  const uint16_t port = base::checked_cast<uint16_t>(target.port());
  payload.push_back(port >> 8);
  payload.push_back(port & 0xff);
  return payload;
}

}  // namespace net
