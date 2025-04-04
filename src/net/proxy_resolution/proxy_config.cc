// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/proxy_config.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/notreached.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "build/buildflag.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/net_buildflags.h"
#include "net/proxy_resolution/proxy_info.h"

namespace net {

namespace {

// If |proxies| is non-empty, sets it in |dict| under the key |name|.
void AddProxyListToValue(const char* name,
                         const ProxyList& proxies,
                         base::Value::Dict* dict) {
  if (!proxies.IsEmpty()) {
    dict->Set(name, proxies.ToValue());
  }
}

// Split the |uri_list| on commas and add each entry to |proxy_list| in turn.
void AddProxyURIListToProxyList(std::string uri_list,
                                ProxyList* proxy_list,
                                ProxyServer::Scheme default_scheme,
                                bool allow_bracketed_proxy_chains,
                                bool is_quic_allowed) {
  base::StringTokenizer proxy_uri_list(uri_list, ",");
  while (proxy_uri_list.GetNext()) {
    proxy_list->AddProxyChain(
        allow_bracketed_proxy_chains
            ? MultiProxyUrisToProxyChain(proxy_uri_list.token(), default_scheme,
                                         is_quic_allowed)
            : ProxyUriToProxyChain(proxy_uri_list.token(), default_scheme,
                                   is_quic_allowed));
  }
}

}  // namespace

ProxyConfig::ProxyRules::ProxyRules() = default;

ProxyConfig::ProxyRules::ProxyRules(const ProxyRules& other) = default;

ProxyConfig::ProxyRules::~ProxyRules() = default;

void ProxyConfig::ProxyRules::Apply(const GURL& url, ProxyInfo* result) const {
  if (empty()) {
    result->UseDirect();
    return;
  }

  if (bypass_rules.Matches(url, reverse_bypass)) {
    result->UseDirectWithBypassedProxy();
    return;
  }

  switch (type) {
    case ProxyRules::Type::PROXY_LIST: {
      result->UseProxyList(single_proxies);
      return;
    }
    case ProxyRules::Type::PROXY_LIST_PER_SCHEME: {
      const ProxyList* entry = MapUrlSchemeToProxyList(url.scheme());
      if (entry) {
        result->UseProxyList(*entry);
      } else {
        // We failed to find a matching proxy server for the current URL
        // scheme. Default to direct.
        result->UseDirect();
      }
      return;
    }
    default: {
      NOTREACHED();
    }
  }
}

void ProxyConfig::ProxyRules::ParseFromString(const std::string& proxy_rules,
                                              bool allow_bracketed_proxy_chains,
                                              bool is_quic_allowed) {
  // Reset.
  type = Type::EMPTY;
  single_proxies = ProxyList();
  proxies_for_http = ProxyList();
  proxies_for_https = ProxyList();
  proxies_for_ftp = ProxyList();
  fallback_proxies = ProxyList();

#if !BUILDFLAG(ENABLE_BRACKETED_PROXY_URIS)
  // `allow_multi_proxy_chains` can only be true in non-release builds;
  CHECK(!allow_bracketed_proxy_chains);
#endif  // !BUILDFLAG(ENABLE_BRACKETED_PROXY_URIS)

#if !BUILDFLAG(ENABLE_QUIC_PROXY_SUPPORT)
  CHECK(!is_quic_allowed);
#endif  // BUILDFLAG(ENABLE_QUIC_PROXY_SUPPORT)

  base::StringTokenizer proxy_server_list(proxy_rules, ";");
  while (proxy_server_list.GetNext()) {
    base::StringTokenizer proxy_server_for_scheme(
        proxy_server_list.token_begin(), proxy_server_list.token_end(), "=");

    while (proxy_server_for_scheme.GetNext()) {
      std::string url_scheme = proxy_server_for_scheme.token();

      // If we fail to get the proxy server here, it means that
      // this is a regular proxy server configuration, i.e. proxies
      // are not configured per protocol.
      if (!proxy_server_for_scheme.GetNext()) {
        if (type == Type::PROXY_LIST_PER_SCHEME) {
          continue;  // Unexpected.
        }
        AddProxyURIListToProxyList(
            std::move(url_scheme), &single_proxies, ProxyServer::SCHEME_HTTP,
            allow_bracketed_proxy_chains, is_quic_allowed);
        type = Type::PROXY_LIST;
        return;
      }

      // Trim whitespace off the url scheme.
      base::TrimWhitespaceASCII(url_scheme, base::TRIM_ALL, &url_scheme);

      // Add it to the per-scheme mappings (if supported scheme).
      type = Type::PROXY_LIST_PER_SCHEME;
      ProxyList* entry = MapUrlSchemeToProxyListNoFallback(url_scheme);
      ProxyServer::Scheme default_scheme = ProxyServer::SCHEME_HTTP;

      // socks=XXX is inconsistent with the other formats, since "socks"
      // is not a URL scheme. Rather this means "for everything else, send
      // it to the SOCKS proxy server XXX".
      if (url_scheme == "socks") {
        DCHECK(!entry);
        entry = &fallback_proxies;
        // Note that here 'socks' is understood to be SOCKS4, even though
        // 'socks' maps to SOCKS5 in ProxyServer::GetSchemeFromURIInternal.
        default_scheme = ProxyServer::SCHEME_SOCKS4;
      }

      if (entry) {
        AddProxyURIListToProxyList(proxy_server_for_scheme.token(), entry,
                                   default_scheme, allow_bracketed_proxy_chains,
                                   is_quic_allowed);
      }
    }
  }
}

const ProxyList* ProxyConfig::ProxyRules::MapUrlSchemeToProxyList(
    const std::string& url_scheme) const {
  const ProxyList* proxy_server_list =
      const_cast<ProxyRules*>(this)->MapUrlSchemeToProxyListNoFallback(
          url_scheme);
  if (proxy_server_list && !proxy_server_list->IsEmpty()) {
    return proxy_server_list;
  }
  if (url_scheme == "ws" || url_scheme == "wss") {
    return GetProxyListForWebSocketScheme();
  }
  if (!fallback_proxies.IsEmpty()) {
    return &fallback_proxies;
  }
  return nullptr;  // No mapping for this scheme. Use direct.
}

bool ProxyConfig::ProxyRules::Equals(const ProxyRules& other) const {
  return type == other.type && single_proxies.Equals(other.single_proxies) &&
         proxies_for_http.Equals(other.proxies_for_http) &&
         proxies_for_https.Equals(other.proxies_for_https) &&
         proxies_for_ftp.Equals(other.proxies_for_ftp) &&
         fallback_proxies.Equals(other.fallback_proxies) &&
         bypass_rules == other.bypass_rules &&
         reverse_bypass == other.reverse_bypass;
}

ProxyList* ProxyConfig::ProxyRules::MapUrlSchemeToProxyListNoFallback(
    const std::string& scheme) {
  DCHECK_EQ(Type::PROXY_LIST_PER_SCHEME, type);
  if (scheme == "http") {
    return &proxies_for_http;
  }
  if (scheme == "https") {
    return &proxies_for_https;
  }
  if (scheme == "ftp") {
    return &proxies_for_ftp;
  }
  return nullptr;  // No mapping for this scheme.
}

const ProxyList* ProxyConfig::ProxyRules::GetProxyListForWebSocketScheme()
    const {
  // Follow the recommendation from RFC 6455 section 4.1.3:
  //
  //       NOTE: Implementations that do not expose explicit UI for
  //       selecting a proxy for WebSocket connections separate from other
  //       proxies are encouraged to use a SOCKS5 [RFC1928] proxy for
  //       WebSocket connections, if available, or failing that, to prefer
  //       the proxy configured for HTTPS connections over the proxy
  //       configured for HTTP connections.
  //
  // This interpretation is a bit different from the RFC, in
  // that it favors both SOCKSv4 and SOCKSv5.
  //
  // When the net::ProxyRules came from system proxy settings,
  // "fallback_proxies" will be empty, or a a single SOCKS
  // proxy, making this ordering match the RFC.
  //
  // However for other configurations it is possible for
  // "fallback_proxies" to be a list of any ProxyServer,
  // including non-SOCKS. In this case "fallback_proxies" is
  // still prioritized over proxies_for_http and
  // proxies_for_https.
  if (!fallback_proxies.IsEmpty()) {
    return &fallback_proxies;
  }
  if (!proxies_for_https.IsEmpty()) {
    return &proxies_for_https;
  }
  if (!proxies_for_http.IsEmpty()) {
    return &proxies_for_http;
  }
  return nullptr;
}

ProxyConfig::ProxyConfig() = default;

ProxyConfig::ProxyConfig(const ProxyConfig& config) = default;

ProxyConfig::ProxyConfig(ProxyConfig&& config) = default;

ProxyConfig& ProxyConfig::operator=(const ProxyConfig& config) = default;

ProxyConfig& ProxyConfig::operator=(ProxyConfig&& config) = default;

ProxyConfig::~ProxyConfig() = default;

bool ProxyConfig::Equals(const ProxyConfig& other) const {
  return auto_detect_ == other.auto_detect_ && pac_url_ == other.pac_url_ &&
         pac_mandatory_ == other.pac_mandatory_ &&
         from_system_ == other.from_system_ &&
         proxy_rules_.Equals(other.proxy_rules());
}

bool ProxyConfig::HasAutomaticSettings() const {
  return auto_detect_ || has_pac_url();
}

void ProxyConfig::ClearAutomaticSettings() {
  auto_detect_ = false;
  pac_url_ = GURL();
}

base::Value ProxyConfig::ToValue() const {
  base::Value::Dict dict;

  // Output the automatic settings.
  if (auto_detect_) {
    dict.Set("auto_detect", auto_detect_);
  }
  if (has_pac_url()) {
    dict.Set("pac_url", pac_url_.possibly_invalid_spec());
    if (pac_mandatory_) {
      dict.Set("pac_mandatory", pac_mandatory_);
    }
  }
  if (from_system_) {
    dict.Set("from_system", from_system_);
  }

  // Output the manual settings.
  if (proxy_rules_.type != ProxyRules::Type::EMPTY) {
    switch (proxy_rules_.type) {
      case ProxyRules::Type::PROXY_LIST:
        AddProxyListToValue("single_proxy", proxy_rules_.single_proxies, &dict);
        break;
      case ProxyRules::Type::PROXY_LIST_PER_SCHEME: {
        base::Value::Dict dict2;
        AddProxyListToValue("http", proxy_rules_.proxies_for_http, &dict2);
        AddProxyListToValue("https", proxy_rules_.proxies_for_https, &dict2);
        AddProxyListToValue("ftp", proxy_rules_.proxies_for_ftp, &dict2);
        AddProxyListToValue("fallback", proxy_rules_.fallback_proxies, &dict2);
        dict.Set("proxy_per_scheme", std::move(dict2));
        break;
      }
      default:
        NOTREACHED();
    }

    // Output the bypass rules.
    const ProxyBypassRules& bypass = proxy_rules_.bypass_rules;
    if (!bypass.rules().empty()) {
      if (proxy_rules_.reverse_bypass) {
        dict.Set("reverse_bypass", true);
      }

      base::Value::List list;

      for (const auto& bypass_rule : bypass.rules()) {
        list.Append(bypass_rule->ToString());
      }

      dict.Set("bypass_list", std::move(list));
    }
  }

  return base::Value(std::move(dict));
}

}  // namespace net
