// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/secure_hash.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "crypto/sha2.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(SecureHashTest, TestUpdate) {
  // Example B.3 from FIPS 180-2: long message.
  std::string input3(500000, 'a');  // 'a' repeated half a million times
  const int kExpectedHashOfInput3[] = {
      0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7,
      0xe2, 0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97,
      0x20, 0x0e, 0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0};

  uint8_t output3[crypto::kSHA256Length];

  std::unique_ptr<crypto::SecureHash> ctx(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  ctx->Update(input3.data(), input3.size());
  ctx->Update(input3.data(), input3.size());

  ctx->Finish(output3, sizeof(output3));
  for (size_t i = 0; i < crypto::kSHA256Length; i++)
    EXPECT_EQ(kExpectedHashOfInput3[i], static_cast<int>(output3[i]));
}

TEST(SecureHashTest, TestClone) {
  std::string input1(10001, 'a');  // 'a' repeated 10001 times
  std::string input2(10001, 'd');  // 'd' repeated 10001 times

  const uint8_t kExpectedHashOfInput1[crypto::kSHA256Length] = {
      0x0c, 0xab, 0x99, 0xa0, 0x58, 0x60, 0x0f, 0xfa, 0xad, 0x12, 0x92,
      0xd0, 0xc5, 0x3c, 0x05, 0x48, 0xeb, 0xaf, 0x88, 0xdd, 0x1d, 0x01,
      0x03, 0x03, 0x45, 0x70, 0x5f, 0x01, 0x8a, 0x81, 0x39, 0x09};
  const uint8_t kExpectedHashOfInput1And2[crypto::kSHA256Length] = {
      0x4c, 0x8e, 0x26, 0x5a, 0xc3, 0x85, 0x1f, 0x1f, 0xa5, 0x04, 0x1c,
      0xc7, 0x88, 0x53, 0x1c, 0xc7, 0x80, 0x47, 0x15, 0xfb, 0x47, 0xff,
      0x72, 0xb1, 0x28, 0x37, 0xb0, 0x4d, 0x6e, 0x22, 0x2e, 0x4d};

  uint8_t output1[crypto::kSHA256Length];
  uint8_t output2[crypto::kSHA256Length];
  uint8_t output3[crypto::kSHA256Length];

  std::unique_ptr<crypto::SecureHash> ctx1(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  ctx1->Update(input1.data(), input1.size());

  std::unique_ptr<crypto::SecureHash> ctx2(ctx1->Clone());
  std::unique_ptr<crypto::SecureHash> ctx3(ctx2->Clone());
  // At this point, ctx1, ctx2, and ctx3 are all equivalent and represent the
  // state after hashing input1.

  // Updating ctx1 and ctx2 with input2 should produce equivalent results.
  ctx1->Update(input2.data(), input2.size());
  ctx1->Finish(output1, sizeof(output1));

  ctx2->Update(input2.data(), input2.size());
  ctx2->Finish(output2, sizeof(output2));

  EXPECT_EQ(0, memcmp(output1, output2, crypto::kSHA256Length));
  EXPECT_EQ(0,
            memcmp(output1, kExpectedHashOfInput1And2, crypto::kSHA256Length));

  // Finish() ctx3, which should produce the hash of input1.
  ctx3->Finish(&output3, sizeof(output3));
  EXPECT_EQ(0, memcmp(output3, kExpectedHashOfInput1, crypto::kSHA256Length));
}

TEST(SecureHashTest, TestLength) {
  std::unique_ptr<crypto::SecureHash> ctx(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  EXPECT_EQ(crypto::kSHA256Length, ctx->GetHashLength());
}

TEST(SecureHashTest, Equality) {
  std::string input1(10001, 'a');  // 'a' repeated 10001 times
  std::string input2(10001, 'd');  // 'd' repeated 10001 times

  uint8_t output1[crypto::kSHA256Length];
  uint8_t output2[crypto::kSHA256Length];

  // Call Update() twice on input1 and input2.
  std::unique_ptr<crypto::SecureHash> ctx1(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  ctx1->Update(input1.data(), input1.size());
  ctx1->Update(input2.data(), input2.size());
  ctx1->Finish(output1, sizeof(output1));

  // Call Update() once one input1 + input2 (concatenation).
  std::unique_ptr<crypto::SecureHash> ctx2(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  std::string input3 = input1 + input2;
  ctx2->Update(input3.data(), input3.size());
  ctx2->Finish(output2, sizeof(output2));

  // The hash should be the same.
  EXPECT_EQ(0, memcmp(output1, output2, crypto::kSHA256Length));
}
