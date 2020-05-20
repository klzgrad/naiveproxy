// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_types.h"

#include <cstdint>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicTypesTest : public QuicTest {};

TEST_F(QuicTypesTest, QuicIetfTransportErrorCodeString) {
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

}  // namespace
}  // namespace test
}  // namespace quic
