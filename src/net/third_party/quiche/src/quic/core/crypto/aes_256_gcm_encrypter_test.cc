// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_256_gcm_encrypter.h"

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

// The AES GCM test vectors come from the file gcmEncryptExtIV256.rsp
// downloaded from
// https://csrc.nist.gov/Projects/Cryptographic-Algorithm-Validation-Program/CAVP-TESTING-BLOCK-CIPHER-MODES#GCMVS
// on 2017-09-27. The test vectors in that file look like this:
//
// [Keylen = 256]
// [IVlen = 96]
// [PTlen = 0]
// [AADlen = 0]
// [Taglen = 128]
//
// Count = 0
// Key = b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4
// IV = 516c33929df5a3284ff463d7
// PT =
// AAD =
// CT =
// Tag = bdc1ac884d332457a1d2664f168c76f0
//
// Count = 1
// Key = 5fe0861cdc2690ce69b3658c7f26f8458eec1c9243c5ba0845305d897e96ca0f
// IV = 770ac1a5a3d476d5d96944a1
// PT =
// AAD =
// CT =
// Tag = 196d691e1047093ca4b3d2ef4baba216
//
// ...
//
// The gcmEncryptExtIV256.rsp file is huge (3.2 MB), so a few test vectors were
// selected for this unit test.

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
  const char* key;
  const char* iv;
  const char* pt;
  const char* aad;
  const char* ct;
  const char* tag;
};

const TestGroupInfo test_group_info[] = {
    {256, 96, 0, 0, 128},     {256, 96, 0, 128, 128},   {256, 96, 128, 0, 128},
    {256, 96, 408, 160, 128}, {256, 96, 408, 720, 128}, {256, 96, 104, 0, 128},
};

const TestVector test_group_0[] = {
    {"b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4",
     "516c33929df5a3284ff463d7", "", "", "",
     "bdc1ac884d332457a1d2664f168c76f0"},
    {"5fe0861cdc2690ce69b3658c7f26f8458eec1c9243c5ba0845305d897e96ca0f",
     "770ac1a5a3d476d5d96944a1", "", "", "",
     "196d691e1047093ca4b3d2ef4baba216"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_1[] = {
    {"78dc4e0aaf52d935c3c01eea57428f00ca1fd475f5da86a49c8dd73d68c8e223",
     "d79cf22d504cc793c3fb6c8a", "", "b96baa8c1c75a671bfb2d08d06be5f36", "",
     "3e5d486aa2e30b22e040b85723a06e76"},
    {"4457ff33683cca6ca493878bdc00373893a9763412eef8cddb54f91318e0da88",
     "699d1f29d7b8c55300bb1fd2", "", "6749daeea367d0e9809e2dc2f309e6e3", "",
     "d60c74d2517fde4a74e0cd4709ed43a9"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_2[] = {
    {"31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22",
     "0d18e06c7c725ac9e362e1ce", "2db5168e932556f8089a0622981d017d", "",
     "fa4362189661d163fcd6a56d8bf0405a", "d636ac1bbedd5cc3ee727dc2ab4a9489"},
    {"460fc864972261c2560e1eb88761ff1c992b982497bd2ac36c04071cbb8e5d99",
     "8a4a16b9e210eb68bcb6f58d", "99e4e926ffe927f691893fb79a96b067", "",
     "133fc15751621b5f325c7ff71ce08324", "ec4e87e0cf74a13618d0b68636ba9fa7"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_3[] = {
    {"24501ad384e473963d476edcfe08205237acfd49b5b8f33857f8114e863fec7f",
     "9ff18563b978ec281b3f2794",
     "27f348f9cdc0c5bd5e66b1ccb63ad920ff2219d14e8d631b3872265cf117ee86757accb15"
     "8bd9abb3868fdc0d0b074b5f01b2c",
     "adb5ec720ccf9898500028bf34afccbcaca126ef",
     "eb7cb754c824e8d96f7c6d9b76c7d26fb874ffbf1d65c6f64a698d839b0b06145dae82057"
     "ad55994cf59ad7f67c0fa5e85fab8",
     "bc95c532fecc594c36d1550286a7a3f0"},
    {"fb43f5ab4a1738a30c1e053d484a94254125d55dccee1ad67c368bc1a985d235",
     "9fbb5f8252db0bca21f1c230",
     "34b797bb82250e23c5e796db2c37e488b3b99d1b981cea5e5b0c61a0b39adb6bd6ef1f507"
     "22e2e4f81115cfcf53f842e2a6c08",
     "98f8ae1735c39f732e2cbee1156dabeb854ec7a2",
     "871cd53d95a8b806bd4821e6c4456204d27fd704ba3d07ce25872dc604ea5c5ea13322186"
     "b7489db4fa060c1fd4159692612c8",
     "07b48e4a32fac47e115d7ac7445d8330"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_4[] = {
    {"148579a3cbca86d5520d66c0ec71ca5f7e41ba78e56dc6eebd566fed547fe691",
     "b08a5ea1927499c6ecbfd4e0",
     "9d0b15fdf1bd595f91f8b3abc0f7dec927dfd4799935a1795d9ce00c9b879434420fe42c2"
     "75a7cd7b39d638fb81ca52b49dc41",
     "e4f963f015ffbb99ee3349bbaf7e8e8e6c2a71c230a48f9d59860a29091d2747e01a5ca57"
     "2347e247d25f56ba7ae8e05cde2be3c97931292c02370208ecd097ef692687fecf2f419d3"
     "200162a6480a57dad408a0dfeb492e2c5d",
     "2097e372950a5e9383c675e89eea1c314f999159f5611344b298cda45e62843716f215f82"
     "ee663919c64002a5c198d7878fd3f",
     "adbecdb0d5c2224d804d2886ff9a5760"},
    {"e49af19182faef0ebeeba9f2d3be044e77b1212358366e4ef59e008aebcd9788",
     "e7f37d79a6a487a5a703edbb",
     "461cd0caf7427a3d44408d825ed719237272ecd503b9094d1f62c97d63ed83a0b50bdc804"
     "ffdd7991da7a5b6dcf48d4bcd2cbc",
     "19a9a1cfc647346781bef51ed9070d05f99a0e0192a223c5cd2522dbdf97d9739dd39fb17"
     "8ade3339e68774b058aa03e9a20a9a205bc05f32381df4d63396ef691fefd5a71b49a2ad8"
     "2d5ea428778ca47ee1398792762413cff4",
     "32ca3588e3e56eb4c8301b009d8b84b8a900b2b88ca3c21944205e9dd7311757b51394ae9"
     "0d8bb3807b471677614f4198af909",
     "3e403d035c71d88f1be1a256c89ba6ad"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_5[] = {
    {"82c4f12eeec3b2d3d157b0f992d292b237478d2cecc1d5f161389b97f999057a",
     "7b40b20f5f397177990ef2d1", "982a296ee1cd7086afad976945", "",
     "ec8e05a0471d6b43a59ca5335f", "113ddeafc62373cac2f5951bb9165249"},
    {"db4340af2f835a6c6d7ea0ca9d83ca81ba02c29b7410f221cb6071114e393240",
     "40e438357dd80a85cac3349e", "8ddb3397bd42853193cb0f80c9", "",
     "b694118c85c41abf69e229cb0f", "c07f1b8aafbd152f697eb67f2a85fe45"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector* const test_group_array[] = {
    test_group_0, test_group_1, test_group_2,
    test_group_3, test_group_4, test_group_5,
};

}  // namespace

namespace quic {
namespace test {

// EncryptWithNonce wraps the |Encrypt| method of |encrypter| to allow passing
// in an nonce and also to allocate the buffer needed for the ciphertext.
QuicData* EncryptWithNonce(Aes256GcmEncrypter* encrypter,
                           quiche::QuicheStringPiece nonce,
                           quiche::QuicheStringPiece associated_data,
                           quiche::QuicheStringPiece plaintext) {
  size_t ciphertext_size = encrypter->GetCiphertextSize(plaintext.length());
  std::unique_ptr<char[]> ciphertext(new char[ciphertext_size]);

  if (!encrypter->Encrypt(nonce, associated_data, plaintext,
                          reinterpret_cast<unsigned char*>(ciphertext.get()))) {
    return nullptr;
  }

  return new QuicData(ciphertext.release(), ciphertext_size, true);
}

class Aes256GcmEncrypterTest : public QuicTest {};

TEST_F(Aes256GcmEncrypterTest, Encrypt) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(test_group_array); i++) {
    SCOPED_TRACE(i);
    const TestVector* test_vectors = test_group_array[i];
    const TestGroupInfo& test_info = test_group_info[i];
    for (size_t j = 0; test_vectors[j].key != nullptr; j++) {
      // Decode the test vector.
      std::string key = quiche::QuicheTextUtils::HexDecode(test_vectors[j].key);
      std::string iv = quiche::QuicheTextUtils::HexDecode(test_vectors[j].iv);
      std::string pt = quiche::QuicheTextUtils::HexDecode(test_vectors[j].pt);
      std::string aad = quiche::QuicheTextUtils::HexDecode(test_vectors[j].aad);
      std::string ct = quiche::QuicheTextUtils::HexDecode(test_vectors[j].ct);
      std::string tag = quiche::QuicheTextUtils::HexDecode(test_vectors[j].tag);

      // The test vector's lengths should look sane. Note that the lengths
      // in |test_info| are in bits.
      EXPECT_EQ(test_info.key_len, key.length() * 8);
      EXPECT_EQ(test_info.iv_len, iv.length() * 8);
      EXPECT_EQ(test_info.pt_len, pt.length() * 8);
      EXPECT_EQ(test_info.aad_len, aad.length() * 8);
      EXPECT_EQ(test_info.pt_len, ct.length() * 8);
      EXPECT_EQ(test_info.tag_len, tag.length() * 8);

      Aes256GcmEncrypter encrypter;
      ASSERT_TRUE(encrypter.SetKey(key));
      std::unique_ptr<QuicData> encrypted(EncryptWithNonce(
          &encrypter, iv,
          // This deliberately tests that the encrypter can
          // handle an AAD that is set to nullptr, as opposed
          // to a zero-length, non-nullptr pointer.
          aad.length() ? aad : quiche::QuicheStringPiece(), pt));
      ASSERT_TRUE(encrypted.get());

      ASSERT_EQ(ct.length() + tag.length(), encrypted->length());
      quiche::test::CompareCharArraysWithHexError(
          "ciphertext", encrypted->data(), ct.length(), ct.data(), ct.length());
      quiche::test::CompareCharArraysWithHexError(
          "authentication tag", encrypted->data() + ct.length(), tag.length(),
          tag.data(), tag.length());
    }
  }
}

TEST_F(Aes256GcmEncrypterTest, GetMaxPlaintextSize) {
  Aes256GcmEncrypter encrypter;
  EXPECT_EQ(1000u, encrypter.GetMaxPlaintextSize(1016));
  EXPECT_EQ(100u, encrypter.GetMaxPlaintextSize(116));
  EXPECT_EQ(10u, encrypter.GetMaxPlaintextSize(26));
}

TEST_F(Aes256GcmEncrypterTest, GetCiphertextSize) {
  Aes256GcmEncrypter encrypter;
  EXPECT_EQ(1016u, encrypter.GetCiphertextSize(1000));
  EXPECT_EQ(116u, encrypter.GetCiphertextSize(100));
  EXPECT_EQ(26u, encrypter.GetCiphertextSize(10));
}

TEST_F(Aes256GcmEncrypterTest, GenerateHeaderProtectionMask) {
  Aes256GcmEncrypter encrypter;
  std::string key = quiche::QuicheTextUtils::HexDecode(
      "ed23ecbf54d426def5c52c3dcfc84434e62e57781d3125bb21ed91b7d3e07788");
  std::string sample =
      quiche::QuicheTextUtils::HexDecode("4d190c474be2b8babafb49ec4e38e810");
  ASSERT_TRUE(encrypter.SetHeaderProtectionKey(key));
  std::string mask = encrypter.GenerateHeaderProtectionMask(sample);
  std::string expected_mask =
      quiche::QuicheTextUtils::HexDecode("db9ed4e6ccd033af2eae01407199c56e");
  quiche::test::CompareCharArraysWithHexError(
      "header protection mask", mask.data(), mask.size(), expected_mask.data(),
      expected_mask.size());
}

}  // namespace test
}  // namespace quic
