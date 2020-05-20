// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/crypto_secret_boxer.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {

class CryptoSecretBoxerTest : public QuicTest {};

TEST_F(CryptoSecretBoxerTest, BoxAndUnbox) {
  quiche::QuicheStringPiece message("hello world");

  CryptoSecretBoxer boxer;
  boxer.SetKeys({std::string(CryptoSecretBoxer::GetKeySize(), 0x11)});

  const std::string box = boxer.Box(QuicRandom::GetInstance(), message);

  std::string storage;
  quiche::QuicheStringPiece result;
  EXPECT_TRUE(boxer.Unbox(box, &storage, &result));
  EXPECT_EQ(result, message);

  EXPECT_FALSE(boxer.Unbox(std::string(1, 'X') + box, &storage, &result));
  EXPECT_FALSE(
      boxer.Unbox(box.substr(1, std::string::npos), &storage, &result));
  EXPECT_FALSE(boxer.Unbox(std::string(), &storage, &result));
  EXPECT_FALSE(boxer.Unbox(
      std::string(1, box[0] ^ 0x80) + box.substr(1, std::string::npos),
      &storage, &result));
}

// Helper function to test whether one boxer can decode the output of another.
static bool CanDecode(const CryptoSecretBoxer& decoder,
                      const CryptoSecretBoxer& encoder) {
  quiche::QuicheStringPiece message("hello world");
  const std::string boxed = encoder.Box(QuicRandom::GetInstance(), message);
  std::string storage;
  quiche::QuicheStringPiece result;
  bool ok = decoder.Unbox(boxed, &storage, &result);
  if (ok) {
    EXPECT_EQ(result, message);
  }
  return ok;
}

TEST_F(CryptoSecretBoxerTest, MultipleKeys) {
  std::string key_11(CryptoSecretBoxer::GetKeySize(), 0x11);
  std::string key_12(CryptoSecretBoxer::GetKeySize(), 0x12);

  CryptoSecretBoxer boxer_11, boxer_12, boxer;
  boxer_11.SetKeys({key_11});
  boxer_12.SetKeys({key_12});
  boxer.SetKeys({key_12, key_11});

  // Neither single-key boxer can decode the other's tokens.
  EXPECT_FALSE(CanDecode(boxer_11, boxer_12));
  EXPECT_FALSE(CanDecode(boxer_12, boxer_11));

  // |boxer| encodes with the first key, which is key_12.
  EXPECT_TRUE(CanDecode(boxer_12, boxer));
  EXPECT_FALSE(CanDecode(boxer_11, boxer));

  // The boxer with both keys can decode tokens from either single-key boxer.
  EXPECT_TRUE(CanDecode(boxer, boxer_11));
  EXPECT_TRUE(CanDecode(boxer, boxer_12));

  // After we flush key_11 from |boxer|, it can no longer decode tokens from
  // |boxer_11|.
  boxer.SetKeys({key_12});
  EXPECT_FALSE(CanDecode(boxer, boxer_11));
}

}  // namespace test
}  // namespace quic
