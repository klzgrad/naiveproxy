// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/tools/simple_ticket_crypter.h"

#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/mock_clock.h"

namespace quic {
namespace test {

namespace {

constexpr QuicTime::Delta kOneDay = QuicTime::Delta::FromSeconds(60 * 60 * 24);

}  // namespace

class DecryptCallback : public quic::ProofSource::DecryptCallback {
 public:
  explicit DecryptCallback(std::vector<uint8_t>* out) : out_(out) {}

  void Run(std::vector<uint8_t> plaintext) override { *out_ = plaintext; }

 private:
  std::vector<uint8_t>* out_;
};

absl::string_view StringPiece(const std::vector<uint8_t>& in) {
  return absl::string_view(reinterpret_cast<const char*>(in.data()), in.size());
}

class SimpleTicketCrypterTest : public QuicTest {
 public:
  SimpleTicketCrypterTest() : ticket_crypter_(&mock_clock_) {}

 protected:
  MockClock mock_clock_;
  SimpleTicketCrypter ticket_crypter_;
};

TEST_F(SimpleTicketCrypterTest, EncryptDecrypt) {
  std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
  std::vector<uint8_t> ciphertext =
      ticket_crypter_.Encrypt(StringPiece(plaintext));
  EXPECT_NE(plaintext, ciphertext);

  std::vector<uint8_t> out_plaintext;
  ticket_crypter_.Decrypt(StringPiece(ciphertext),
                          std::make_unique<DecryptCallback>(&out_plaintext));
  EXPECT_EQ(out_plaintext, plaintext);
}

TEST_F(SimpleTicketCrypterTest, CiphertextsDiffer) {
  std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
  std::vector<uint8_t> ciphertext1 =
      ticket_crypter_.Encrypt(StringPiece(plaintext));
  std::vector<uint8_t> ciphertext2 =
      ticket_crypter_.Encrypt(StringPiece(plaintext));
  EXPECT_NE(ciphertext1, ciphertext2);
}

TEST_F(SimpleTicketCrypterTest, DecryptionFailureWithModifiedCiphertext) {
  std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
  std::vector<uint8_t> ciphertext =
      ticket_crypter_.Encrypt(StringPiece(plaintext));
  EXPECT_NE(plaintext, ciphertext);

  // Check that a bit flip in any byte will cause a decryption failure.
  for (size_t i = 0; i < ciphertext.size(); i++) {
    SCOPED_TRACE(i);
    std::vector<uint8_t> munged_ciphertext = ciphertext;
    munged_ciphertext[i] ^= 1;
    std::vector<uint8_t> out_plaintext;
    ticket_crypter_.Decrypt(StringPiece(munged_ciphertext),
                            std::make_unique<DecryptCallback>(&out_plaintext));
    EXPECT_TRUE(out_plaintext.empty());
  }
}

TEST_F(SimpleTicketCrypterTest, DecryptionFailureWithEmptyCiphertext) {
  std::vector<uint8_t> out_plaintext;
  ticket_crypter_.Decrypt(absl::string_view(),
                          std::make_unique<DecryptCallback>(&out_plaintext));
  EXPECT_TRUE(out_plaintext.empty());
}

TEST_F(SimpleTicketCrypterTest, KeyRotation) {
  std::vector<uint8_t> plaintext = {1, 2, 3};
  std::vector<uint8_t> ciphertext =
      ticket_crypter_.Encrypt(StringPiece(plaintext));
  EXPECT_FALSE(ciphertext.empty());

  // Advance the clock 8 days, so the key used for |ciphertext| is now the
  // previous key. Check that decryption still works.
  mock_clock_.AdvanceTime(kOneDay * 8);
  std::vector<uint8_t> out_plaintext;
  ticket_crypter_.Decrypt(StringPiece(ciphertext),
                          std::make_unique<DecryptCallback>(&out_plaintext));
  EXPECT_EQ(out_plaintext, plaintext);

  // Advance the clock 8 more days. Now the original key should be expired and
  // decryption should fail.
  mock_clock_.AdvanceTime(kOneDay * 8);
  ticket_crypter_.Decrypt(StringPiece(ciphertext),
                          std::make_unique<DecryptCallback>(&out_plaintext));
  EXPECT_TRUE(out_plaintext.empty());
}

}  // namespace test
}  // namespace quic
