// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_multi_round_parse.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(HttpAuthHandlerNegotiateParseTest, ParseFirstRoundChallenge) {
  // The first round should just consist of an unadorned header with the scheme
  // name.
  std::string challenge_text = "DummyScheme";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            ParseFirstRoundChallenge("dummyscheme", &challenge));
}

TEST(HttpAuthHandlerNegotiateParseTest,
     ParseFirstNegotiateChallenge_UnexpectedToken) {
  // If the first round challenge has an additional authentication token, it
  // should be treated as an invalid challenge from the server.
  std::string challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            ParseFirstRoundChallenge("negotiate", &challenge));
}

TEST(HttpAuthHandlerNegotiateParseTest,
     ParseFirstNegotiateChallenge_BadScheme) {
  std::string challenge_text = "DummyScheme";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            ParseFirstRoundChallenge("negotiate", &challenge));
}

TEST(HttpAuthHandlerNegotiateParseTest, ParseLaterRoundChallenge) {
  // Later rounds should always have a Base64 encoded token.
  std::string challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  std::string encoded_token;
  std::string decoded_token;
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            ParseLaterRoundChallenge("negotiate", &challenge, &encoded_token,
                                     &decoded_token));
  EXPECT_EQ("Zm9vYmFy", encoded_token);
  EXPECT_EQ("foobar", decoded_token);
}

TEST(HttpAuthHandlerNegotiateParseTest,
     ParseAnotherNegotiateChallenge_MissingToken) {
  std::string challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  std::string encoded_token;
  std::string decoded_token;
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_REJECT,
            ParseLaterRoundChallenge("negotiate", &challenge, &encoded_token,
                                     &decoded_token));
}

TEST(HttpAuthHandlerNegotiateParseTest,
     ParseAnotherNegotiateChallenge_InvalidToken) {
  std::string challenge_text = "Negotiate ***";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  std::string encoded_token;
  std::string decoded_token;
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            ParseLaterRoundChallenge("negotiate", &challenge, &encoded_token,
                                     &decoded_token));
}

}  // namespace net
