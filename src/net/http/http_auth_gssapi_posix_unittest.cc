// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_gssapi_posix.h"

#include <memory>

#include "base/bind.h"
#include "base/logging.h"
#include "base/native_library.h"
#include "base/stl_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/mock_gssapi_library_posix.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

// gss_buffer_t helpers.
void ClearBuffer(gss_buffer_t dest) {
  if (!dest)
    return;
  dest->length = 0;
  delete [] reinterpret_cast<char*>(dest->value);
  dest->value = nullptr;
}

void SetBuffer(gss_buffer_t dest, const void* src, size_t length) {
  if (!dest)
    return;
  ClearBuffer(dest);
  if (!src)
    return;
  dest->length = length;
  if (length) {
    dest->value = new char[length];
    memcpy(dest->value, src, length);
  }
}

void CopyBuffer(gss_buffer_t dest, const gss_buffer_t src) {
  if (!dest)
    return;
  ClearBuffer(dest);
  if (!src)
    return;
  SetBuffer(dest, src->value, src->length);
}

const char kInitialAuthResponse[] = "Mary had a little lamb";

void EstablishInitialContext(test::MockGSSAPILibrary* library) {
  test::GssContextMockImpl context_info(
      "localhost",                         // Source name
      "example.com",                       // Target name
      23,                                  // Lifetime
      *CHROME_GSS_SPNEGO_MECH_OID_DESC,    // Mechanism
      0,                                   // Context flags
      1,                                   // Locally initiated
      0);                                  // Open
  gss_buffer_desc in_buffer = {0, nullptr};
  gss_buffer_desc out_buffer = {base::size(kInitialAuthResponse),
                                const_cast<char*>(kInitialAuthResponse)};
  library->ExpectSecurityContext(
      "Negotiate",
      GSS_S_CONTINUE_NEEDED,
      0,
      context_info,
      in_buffer,
      out_buffer);
}

void UnexpectedCallback(int result) {
  // At present getting tokens from gssapi is fully synchronous, so the callback
  // should never be called.
  ADD_FAILURE();
}

}  // namespace

TEST(HttpAuthGSSAPIPOSIXTest, GSSAPIStartup) {
  // TODO(ahendrickson): Manipulate the libraries and paths to test each of the
  // libraries we expect, and also whether or not they have the interface
  // functions we want.
  std::unique_ptr<GSSAPILibrary> gssapi(new GSSAPISharedLibrary(std::string()));
  DCHECK(gssapi.get());
  EXPECT_TRUE(gssapi.get()->Init());
}

#if defined(DLOPEN_KERBEROS)
TEST(HttpAuthGSSAPIPOSIXTest, GSSAPILoadCustomLibrary) {
  std::unique_ptr<GSSAPILibrary> gssapi(
      new GSSAPISharedLibrary("/this/library/does/not/exist"));
  EXPECT_FALSE(gssapi.get()->Init());
}
#endif  // defined(DLOPEN_KERBEROS)

TEST(HttpAuthGSSAPIPOSIXTest, GSSAPICycle) {
  std::unique_ptr<test::MockGSSAPILibrary> mock_library(
      new test::MockGSSAPILibrary);
  DCHECK(mock_library.get());
  mock_library->Init();
  const char kAuthResponse[] = "Mary had a little lamb";
  test::GssContextMockImpl context1(
      "localhost",                         // Source name
      "example.com",                       // Target name
      23,                                  // Lifetime
      *CHROME_GSS_SPNEGO_MECH_OID_DESC,    // Mechanism
      0,                                   // Context flags
      1,                                   // Locally initiated
      0);                                  // Open
  test::GssContextMockImpl context2(
      "localhost",                         // Source name
      "example.com",                       // Target name
      23,                                  // Lifetime
      *CHROME_GSS_SPNEGO_MECH_OID_DESC,    // Mechanism
      0,                                   // Context flags
      1,                                   // Locally initiated
      1);                                  // Open
  test::MockGSSAPILibrary::SecurityContextQuery queries[] = {
      test::MockGSSAPILibrary::SecurityContextQuery(
          "Negotiate",            // Package name
          GSS_S_CONTINUE_NEEDED,  // Major response code
          0,                      // Minor response code
          context1,               // Context
          nullptr,                // Expected input token
          kAuthResponse),         // Output token
      test::MockGSSAPILibrary::SecurityContextQuery(
          "Negotiate",     // Package name
          GSS_S_COMPLETE,  // Major response code
          0,               // Minor response code
          context2,        // Context
          kAuthResponse,   // Expected input token
          kAuthResponse)   // Output token
  };

  for (size_t i = 0; i < base::size(queries); ++i) {
    mock_library->ExpectSecurityContext(queries[i].expected_package,
                                        queries[i].response_code,
                                        queries[i].minor_response_code,
                                        queries[i].context_info,
                                        queries[i].expected_input_token,
                                        queries[i].output_token);
  }

  OM_uint32 major_status = 0;
  OM_uint32 minor_status = 0;
  gss_cred_id_t initiator_cred_handle = nullptr;
  gss_ctx_id_t context_handle = nullptr;
  gss_name_t target_name = nullptr;
  gss_OID mech_type = nullptr;
  OM_uint32 req_flags = 0;
  OM_uint32 time_req = 25;
  gss_channel_bindings_t input_chan_bindings = nullptr;
  gss_buffer_desc input_token = {0, nullptr};
  gss_OID actual_mech_type = nullptr;
  gss_buffer_desc output_token = {0, nullptr};
  OM_uint32 ret_flags = 0;
  OM_uint32 time_rec = 0;
  for (size_t i = 0; i < base::size(queries); ++i) {
    major_status = mock_library->init_sec_context(&minor_status,
                                                  initiator_cred_handle,
                                                  &context_handle,
                                                  target_name,
                                                  mech_type,
                                                  req_flags,
                                                  time_req,
                                                  input_chan_bindings,
                                                  &input_token,
                                                  &actual_mech_type,
                                                  &output_token,
                                                  &ret_flags,
                                                  &time_rec);
    EXPECT_EQ(queries[i].response_code, major_status);
    CopyBuffer(&input_token, &output_token);
    ClearBuffer(&output_token);
  }
  ClearBuffer(&input_token);
  major_status = mock_library->delete_sec_context(&minor_status,
                                                  &context_handle,
                                                  GSS_C_NO_BUFFER);
  EXPECT_EQ(static_cast<OM_uint32>(GSS_S_COMPLETE), major_status);
}

TEST(HttpAuthGSSAPITest, ParseChallenge_FirstRound) {
  // The first round should just consist of an unadorned "Negotiate" header.
  test::MockGSSAPILibrary mock_library;
  HttpAuthGSSAPI auth_gssapi(&mock_library, "Negotiate",
                             CHROME_GSS_SPNEGO_MECH_OID_DESC);
  std::string challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_gssapi.ParseChallenge(&challenge));
}

TEST(HttpAuthGSSAPITest, ParseChallenge_TwoRounds) {
  // The first round should just have "Negotiate", and the second round should
  // have a valid base64 token associated with it.
  test::MockGSSAPILibrary mock_library;
  HttpAuthGSSAPI auth_gssapi(&mock_library, "Negotiate",
                             CHROME_GSS_SPNEGO_MECH_OID_DESC);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_gssapi.ParseChallenge(&first_challenge));

  // Generate an auth token and create another thing.
  EstablishInitialContext(&mock_library);
  std::string auth_token;
  EXPECT_EQ(OK, auth_gssapi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));

  std::string second_challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_gssapi.ParseChallenge(&second_challenge));
}

TEST(HttpAuthGSSAPITest, ParseChallenge_UnexpectedTokenFirstRound) {
  // If the first round challenge has an additional authentication token, it
  // should be treated as an invalid challenge from the server.
  test::MockGSSAPILibrary mock_library;
  HttpAuthGSSAPI auth_gssapi(&mock_library, "Negotiate",
                             CHROME_GSS_SPNEGO_MECH_OID_DESC);
  std::string challenge_text = "Negotiate Zm9vYmFy";
  HttpAuthChallengeTokenizer challenge(challenge_text.begin(),
                                       challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            auth_gssapi.ParseChallenge(&challenge));
}

TEST(HttpAuthGSSAPITest, ParseChallenge_MissingTokenSecondRound) {
  // If a later-round challenge is simply "Negotiate", it should be treated as
  // an authentication challenge rejection from the server or proxy.
  test::MockGSSAPILibrary mock_library;
  HttpAuthGSSAPI auth_gssapi(&mock_library, "Negotiate",
                             CHROME_GSS_SPNEGO_MECH_OID_DESC);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_gssapi.ParseChallenge(&first_challenge));

  EstablishInitialContext(&mock_library);
  std::string auth_token;
  EXPECT_EQ(OK, auth_gssapi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));
  std::string second_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_REJECT,
            auth_gssapi.ParseChallenge(&second_challenge));
}

TEST(HttpAuthGSSAPITest, ParseChallenge_NonBase64EncodedToken) {
  // If a later-round challenge has an invalid base64 encoded token, it should
  // be treated as an invalid challenge.
  test::MockGSSAPILibrary mock_library;
  HttpAuthGSSAPI auth_gssapi(&mock_library, "Negotiate",
                             CHROME_GSS_SPNEGO_MECH_OID_DESC);
  std::string first_challenge_text = "Negotiate";
  HttpAuthChallengeTokenizer first_challenge(first_challenge_text.begin(),
                                             first_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_ACCEPT,
            auth_gssapi.ParseChallenge(&first_challenge));

  EstablishInitialContext(&mock_library);
  std::string auth_token;
  EXPECT_EQ(OK, auth_gssapi.GenerateAuthToken(
                    nullptr, "HTTP/intranet.google.com", std::string(),
                    &auth_token, base::BindOnce(&UnexpectedCallback)));
  std::string second_challenge_text = "Negotiate =happyjoy=";
  HttpAuthChallengeTokenizer second_challenge(second_challenge_text.begin(),
                                              second_challenge_text.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_INVALID,
            auth_gssapi.ParseChallenge(&second_challenge));
}

}  // namespace net
