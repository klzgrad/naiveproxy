// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler_ntlm.h"

#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_auth_handler_ntlm.h"
#include "net/http/http_auth_preferences.h"
#include "net/ssl/ssl_info.h"

namespace net {

namespace {

uint64_t GetMSTime() {
  return base::Time::Now().since_origin().InMicroseconds() * 10;
}

void GenerateRandom(uint8_t* output, size_t n) {
  base::RandBytes(output, n);
}

void RecordNtlmV2Usage(bool is_v2, bool is_secure) {
  auto bucket = is_v2 ? is_secure ? NtlmV2Usage::kEnabledOverSecure
                                  : NtlmV2Usage::kEnabledOverInsecure
                      : is_secure ? NtlmV2Usage::kDisabledOverSecure
                                  : NtlmV2Usage::kDisabledOverInsecure;
  UMA_HISTOGRAM_ENUMERATION("Net.HttpAuthNtlmV2Usage", bucket);
}

}  // namespace

// static
HttpAuthHandlerNTLM::GetMSTimeProc HttpAuthHandlerNTLM::get_ms_time_proc_ =
    GetMSTime;

// static
HttpAuthHandlerNTLM::GenerateRandomProc
    HttpAuthHandlerNTLM::generate_random_proc_ = GenerateRandom;

// static
HttpAuthHandlerNTLM::HostNameProc HttpAuthHandlerNTLM::get_host_name_proc_ =
    GetHostName;

HttpAuthHandlerNTLM::HttpAuthHandlerNTLM(
    const HttpAuthPreferences* http_auth_preferences)
    : ntlm_client_(ntlm::NtlmFeatures(
          http_auth_preferences ? http_auth_preferences->NtlmV2Enabled()
                                : true)) {}

bool HttpAuthHandlerNTLM::NeedsIdentity() {
  // This gets called for each round-trip.  Only require identity on
  // the first call (when auth_data_ is empty).  On subsequent calls,
  // we use the initially established identity.
  return auth_data_.empty();
}

bool HttpAuthHandlerNTLM::AllowsDefaultCredentials() {
  // Default credentials are not supported in the portable implementation of
  // NTLM, but are supported in the SSPI implementation.
  return false;
}

int HttpAuthHandlerNTLM::InitializeBeforeFirstChallenge() {
  return OK;
}

HttpAuthHandlerNTLM::~HttpAuthHandlerNTLM() = default;

// static
HttpAuthHandlerNTLM::GetMSTimeProc HttpAuthHandlerNTLM::SetGetMSTimeProc(
    GetMSTimeProc proc) {
  GetMSTimeProc old_proc = get_ms_time_proc_;
  get_ms_time_proc_ = proc;
  return old_proc;
}

// static
HttpAuthHandlerNTLM::GenerateRandomProc
HttpAuthHandlerNTLM::SetGenerateRandomProc(GenerateRandomProc proc) {
  GenerateRandomProc old_proc = generate_random_proc_;
  generate_random_proc_ = proc;
  return old_proc;
}

// static
HttpAuthHandlerNTLM::HostNameProc HttpAuthHandlerNTLM::SetHostNameProc(
    HostNameProc proc) {
  HostNameProc old_proc = get_host_name_proc_;
  get_host_name_proc_ = proc;
  return old_proc;
}

HttpAuthHandlerNTLM::Factory::Factory() = default;

HttpAuthHandlerNTLM::Factory::~Factory() = default;

std::vector<uint8_t> HttpAuthHandlerNTLM::GetNextToken(
    base::span<const uint8_t> in_token) {
  // If in_token is non-empty, then assume it contains a challenge message,
  // and generate the Authenticate message in reply. Otherwise return the
  // Negotiate message.
  if (in_token.empty()) {
    return ntlm_client_.GetNegotiateMessage();
  }

  std::string hostname = get_host_name_proc_();
  if (hostname.empty())
    return {};
  uint8_t client_challenge[8];
  generate_random_proc_(client_challenge, 8);
  uint64_t client_time = get_ms_time_proc_();

  return ntlm_client_.GenerateAuthenticateMessage(
      domain_, credentials_.username(), credentials_.password(), hostname,
      channel_bindings_, CreateSPN(origin_), client_time, client_challenge,
      in_token);
}

int HttpAuthHandlerNTLM::Factory::CreateAuthHandler(
    HttpAuthChallengeTokenizer* challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const GURL& origin,
    CreateReason reason,
    int digest_nonce_count,
    const NetLogWithSource& net_log,
    HostResolver* host_resolver,
    std::unique_ptr<HttpAuthHandler>* handler) {
  if (reason == CREATE_PREEMPTIVE)
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  // TODO(cbentzel): Move towards model of parsing in the factory
  //                 method and only constructing when valid.
  // NOTE: Default credentials are not supported for the portable implementation
  // of NTLM.
  std::unique_ptr<HttpAuthHandler> tmp_handler(
      new HttpAuthHandlerNTLM(http_auth_preferences()));
  if (!tmp_handler->InitFromChallenge(challenge, target, ssl_info, origin,
                                      net_log))
    return ERR_INVALID_RESPONSE;
  RecordNtlmV2Usage(
      http_auth_preferences() ? http_auth_preferences()->NtlmV2Enabled() : true,
      ssl_info.is_valid());
  handler->swap(tmp_handler);
  return OK;
}

}  // namespace net
