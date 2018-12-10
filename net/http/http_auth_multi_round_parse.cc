// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base64.h"
#include "base/strings/string_util.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_multi_round_parse.h"

namespace net {

namespace {

// Check that the scheme in the challenge matches the expected scheme
bool SchemeIsValid(const std::string& scheme,
                   HttpAuthChallengeTokenizer* challenge) {
  // There is no guarantee that challenge->scheme() is valid ASCII, but
  // LowerCaseEqualsASCII will do the right thing even if it isn't.
  return base::LowerCaseEqualsASCII(challenge->scheme(),
                                    base::ToLowerASCII(scheme));
}

}  // namespace

HttpAuth::AuthorizationResult ParseFirstRoundChallenge(
    const std::string& scheme,
    HttpAuthChallengeTokenizer* challenge) {
  // Verify the challenge's auth-scheme.
  if (!SchemeIsValid(scheme, challenge))
    return HttpAuth::AUTHORIZATION_RESULT_INVALID;

  std::string encoded_auth_token = challenge->base64_param();
  if (!encoded_auth_token.empty()) {
    return HttpAuth::AUTHORIZATION_RESULT_INVALID;
  }
  return HttpAuth::AUTHORIZATION_RESULT_ACCEPT;
}

HttpAuth::AuthorizationResult ParseLaterRoundChallenge(
    const std::string& scheme,
    HttpAuthChallengeTokenizer* challenge,
    std::string* encoded_token,
    std::string* decoded_token) {
  // Verify the challenge's auth-scheme.
  if (!SchemeIsValid(scheme, challenge))
    return HttpAuth::AUTHORIZATION_RESULT_INVALID;

  *encoded_token = challenge->base64_param();
  if (encoded_token->empty())
    return HttpAuth::AUTHORIZATION_RESULT_REJECT;

  // Make sure the additional token is base64 encoded.
  if (!base::Base64Decode(*encoded_token, decoded_token))
    return HttpAuth::AUTHORIZATION_RESULT_INVALID;
  return HttpAuth::AUTHORIZATION_RESULT_ACCEPT;
}

}  // namespace net
