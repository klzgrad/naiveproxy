// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"

#include <cstdint>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicErrorCodesTest : public QuicTest {};

TEST_F(QuicErrorCodesTest, QuicRstStreamErrorCodeToString) {
  EXPECT_STREQ("QUIC_BAD_APPLICATION_PAYLOAD",
               QuicRstStreamErrorCodeToString(QUIC_BAD_APPLICATION_PAYLOAD));
}

TEST_F(QuicErrorCodesTest, QuicErrorCodeToString) {
  EXPECT_STREQ("QUIC_NO_ERROR", QuicErrorCodeToString(QUIC_NO_ERROR));
}

TEST_F(QuicErrorCodesTest, QuicIetfTransportErrorCodeString) {
  EXPECT_EQ("Private(65280)",
            QuicIetfTransportErrorCodeString(
                static_cast<quic::QuicIetfTransportErrorCodes>(0xff00u)));

  EXPECT_EQ("CRYPTO_ERROR(missing extension)",
            QuicIetfTransportErrorCodeString(
                static_cast<quic::QuicIetfTransportErrorCodes>(
                    CRYPTO_ERROR_FIRST + SSL_AD_MISSING_EXTENSION)));

  EXPECT_EQ("NO_IETF_QUIC_ERROR",
            QuicIetfTransportErrorCodeString(NO_IETF_QUIC_ERROR));
  EXPECT_EQ("INTERNAL_ERROR", QuicIetfTransportErrorCodeString(INTERNAL_ERROR));
  EXPECT_EQ("SERVER_BUSY_ERROR",
            QuicIetfTransportErrorCodeString(SERVER_BUSY_ERROR));
  EXPECT_EQ("FLOW_CONTROL_ERROR",
            QuicIetfTransportErrorCodeString(FLOW_CONTROL_ERROR));
  EXPECT_EQ("STREAM_LIMIT_ERROR",
            QuicIetfTransportErrorCodeString(STREAM_LIMIT_ERROR));
  EXPECT_EQ("STREAM_STATE_ERROR",
            QuicIetfTransportErrorCodeString(STREAM_STATE_ERROR));
  EXPECT_EQ("FINAL_SIZE_ERROR",
            QuicIetfTransportErrorCodeString(FINAL_SIZE_ERROR));
  EXPECT_EQ("FRAME_ENCODING_ERROR",
            QuicIetfTransportErrorCodeString(FRAME_ENCODING_ERROR));
  EXPECT_EQ("TRANSPORT_PARAMETER_ERROR",
            QuicIetfTransportErrorCodeString(TRANSPORT_PARAMETER_ERROR));
  EXPECT_EQ("CONNECTION_ID_LIMIT_ERROR",
            QuicIetfTransportErrorCodeString(CONNECTION_ID_LIMIT_ERROR));
  EXPECT_EQ("PROTOCOL_VIOLATION",
            QuicIetfTransportErrorCodeString(PROTOCOL_VIOLATION));
  EXPECT_EQ("INVALID_TOKEN", QuicIetfTransportErrorCodeString(INVALID_TOKEN));
  EXPECT_EQ("CRYPTO_BUFFER_EXCEEDED",
            QuicIetfTransportErrorCodeString(CRYPTO_BUFFER_EXCEEDED));

  EXPECT_EQ("Unknown(1024)",
            QuicIetfTransportErrorCodeString(
                static_cast<quic::QuicIetfTransportErrorCodes>(0x400)));
}

TEST_F(QuicErrorCodesTest, QuicErrorCodeToTransportErrorCode) {
  for (int internal_error_code = 0; internal_error_code < QUIC_LAST_ERROR;
       ++internal_error_code) {
    std::string internal_error_code_string =
        QuicErrorCodeToString(static_cast<QuicErrorCode>(internal_error_code));
    if (internal_error_code_string == "INVALID_ERROR_CODE") {
      // Not a valid QuicErrorCode.
      continue;
    }
    QuicErrorCodeToIetfMapping ietf_error_code =
        QuicErrorCodeToTransportErrorCode(
            static_cast<QuicErrorCode>(internal_error_code));
    if (ietf_error_code.is_transport_close) {
      QuicIetfTransportErrorCodes transport_error_code =
          static_cast<QuicIetfTransportErrorCodes>(ietf_error_code.error_code);
      bool is_valid_transport_error_code = transport_error_code <= 0x0d;
      EXPECT_TRUE(is_valid_transport_error_code) << internal_error_code_string;
    } else {
      // Non-transport errors are application errors, either HTTP/3 or QPACK.
      uint64_t application_error_code = ietf_error_code.error_code;
      bool is_valid_http3_error_code =
          application_error_code >= 0x100 && application_error_code <= 0x110;
      bool is_valid_qpack_error_code =
          application_error_code >= 0x200 && application_error_code <= 0x202;
      EXPECT_TRUE(is_valid_http3_error_code || is_valid_qpack_error_code)
          << internal_error_code_string;
    }
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
