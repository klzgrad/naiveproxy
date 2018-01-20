// Copyright 2024 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_config.h"

#include <algorithm>
#include <iostream>

#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_tokenizer.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace net {
namespace {
ProxyServer MyProxyUriToProxyServer(std::string_view uri) {
  if (uri.compare(0, 7, "quic://") == 0) {
    return ProxySchemeHostAndPortToProxyServer(ProxyServer::SCHEME_QUIC,
                                               uri.substr(7));
  }
  return ProxyUriToProxyServer(uri, ProxyServer::SCHEME_INVALID);
}
}  // namespace

NaiveListenConfig::NaiveListenConfig() = default;
NaiveListenConfig::NaiveListenConfig(const NaiveListenConfig&) = default;
NaiveListenConfig::~NaiveListenConfig() = default;

bool NaiveListenConfig::Parse(const std::string& str) {
  GURL url(str);
  if (url.scheme() == "socks") {
    protocol = ClientProtocol::kSocks5;
  } else if (url.scheme() == "http") {
    protocol = ClientProtocol::kHttp;
  } else if (url.scheme() == "redir") {
#if BUILDFLAG(IS_LINUX)
    protocol = ClientProtocol::kRedir;
#else
    std::cerr << "Redir protocol only supports Linux." << std::endl;
    return false;
#endif
  } else {
    std::cerr << "Invalid scheme in " << str << std::endl;
    return false;
  }

  if (!url.username().empty()) {
    user = base::UnescapeBinaryURLComponent(url.username());
  }
  if (!url.password().empty()) {
    pass = base::UnescapeBinaryURLComponent(url.password());
  }

  if (!url.host().empty()) {
    addr = url.HostNoBrackets();
  }

  int effective_port = url.EffectiveIntPort();
  if (effective_port == url::PORT_INVALID) {
    std::cerr << "Invalid port in " << str << std::endl;
    return false;
  }
  if (effective_port != url::PORT_UNSPECIFIED) {
    port = effective_port;
  }

  return true;
}

NaiveConfig::NaiveConfig() = default;
NaiveConfig::NaiveConfig(const NaiveConfig&) = default;
NaiveConfig::~NaiveConfig() = default;

bool NaiveConfig::Parse(const base::Value::Dict& value) {
  if (const base::Value* v = value.Find("listen")) {
    listen.clear();
    if (const std::string* str = v->GetIfString()) {
      if (!listen.emplace_back().Parse(*str)) {
        return false;
      }
    } else if (const base::Value::List* strs = v->GetIfList()) {
      for (const auto& str_e : *strs) {
        if (const std::string* s = str_e.GetIfString()) {
          if (!listen.emplace_back().Parse(*s)) {
            return false;
          }
        } else {
          std::cerr << "Invalid listen element" << std::endl;
          return false;
        }
      }
    } else {
      std::cerr << "Invalid listen" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("insecure-concurrency")) {
    if (std::optional<int> i = v->GetIfInt()) {
      insecure_concurrency = *i;
    } else if (const std::string* str = v->GetIfString()) {
      if (!base::StringToInt(*str, &insecure_concurrency)) {
        std::cerr << "Invalid concurrency" << std::endl;
        return false;
      }
    } else {
      std::cerr << "Invalid concurrency" << std::endl;
      return false;
    }
    if (insecure_concurrency < 1) {
      std::cerr << "Invalid concurrency" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("extra-headers")) {
    if (const std::string* str = v->GetIfString()) {
      extra_headers.AddHeadersFromString(*str);
    } else {
      std::cerr << "Invalid extra-headers" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("proxy")) {
    if (const std::string* str = v->GetIfString(); str && !str->empty()) {
      base::StringTokenizer proxy_uri_list(*str, ",");
      std::vector<ProxyServer> proxy_servers;
      bool seen_tcp = false;
      while (proxy_uri_list.GetNext()) {
        std::string token(proxy_uri_list.token());
        GURL url(token);

        std::u16string proxy_user;
        std::u16string proxy_pass;
        net::GetIdentityFromURL(url, &proxy_user, &proxy_pass);
        GURL::Replacements remove_auth;
        remove_auth.ClearUsername();
        remove_auth.ClearPassword();
        GURL url_no_auth = url.ReplaceComponents(remove_auth);
        std::string proxy_uri = url_no_auth.GetWithEmptyPath().spec();
        if (!proxy_uri.empty() && proxy_uri.back() == '/') {
          proxy_uri.pop_back();
        }

        proxy_servers.emplace_back(MyProxyUriToProxyServer(proxy_uri));
        const ProxyServer& last = proxy_servers.back();
        if (last.is_quic()) {
          if (seen_tcp) {
            std::cerr << "QUIC proxy cannot follow TCP-based proxies"
                      << std::endl;
            return false;
          }
          origins_to_force_quic_on.insert(HostPortPair::FromURL(url));
        } else if (last.is_https() || last.is_http() || last.is_socks()) {
          seen_tcp = true;
        } else {
          std::cerr << "Invalid proxy scheme" << std::endl;
          return false;
        }

        AuthCredentials auth(proxy_user, proxy_pass);
        if (!auth.Empty()) {
          if (last.is_socks()) {
            std::cerr << "SOCKS proxy with auth is not supported" << std::endl;
          } else {
            std::string proxy_url(token);
            if (proxy_url.compare(0, 7, "quic://") == 0) {
              proxy_url.replace(0, 4, "https");
            }
            auth_store[url::SchemeHostPort{GURL{proxy_url}}] = auth;
          }
        }
      }

      if (proxy_servers.size() > 1 &&
          std::any_of(proxy_servers.begin(), proxy_servers.end(),
                      [](const ProxyServer& s) { return s.is_socks(); })) {
        // See net/socket/connect_job_params_factory.cc
        // DCHECK(proxy_server.is_socks());
        // DCHECK_EQ(1u, proxy_chain.length());
        std::cerr
            << "Multi-proxy chain containing SOCKS proxies is not supported."
            << std::endl;
        return false;
      }
      if (std::any_of(proxy_servers.begin(), proxy_servers.end(),
                      [](const ProxyServer& s) { return s.is_quic(); })) {
        proxy_chain = ProxyChain::ForIpProtection(proxy_servers);
      } else {
        proxy_chain = ProxyChain(proxy_servers);
      }

      if (!proxy_chain.IsValid()) {
        std::cerr << "Invalid proxy chain" << std::endl;
        return false;
      }
    } else {
      std::cerr << "Invalid proxy argument" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("host-resolver-rules")) {
    if (const std::string* str = v->GetIfString()) {
      host_resolver_rules = *str;
    } else {
      std::cerr << "Invalid host-resolver-rules" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("resolver-range")) {
    if (const std::string* str = v->GetIfString(); str && !str->empty()) {
      if (!net::ParseCIDRBlock(*str, &resolver_range, &resolver_prefix)) {
        std::cerr << "Invalid resolver-range" << std::endl;
        return false;
      }
      if (resolver_range.IsIPv6()) {
        std::cerr << "IPv6 resolver range not supported" << std::endl;
        return false;
      }
    } else {
      std::cerr << "Invalid resolver-range" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("log")) {
    if (const std::string* str = v->GetIfString()) {
      if (!str->empty()) {
        log.logging_dest = logging::LOG_TO_FILE;
        log_file = base::FilePath::FromUTF8Unsafe(*str);
        log.log_file_path = log_file.value().c_str();
      } else {
        log.logging_dest = logging::LOG_TO_STDERR;
      }
    } else {
      std::cerr << "Invalid log" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("log-net-log")) {
    if (const std::string* str = v->GetIfString(); str && !str->empty()) {
      log_net_log = base::FilePath::FromUTF8Unsafe(*str);
    } else {
      std::cerr << "Invalid log-net-log" << std::endl;
      return false;
    }
  }

  if (const base::Value* v = value.Find("ssl-key-log-file")) {
    if (const std::string* str = v->GetIfString(); str && !str->empty()) {
      ssl_key_log_file = base::FilePath::FromUTF8Unsafe(*str);
    } else {
      std::cerr << "Invalid ssl-key-log-file" << std::endl;
      return false;
    }
  }

  if (value.contains("no-post-quantum")) {
    no_post_quantum = true;
  }

  return true;
}

}  // namespace net
