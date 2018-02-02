// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler_factory.h"

#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_filter.h"
#include "net/http/http_auth_handler_basic.h"
#include "net/http/http_auth_handler_digest.h"
#include "net/http/http_auth_handler_ntlm.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_auth_scheme.h"
#include "net/ssl/ssl_info.h"

#if defined(USE_KERBEROS)
#include "net/http/http_auth_handler_negotiate.h"
#endif

namespace net {

int HttpAuthHandlerFactory::CreateAuthHandlerFromString(
    const std::string& challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const GURL& origin,
    const NetLogWithSource& net_log,
    std::unique_ptr<HttpAuthHandler>* handler) {
  HttpAuthChallengeTokenizer props(challenge.begin(), challenge.end());
  return CreateAuthHandler(&props, target, ssl_info, origin, CREATE_CHALLENGE,
                           1, net_log, handler);
}

int HttpAuthHandlerFactory::CreatePreemptiveAuthHandlerFromString(
    const std::string& challenge,
    HttpAuth::Target target,
    const GURL& origin,
    int digest_nonce_count,
    const NetLogWithSource& net_log,
    std::unique_ptr<HttpAuthHandler>* handler) {
  HttpAuthChallengeTokenizer props(challenge.begin(), challenge.end());
  SSLInfo null_ssl_info;
  return CreateAuthHandler(&props, target, null_ssl_info, origin,
                           CREATE_PREEMPTIVE, digest_nonce_count, net_log,
                           handler);
}

namespace {

const char* const kDefaultAuthSchemes[] = {kBasicAuthScheme, kDigestAuthScheme,
#if defined(USE_KERBEROS) && !defined(OS_ANDROID)
                                           kNegotiateAuthScheme,
#endif
                                           kNtlmAuthScheme};

// Create a registry factory. Note that |prefs| may be a temporary, and
// should only be used to create the factories. It should not be passed
// to the registry factory or its children as the preferences they should
// use.
std::unique_ptr<HttpAuthHandlerRegistryFactory>
CreateAuthHandlerRegistryFactory(const HttpAuthPreferences& prefs,
                                 HostResolver* host_resolver) {
  std::unique_ptr<HttpAuthHandlerRegistryFactory> registry_factory(
      new HttpAuthHandlerRegistryFactory());
  if (prefs.IsSupportedScheme(kBasicAuthScheme))
    registry_factory->RegisterSchemeFactory(
        kBasicAuthScheme, new HttpAuthHandlerBasic::Factory());
  if (prefs.IsSupportedScheme(kDigestAuthScheme))
    registry_factory->RegisterSchemeFactory(
        kDigestAuthScheme, new HttpAuthHandlerDigest::Factory());
  if (prefs.IsSupportedScheme(kNtlmAuthScheme)) {
    HttpAuthHandlerNTLM::Factory* ntlm_factory =
        new HttpAuthHandlerNTLM::Factory();
#if defined(OS_WIN)
    ntlm_factory->set_sspi_library(new SSPILibraryDefault());
#endif  // defined(OS_WIN)
    registry_factory->RegisterSchemeFactory(kNtlmAuthScheme, ntlm_factory);
  }
#if defined(USE_KERBEROS)
  if (prefs.IsSupportedScheme(kNegotiateAuthScheme)) {
    DCHECK(host_resolver);
    HttpAuthHandlerNegotiate::Factory* negotiate_factory =
        new HttpAuthHandlerNegotiate::Factory();
#if defined(OS_WIN)
    negotiate_factory->set_library(std::make_unique<SSPILibraryDefault>());
#elif defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
    negotiate_factory->set_library(
        std::make_unique<GSSAPISharedLibrary>(prefs.GssapiLibraryName()));
#elif defined(OS_CHROMEOS)
    negotiate_factory->set_library(std::make_unique<GSSAPISharedLibrary>(""));
#endif
    negotiate_factory->set_host_resolver(host_resolver);
    registry_factory->RegisterSchemeFactory(kNegotiateAuthScheme,
                                            negotiate_factory);
  }
#endif  // defined(USE_KERBEROS)
  return registry_factory;
}

}  // namespace

HttpAuthHandlerRegistryFactory::HttpAuthHandlerRegistryFactory() = default;

HttpAuthHandlerRegistryFactory::~HttpAuthHandlerRegistryFactory() = default;

void HttpAuthHandlerRegistryFactory::SetHttpAuthPreferences(
    const std::string& scheme,
    const HttpAuthPreferences* prefs) {
  HttpAuthHandlerFactory* factory = GetSchemeFactory(scheme);
  if (factory)
    factory->set_http_auth_preferences(prefs);
}

void HttpAuthHandlerRegistryFactory::RegisterSchemeFactory(
    const std::string& scheme,
    HttpAuthHandlerFactory* factory) {
  factory->set_http_auth_preferences(http_auth_preferences());
  std::string lower_scheme = base::ToLowerASCII(scheme);
  if (factory)
    factory_map_[lower_scheme] = base::WrapUnique(factory);
  else
    factory_map_.erase(lower_scheme);
}

HttpAuthHandlerFactory* HttpAuthHandlerRegistryFactory::GetSchemeFactory(
    const std::string& scheme) const {
  std::string lower_scheme = base::ToLowerASCII(scheme);
  FactoryMap::const_iterator it = factory_map_.find(lower_scheme);
  if (it == factory_map_.end()) {
    return NULL;                  // |scheme| is not registered.
  }
  return it->second.get();
}

// static
std::unique_ptr<HttpAuthHandlerRegistryFactory>
HttpAuthHandlerFactory::CreateDefault(HostResolver* host_resolver) {
  std::vector<std::string> auth_types(std::begin(kDefaultAuthSchemes),
                                      std::end(kDefaultAuthSchemes));
  HttpAuthPreferences prefs(auth_types
#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
                            ,
                            std::string()
#endif
#if defined(OS_CHROMEOS)
                                ,
                            true
#endif
                            );
  return CreateAuthHandlerRegistryFactory(prefs, host_resolver);
}

// static
std::unique_ptr<HttpAuthHandlerRegistryFactory>
HttpAuthHandlerRegistryFactory::Create(const HttpAuthPreferences* prefs,
                                       HostResolver* host_resolver) {
  std::unique_ptr<HttpAuthHandlerRegistryFactory> registry_factory(
      CreateAuthHandlerRegistryFactory(*prefs, host_resolver));
  registry_factory->set_http_auth_preferences(prefs);
  for (auto& factory_entry : registry_factory->factory_map_) {
    factory_entry.second->set_http_auth_preferences(prefs);
  }
  return registry_factory;
}

int HttpAuthHandlerRegistryFactory::CreateAuthHandler(
    HttpAuthChallengeTokenizer* challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const GURL& origin,
    CreateReason reason,
    int digest_nonce_count,
    const NetLogWithSource& net_log,
    std::unique_ptr<HttpAuthHandler>* handler) {
  std::string scheme = challenge->scheme();
  if (scheme.empty()) {
    handler->reset();
    return ERR_INVALID_RESPONSE;
  }
  std::string lower_scheme = base::ToLowerASCII(scheme);
  FactoryMap::iterator it = factory_map_.find(lower_scheme);
  if (it == factory_map_.end()) {
    handler->reset();
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  }
  DCHECK(it->second);
  return it->second->CreateAuthHandler(challenge, target, ssl_info, origin,
                                       reason, digest_nonce_count, net_log,
                                       handler);
}

}  // namespace net
