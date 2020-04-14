// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_tls_decrypter.h"

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace {

// The test vectors come from RFC 7539 Section 2.8.2.

// Each test vector consists of six strings of lowercase hexadecimal digits.
// The strings may be empty (zero length). A test vector with a nullptr |key|
// marks the end of an array of test vectors.
struct TestVector {
  // Input:
  const char* key;
  const char* iv;
  const char* fixed;
  const char* aad;
  const char* ct;

  // Expected output:
  const char* pt;  // An empty string "" means decryption succeeded and
                   // the plaintext is zero-length. nullptr means decryption
                   // failed.
};

const TestVector test_vectors[] = {
    {"808182838485868788898a8b8c8d8e8f"
     "909192939495969798999a9b9c9d9e9f",

     "4041424344454647",

     "07000000",

     "50515253c0c1c2c3c4c5c6c7",

     "d31a8d34648e60db7b86afbc53ef7ec2"
     "a4aded51296e08fea9e2b5a736ee62d6"
     "3dbea45e8ca9671282fafb69da92728b"
     "1a71de0a9e060b2905d6a5b67ecd3b36"
     "92ddbd7f2d778b8c9803aee328091b58"
     "fab324e4fad675945585808b4831d7bc"
     "3ff4def08e4b7a9de576d26586cec64b"
     "6116"
     "1ae10b594f09e26a7e902ecbd0600691",

     "4c616469657320616e642047656e746c"
     "656d656e206f662074686520636c6173"
     "73206f66202739393a20496620492063"
     "6f756c64206f6666657220796f75206f"
     "6e6c79206f6e652074697020666f7220"
     "746865206675747572652c2073756e73"
     "637265656e20776f756c642062652069"
     "742e"},
    // Modify the ciphertext (Poly1305 authenticator).
    {"808182838485868788898a8b8c8d8e8f"
     "909192939495969798999a9b9c9d9e9f",

     "4041424344454647",

     "07000000",

     "50515253c0c1c2c3c4c5c6c7",

     "d31a8d34648e60db7b86afbc53ef7ec2"
     "a4aded51296e08fea9e2b5a736ee62d6"
     "3dbea45e8ca9671282fafb69da92728b"
     "1a71de0a9e060b2905d6a5b67ecd3b36"
     "92ddbd7f2d778b8c9803aee328091b58"
     "fab324e4fad675945585808b4831d7bc"
     "3ff4def08e4b7a9de576d26586cec64b"
     "6116"
     "1ae10b594f09e26a7e902eccd0600691",

     nullptr},
    // Modify the associated data.
    {"808182838485868788898a8b8c8d8e8f"
     "909192939495969798999a9b9c9d9e9f",

     "4041424344454647",

     "07000000",

     "60515253c0c1c2c3c4c5c6c7",

     "d31a8d34648e60db7b86afbc53ef7ec2"
     "a4aded51296e08fea9e2b5a736ee62d6"
     "3dbea45e8ca9671282fafb69da92728b"
     "1a71de0a9e060b2905d6a5b67ecd3b36"
     "92ddbd7f2d778b8c9803aee328091b58"
     "fab324e4fad675945585808b4831d7bc"
     "3ff4def08e4b7a9de576d26586cec64b"
     "6116"
     "1ae10b594f09e26a7e902ecbd0600691",

     nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

}  // namespace

namespace quic {
namespace test {

// DecryptWithNonce wraps the |Decrypt| method of |decrypter| to allow passing
// in an nonce and also to allocate the buffer needed for the plaintext.
QuicData* DecryptWithNonce(ChaCha20Poly1305TlsDecrypter* decrypter,
                           quiche::QuicheStringPiece nonce,
                           quiche::QuicheStringPiece associated_data,
                           quiche::QuicheStringPiece ciphertext) {
  decrypter->SetIV(nonce);
  std::unique_ptr<char[]> output(new char[ciphertext.length()]);
  size_t output_length = 0;
  const bool success =
      decrypter->DecryptPacket(0, associated_data, ciphertext, output.get(),
                               &output_length, ciphertext.length());
  if (!success) {
    return nullptr;
  }
  return new QuicData(output.release(), output_length, true);
}

class ChaCha20Poly1305TlsDecrypterTest : public QuicTest {};

TEST_F(ChaCha20Poly1305TlsDecrypterTest, Decrypt) {
  for (size_t i = 0; test_vectors[i].key != nullptr; i++) {
    // If not present then decryption is expected to fail.
    bool has_pt = test_vectors[i].pt;

    // Decode the test vector.
    std::string key = quiche::QuicheTextUtils::HexDecode(test_vectors[i].key);
    std::string iv = quiche::QuicheTextUtils::HexDecode(test_vectors[i].iv);
    std::string fixed =
        quiche::QuicheTextUtils::HexDecode(test_vectors[i].fixed);
    std::string aad = quiche::QuicheTextUtils::HexDecode(test_vectors[i].aad);
    std::string ct = quiche::QuicheTextUtils::HexDecode(test_vectors[i].ct);
    std::string pt;
    if (has_pt) {
      pt = quiche::QuicheTextUtils::HexDecode(test_vectors[i].pt);
    }

    ChaCha20Poly1305TlsDecrypter decrypter;
    ASSERT_TRUE(decrypter.SetKey(key));
    std::unique_ptr<QuicData> decrypted(DecryptWithNonce(
        &decrypter, fixed + iv,
        // This deliberately tests that the decrypter can handle an AAD that
        // is set to nullptr, as opposed to a zero-length, non-nullptr pointer.
        quiche::QuicheStringPiece(aad.length() ? aad.data() : nullptr,
                                  aad.length()),
        ct));
    if (!decrypted) {
      EXPECT_FALSE(has_pt);
      continue;
    }
    EXPECT_TRUE(has_pt);

    EXPECT_EQ(16u, ct.size() - decrypted->length());
    ASSERT_EQ(pt.length(), decrypted->length());
    quiche::test::CompareCharArraysWithHexError(
        "plaintext", decrypted->data(), pt.length(), pt.data(), pt.length());
  }
}

TEST_F(ChaCha20Poly1305TlsDecrypterTest, GenerateHeaderProtectionMask) {
  ChaCha20Poly1305TlsDecrypter decrypter;
  std::string key = quiche::QuicheTextUtils::HexDecode(
      "6a067f432787bd6034dd3f08f07fc9703a27e58c70e2d88d948b7f6489923cc7");
  std::string sample =
      quiche::QuicheTextUtils::HexDecode("1210d91cceb45c716b023f492c29e612");
  QuicDataReader sample_reader(sample.data(), sample.size());
  ASSERT_TRUE(decrypter.SetHeaderProtectionKey(key));
  std::string mask = decrypter.GenerateHeaderProtectionMask(&sample_reader);
  std::string expected_mask = quiche::QuicheTextUtils::HexDecode("1cc2cd98dc");
  quiche::test::CompareCharArraysWithHexError(
      "header protection mask", mask.data(), mask.size(), expected_mask.data(),
      expected_mask.size());
}

}  // namespace test
}  // namespace quic
