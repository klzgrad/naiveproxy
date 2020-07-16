// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_decrypter.h"

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace {

// The AES GCM test vectors come from the file gcmDecrypt128.rsp
// downloaded from http://csrc.nist.gov/groups/STM/cavp/index.html on
// 2013-02-01. The test vectors in that file look like this:
//
// [Keylen = 128]
// [IVlen = 96]
// [PTlen = 0]
// [AADlen = 0]
// [Taglen = 128]
//
// Count = 0
// Key = cf063a34d4a9a76c2c86787d3f96db71
// IV = 113b9785971864c83b01c787
// CT =
// AAD =
// Tag = 72ac8493e3a5228b5d130a69d2510e42
// PT =
//
// Count = 1
// Key = a49a5e26a2f8cb63d05546c2a62f5343
// IV = 907763b19b9b4ab6bd4f0281
// CT =
// AAD =
// Tag = a2be08210d8c470a8df6e8fbd79ec5cf
// FAIL
//
// ...
//
// The gcmDecrypt128.rsp file is huge (2.6 MB), so I selected just a
// few test vectors for this unit test.

// Describes a group of test vectors that all have a given key length, IV
// length, plaintext length, AAD length, and tag length.
struct TestGroupInfo {
  size_t key_len;
  size_t iv_len;
  size_t pt_len;
  size_t aad_len;
  size_t tag_len;
};

// Each test vector consists of six strings of lowercase hexadecimal digits.
// The strings may be empty (zero length). A test vector with a nullptr |key|
// marks the end of an array of test vectors.
struct TestVector {
  // Input:
  const char* key;
  const char* iv;
  const char* ct;
  const char* aad;
  const char* tag;

  // Expected output:
  const char* pt;  // An empty string "" means decryption succeeded and
                   // the plaintext is zero-length. nullptr means decryption
                   // failed.
};

const TestGroupInfo test_group_info[] = {
    {128, 96, 0, 0, 128},     {128, 96, 0, 128, 128},   {128, 96, 128, 0, 128},
    {128, 96, 408, 160, 128}, {128, 96, 408, 720, 128}, {128, 96, 104, 0, 128},
};

const TestVector test_group_0[] = {
    {"cf063a34d4a9a76c2c86787d3f96db71", "113b9785971864c83b01c787", "", "",
     "72ac8493e3a5228b5d130a69d2510e42", ""},
    {
        "a49a5e26a2f8cb63d05546c2a62f5343", "907763b19b9b4ab6bd4f0281", "", "",
        "a2be08210d8c470a8df6e8fbd79ec5cf",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_1[] = {
    {
        "d1f6af919cde85661208bdce0c27cb22", "898c6929b435017bf031c3c5", "",
        "7c5faa40e636bbc91107e68010c92b9f", "ae45f11777540a2caeb128be8092468a",
        nullptr  // FAIL
    },
    {"2370e320d4344208e0ff5683f243b213", "04dbb82f044d30831c441228", "",
     "d43a8e5089eea0d026c03a85178b27da", "2a049c049d25aa95969b451d93c31c6e",
     ""},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_2[] = {
    {"e98b72a9881a84ca6b76e0f43e68647a", "8b23299fde174053f3d652ba",
     "5a3c1cf1985dbb8bed818036fdd5ab42", "", "23c7ab0f952b7091cd324835043b5eb5",
     "28286a321293253c3e0aa2704a278032"},
    {"33240636cd3236165f1a553b773e728e", "17c4d61493ecdc8f31700b12",
     "47bb7e23f7bdfe05a8091ac90e4f8b2e", "", "b723c70e931d9785f40fd4ab1d612dc9",
     "95695a5b12f2870b9cc5fdc8f218a97d"},
    {
        "5164df856f1e9cac04a79b808dc5be39", "e76925d5355e0584ce871b2b",
        "0216c899c88d6e32c958c7e553daa5bc", "",
        "a145319896329c96df291f64efbe0e3a",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_3[] = {
    {"af57f42c60c0fc5a09adb81ab86ca1c3", "a2dc01871f37025dc0fc9a79",
     "b9a535864f48ea7b6b1367914978f9bfa087d854bb0e269bed8d279d2eea1210e48947"
     "338b22f9bad09093276a331e9c79c7f4",
     "41dc38988945fcb44faf2ef72d0061289ef8efd8",
     "4f71e72bde0018f555c5adcce062e005",
     "3803a0727eeb0ade441e0ec107161ded2d425ec0d102f21f51bf2cf9947c7ec4aa7279"
     "5b2f69b041596e8817d0a3c16f8fadeb"},
    {"ebc753e5422b377d3cb64b58ffa41b61", "2e1821efaced9acf1f241c9b",
     "069567190554e9ab2b50a4e1fbf9c147340a5025fdbd201929834eaf6532325899ccb9"
     "f401823e04b05817243d2142a3589878",
     "b9673412fd4f88ba0e920f46dd6438ff791d8eef",
     "534d9234d2351cf30e565de47baece0b",
     "39077edb35e9c5a4b1e4c2a6b9bb1fce77f00f5023af40333d6d699014c2bcf4209c18"
     "353a18017f5b36bfc00b1f6dcb7ed485"},
    {
        "52bdbbf9cf477f187ec010589cb39d58", "d3be36d3393134951d324b31",
        "700188da144fa692cf46e4a8499510a53d90903c967f7f13e8a1bd8151a74adc4fe63e"
        "32b992760b3a5f99e9a47838867000a9",
        "93c4fc6a4135f54d640b0c976bf755a06a292c33",
        "8ca4e38aa3dfa6b1d0297021ccf3ea5f",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_4[] = {
    {"da2bb7d581493d692380c77105590201", "44aa3e7856ca279d2eb020c6",
     "9290d430c9e89c37f0446dbd620c9a6b34b1274aeb6f911f75867efcf95b6feda69f1a"
     "f4ee16c761b3c9aeac3da03aa9889c88",
     "4cd171b23bddb3a53cdf959d5c1710b481eb3785a90eb20a2345ee00d0bb7868c367ab"
     "12e6f4dd1dee72af4eee1d197777d1d6499cc541f34edbf45cda6ef90b3c024f9272d7"
     "2ec1909fb8fba7db88a4d6f7d3d925980f9f9f72",
     "9e3ac938d3eb0cadd6f5c9e35d22ba38",
     "9bbf4c1a2742f6ac80cb4e8a052e4a8f4f07c43602361355b717381edf9fabd4cb7e3a"
     "d65dbd1378b196ac270588dd0621f642"},
    {"d74e4958717a9d5c0e235b76a926cae8", "0b7471141e0c70b1995fd7b1",
     "e701c57d2330bf066f9ff8cf3ca4343cafe4894651cd199bdaaa681ba486b4a65c5a22"
     "b0f1420be29ea547d42c713bc6af66aa",
     "4a42b7aae8c245c6f1598a395316e4b8484dbd6e64648d5e302021b1d3fa0a38f46e22"
     "bd9c8080b863dc0016482538a8562a4bd0ba84edbe2697c76fd039527ac179ec5506cf"
     "34a6039312774cedebf4961f3978b14a26509f96",
     "e192c23cb036f0b31592989119eed55d",
     "840d9fb95e32559fb3602e48590280a172ca36d9b49ab69510f5bd552bfab7a306f85f"
     "f0a34bc305b88b804c60b90add594a17"},
    {
        "1986310c725ac94ecfe6422e75fc3ee7", "93ec4214fa8e6dc4e3afc775",
        "b178ec72f85a311ac4168f42a4b2c23113fbea4b85f4b9dabb74e143eb1b8b0a361e02"
        "43edfd365b90d5b325950df0ada058f9",
        "e80b88e62c49c958b5e0b8b54f532d9ff6aa84c8a40132e93e55b59fc24e8decf28463"
        "139f155d1e8ce4ee76aaeefcd245baa0fc519f83a5fb9ad9aa40c4b21126013f576c42"
        "72c2cb136c8fd091cc4539877a5d1e72d607f960",
        "8b347853f11d75e81e8a95010be81f17",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_5[] = {
    {"387218b246c1a8257748b56980e50c94", "dd7e014198672be39f95b69d",
     "cdba9e73eaf3d38eceb2b04a8d", "", "ecf90f4a47c9c626d6fb2c765d201556",
     "48f5b426baca03064554cc2b30"},
    {"294de463721e359863887c820524b3d4", "3338b35c9d57a5d28190e8c9",
     "2f46634e74b8e4c89812ac83b9", "", "dabd506764e68b82a7e720aa18da0abe",
     "46a2e55c8e264df211bd112685"},
    {"28ead7fd2179e0d12aa6d5d88c58c2dc", "5055347f18b4d5add0ae5c41",
     "142d8210c3fb84774cdbd0447a", "", "5fd321d9cdb01952dc85f034736c2a7d",
     "3b95b981086ee73cc4d0cc1422"},
    {
        "7d7b6c988137b8d470c57bf674a09c87", "9edf2aa970d016ac962e1fd8",
        "a85b66c3cb5eab91d5bdc8bc0e", "", "dc054efc01f3afd21d9c2484819f569a",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector* const test_group_array[] = {
    test_group_0, test_group_1, test_group_2,
    test_group_3, test_group_4, test_group_5,
};

}  // namespace

namespace quic {
namespace test {

// DecryptWithNonce wraps the |Decrypt| method of |decrypter| to allow passing
// in an nonce and also to allocate the buffer needed for the plaintext.
QuicData* DecryptWithNonce(Aes128Gcm12Decrypter* decrypter,
                           quiche::QuicheStringPiece nonce,
                           quiche::QuicheStringPiece associated_data,
                           quiche::QuicheStringPiece ciphertext) {
  uint64_t packet_number;
  quiche::QuicheStringPiece nonce_prefix(nonce.data(),
                                         nonce.size() - sizeof(packet_number));
  decrypter->SetNoncePrefix(nonce_prefix);
  memcpy(&packet_number, nonce.data() + nonce_prefix.size(),
         sizeof(packet_number));
  std::unique_ptr<char[]> output(new char[ciphertext.length()]);
  size_t output_length = 0;
  const bool success = decrypter->DecryptPacket(
      packet_number, associated_data, ciphertext, output.get(), &output_length,
      ciphertext.length());
  if (!success) {
    return nullptr;
  }
  return new QuicData(output.release(), output_length, true);
}

class Aes128Gcm12DecrypterTest : public QuicTest {};

TEST_F(Aes128Gcm12DecrypterTest, Decrypt) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(test_group_array); i++) {
    SCOPED_TRACE(i);
    const TestVector* test_vectors = test_group_array[i];
    const TestGroupInfo& test_info = test_group_info[i];
    for (size_t j = 0; test_vectors[j].key != nullptr; j++) {
      // If not present then decryption is expected to fail.
      bool has_pt = test_vectors[j].pt;

      // Decode the test vector.
      std::string key = quiche::QuicheTextUtils::HexDecode(test_vectors[j].key);
      std::string iv = quiche::QuicheTextUtils::HexDecode(test_vectors[j].iv);
      std::string ct = quiche::QuicheTextUtils::HexDecode(test_vectors[j].ct);
      std::string aad = quiche::QuicheTextUtils::HexDecode(test_vectors[j].aad);
      std::string tag = quiche::QuicheTextUtils::HexDecode(test_vectors[j].tag);
      std::string pt;
      if (has_pt) {
        pt = quiche::QuicheTextUtils::HexDecode(test_vectors[j].pt);
      }

      // The test vector's lengths should look sane. Note that the lengths
      // in |test_info| are in bits.
      EXPECT_EQ(test_info.key_len, key.length() * 8);
      EXPECT_EQ(test_info.iv_len, iv.length() * 8);
      EXPECT_EQ(test_info.pt_len, ct.length() * 8);
      EXPECT_EQ(test_info.aad_len, aad.length() * 8);
      EXPECT_EQ(test_info.tag_len, tag.length() * 8);
      if (has_pt) {
        EXPECT_EQ(test_info.pt_len, pt.length() * 8);
      }

      // The test vectors have 16 byte authenticators but this code only uses
      // the first 12.
      ASSERT_LE(static_cast<size_t>(Aes128Gcm12Decrypter::kAuthTagSize),
                tag.length());
      tag.resize(Aes128Gcm12Decrypter::kAuthTagSize);
      std::string ciphertext = ct + tag;

      Aes128Gcm12Decrypter decrypter;
      ASSERT_TRUE(decrypter.SetKey(key));

      std::unique_ptr<QuicData> decrypted(DecryptWithNonce(
          &decrypter, iv,
          // This deliberately tests that the decrypter can
          // handle an AAD that is set to nullptr, as opposed
          // to a zero-length, non-nullptr pointer.
          aad.length() ? aad : quiche::QuicheStringPiece(), ciphertext));
      if (!decrypted) {
        EXPECT_FALSE(has_pt);
        continue;
      }
      EXPECT_TRUE(has_pt);

      ASSERT_EQ(pt.length(), decrypted->length());
      quiche::test::CompareCharArraysWithHexError(
          "plaintext", decrypted->data(), pt.length(), pt.data(), pt.length());
    }
  }
}

}  // namespace test
}  // namespace quic
