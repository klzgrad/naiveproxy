// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_sspi_win.h"
#include "base/bind.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/mock_sspi_library_win.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

void MatchDomainUserAfterSplit(const base::string16& combined,
                               const base::string16& expected_domain,
                               const base::string16& expected_user) {
  base::string16 actual_domain;
  base::string16 actual_user;
  SplitDomainAndUser(combined, &actual_domain, &actual_user);
  EXPECT_EQ(expected_domain, actual_domain);
  EXPECT_EQ(expected_user, actual_user);
}

const ULONG kMaxTokenLength = 100;

void UnexpectedCallback(int result) {
  // At present getting tokens from gssapi is fully synchronous, so the callback
  // should never be called.
  ADD_FAILURE();
}

}  // namespace

TEST(HttpAuthSSPITest, SplitUserAndDomain) {
  MatchDomainUserAfterSplit(STRING16_LITERAL("foobar"), STRING16_LITERAL(""),
                            STRING16_LITERAL("foobar"));
  MatchDomainUserAfterSplit(STRING16_LITERAL("FOO\\bar"),
                            STRING16_LITERAL("FOO"), STRING16_LITERAL("bar"));
}

TEST(HttpAuthSSPITest, DetermineMaxTokenLength_Normal) {
  SecPkgInfoW package_info;
  memset(&package_info, 0x0, sizeof(package_info));
  package_info.cbMaxToken = 1337;

  MockSSPILibrary mock_library;
  mock_library.ExpectQuerySecurityPackageInfo(L"NTLM", SEC_E_OK, &package_info);
  ULONG max_token_length = kMaxTokenLength;
  int rv = DetermineMaxTokenLength(&mock_library, L"NTLM", &max_token_length);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ(1337u, max_token_length);
}

TEST(HttpAuthSSPITest, DetermineMaxTokenLength_InvalidPackage) {
  MockSSPILibrary mock_library;
  mock_library.ExpectQuerySecurityPackageInfo(L"Foo", SEC_E_SECPKG_NOT_FOUND,
                                              nullptr);
  ULONG max_token_length = kMaxTokenLength;
  int rv = DetermineMaxTokenLength(&mock_library, L"Foo", &max_token_length);
  EXPECT_THAT(rv, IsError(ERR_UNSUPPORTED_AUTH_SCHEME));
  // |DetermineMaxTokenLength()| interface states that |max_token_length| should
  // not change on failure.
  EXPECT_EQ(100u, max_token_length);
}

TEST(HttpAuthSSPITest, ParseChallenge_FirstRound) {
  // The first round should just consist of an unadorned "Negotiate" header.
  MockSSPILibrary mock_library;
  HttpAuthSSPI auth_sspi(&mock_library, "Negotiate",
                         NEGOSSP_NAME, kMaxTokenLength);
  std::string challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_sspi.ParseChallenge(&challenge));
}

TEST(HttpAuthSSPITest, ParseChallenge_TwoRounds) {
  // The first round should just have "Negotiate", and the second round should
  // have a valid base64 token associated with it.
  MockSSPILibrary mock_library;
  HttpAuthSSPI auth_sspi(&mock_library, "Negotiate",
                         NEGOSSP_NAME, kMaxTokenLength);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_sspi.ParseChallenge(&first_challenge));

  // Generate an auth token and create another thing.
  std::string auth_token;
  EXPECT_EQ(OK, auth_sspi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));

  std::string second_challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_sspi.ParseChallenge(&second_challenge));
}

TEST(HttpAuthSSPITest, ParseChallenge_UnexpectedTokenFirstRound) {
  // If the first round challenge has an additional authentication token, it
  // should be treated as an invalid challenge from the server.
  MockSSPILibrary mock_library;
  HttpAuthSSPI auth_sspi(&mock_library, "Negotiate",
                         NEGOSSP_NAME, kMaxTokenLength);
  std::string challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            auth_sspi.ParseChallenge(&challenge));
}

TEST(HttpAuthSSPITest, ParseChallenge_MissingTokenSecondRound) {
  // If a later-round challenge is simply "Negotiate", it should be treated as
  // an authentication challenge rejection from the server or proxy.
  MockSSPILibrary mock_library;
  HttpAuthSSPI auth_sspi(&mock_library, "Negotiate",
                         NEGOSSP_NAME, kMaxTokenLength);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_sspi.ParseChallenge(&first_challenge));

  std::string auth_token;
  EXPECT_EQ(OK, auth_sspi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));
  std::string second_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_REJECT,
            auth_sspi.ParseChallenge(&second_challenge));
}

TEST(HttpAuthSSPITest, ParseChallenge_NonBase64EncodedToken) {
  // If a later-round challenge has an invalid base64 encoded token, it should
  // be treated as an invalid challenge.
  MockSSPILibrary mock_library;
  HttpAuthSSPI auth_sspi(&mock_library, "Negotiate",
                         NEGOSSP_NAME, kMaxTokenLength);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_sspi.ParseChallenge(&first_challenge));

  std::string auth_token;
  EXPECT_EQ(OK, auth_sspi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));
  std::string second_challenge_text = "Negotiate =happyjoy=";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            auth_sspi.ParseChallenge(&second_challenge));
}

}  // namespace net
