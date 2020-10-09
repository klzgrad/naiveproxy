// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_encrypter.h"

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

// The AES GCM test vectors come from the file gcmEncryptExtIV128.rsp
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
// Key = 11754cd72aec309bf52f7687212e8957
// IV = 3c819d9a9bed087615030b65
// PT =
// AAD =
// CT =
// Tag = 250327c674aaf477aef2675748cf6971
//
// Count = 1
// Key = ca47248ac0b6f8372a97ac43508308ed
// IV = ffd2b598feabc9019262d2be
// PT =
// AAD =
// CT =
// Tag = 60d20404af527d248d893ae495707d1a
//
// ...
//
// The gcmEncryptExtIV128.rsp file is huge (2.8 MB), so I selected just a
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
  const char* key;
  const char* iv;
  const char* pt;
  const char* aad;
  const char* ct;
  const char* tag;
};

const TestGroupInfo test_group_info[] = {
    {128, 96, 0, 0, 128},     {128, 96, 0, 128, 128},   {128, 96, 128, 0, 128},
    {128, 96, 408, 160, 128}, {128, 96, 408, 720, 128}, {128, 96, 104, 0, 128},
};

const TestVector test_group_0[] = {
    {"11754cd72aec309bf52f7687212e8957", "3c819d9a9bed087615030b65", "", "", "",
     "250327c674aaf477aef2675748cf6971"},
    {"ca47248ac0b6f8372a97ac43508308ed", "ffd2b598feabc9019262d2be", "", "", "",
     "60d20404af527d248d893ae495707d1a"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_1[] = {
    {"77be63708971c4e240d1cb79e8d77feb", "e0e00f19fed7ba0136a797f3", "",
     "7a43ec1d9c0a5a78a0b16533a6213cab", "",
     "209fcc8d3675ed938e9c7166709dd946"},
    {"7680c5d3ca6154758e510f4d25b98820", "f8f105f9c3df4965780321f8", "",
     "c94c410194c765e3dcc7964379758ed3", "",
     "94dca8edfcf90bb74b153c8d48a17930"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_2[] = {
    {"7fddb57453c241d03efbed3ac44e371c", "ee283a3fc75575e33efd4887",
     "d5de42b461646c255c87bd2962d3b9a2", "", "2ccda4a5415cb91e135c2a0f78c9b2fd",
     "b36d1df9b9d5e596f83e8b7f52971cb3"},
    {"ab72c77b97cb5fe9a382d9fe81ffdbed", "54cc7dc2c37ec006bcc6d1da",
     "007c5e5b3e59df24a7c355584fc1518d", "", "0e1bde206a07a9c2c1b65300f8c64997",
     "2b4401346697138c7a4891ee59867d0c"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_3[] = {
    {"fe47fcce5fc32665d2ae399e4eec72ba", "5adb9609dbaeb58cbd6e7275",
     "7c0e88c88899a779228465074797cd4c2e1498d259b54390b85e3eef1c02df60e743f1"
     "b840382c4bccaf3bafb4ca8429bea063",
     "88319d6e1d3ffa5f987199166c8a9b56c2aeba5a",
     "98f4826f05a265e6dd2be82db241c0fbbbf9ffb1c173aa83964b7cf539304373636525"
     "3ddbc5db8778371495da76d269e5db3e",
     "291ef1982e4defedaa2249f898556b47"},
    {"ec0c2ba17aa95cd6afffe949da9cc3a8", "296bce5b50b7d66096d627ef",
     "b85b3753535b825cbe5f632c0b843c741351f18aa484281aebec2f45bb9eea2d79d987"
     "b764b9611f6c0f8641843d5d58f3a242",
     "f8d00f05d22bf68599bcdeb131292ad6e2df5d14",
     "a7443d31c26bdf2a1c945e29ee4bd344a99cfaf3aa71f8b3f191f83c2adfc7a0716299"
     "5506fde6309ffc19e716eddf1a828c5a",
     "890147971946b627c40016da1ecf3e77"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_4[] = {
    {"2c1f21cf0f6fb3661943155c3e3d8492", "23cb5ff362e22426984d1907",
     "42f758836986954db44bf37c6ef5e4ac0adaf38f27252a1b82d02ea949c8a1a2dbc0d6"
     "8b5615ba7c1220ff6510e259f06655d8",
     "5d3624879d35e46849953e45a32a624d6a6c536ed9857c613b572b0333e701557a713e"
     "3f010ecdf9a6bd6c9e3e44b065208645aff4aabee611b391528514170084ccf587177f"
     "4488f33cfb5e979e42b6e1cfc0a60238982a7aec",
     "81824f0e0d523db30d3da369fdc0d60894c7a0a20646dd015073ad2732bd989b14a222"
     "b6ad57af43e1895df9dca2a5344a62cc",
     "57a3ee28136e94c74838997ae9823f3a"},
    {"d9f7d2411091f947b4d6f1e2d1f0fb2e", "e1934f5db57cc983e6b180e7",
     "73ed042327f70fe9c572a61545eda8b2a0c6e1d6c291ef19248e973aee6c312012f490"
     "c2c6f6166f4a59431e182663fcaea05a",
     "0a8a18a7150e940c3d87b38e73baee9a5c049ee21795663e264b694a949822b639092d"
     "0e67015e86363583fcf0ca645af9f43375f05fdb4ce84f411dcbca73c2220dea03a201"
     "15d2e51398344b16bee1ed7c499b353d6c597af8",
     "aaadbd5c92e9151ce3db7210b8714126b73e43436d242677afa50384f2149b831f1d57"
     "3c7891c2a91fbc48db29967ec9542b23",
     "21b51ca862cb637cdd03b99a0f93b134"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_5[] = {
    {"fe9bb47deb3a61e423c2231841cfd1fb", "4d328eb776f500a2f7fb47aa",
     "f1cc3818e421876bb6b8bbd6c9", "", "b88c5c1977b35b517b0aeae967",
     "43fd4727fe5cdb4b5b42818dea7ef8c9"},
    {"6703df3701a7f54911ca72e24dca046a", "12823ab601c350ea4bc2488c",
     "793cd125b0b84a043e3ac67717", "", "b2051c80014f42f08735a7b0cd",
     "38e6bcd29962e5f2c13626b85a877101"},
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
QuicData* EncryptWithNonce(Aes128Gcm12Encrypter* encrypter,
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

class Aes128Gcm12EncrypterTest : public QuicTest {};

TEST_F(Aes128Gcm12EncrypterTest, Encrypt) {
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

      Aes128Gcm12Encrypter encrypter;
      ASSERT_TRUE(encrypter.SetKey(key));
      std::unique_ptr<QuicData> encrypted(EncryptWithNonce(
          &encrypter, iv,
          // This deliberately tests that the encrypter can
          // handle an AAD that is set to nullptr, as opposed
          // to a zero-length, non-nullptr pointer.
          aad.length() ? aad : quiche::QuicheStringPiece(), pt));
      ASSERT_TRUE(encrypted.get());

      // The test vectors have 16 byte authenticators but this code only uses
      // the first 12.
      ASSERT_LE(static_cast<size_t>(Aes128Gcm12Encrypter::kAuthTagSize),
                tag.length());
      tag.resize(Aes128Gcm12Encrypter::kAuthTagSize);

      ASSERT_EQ(ct.length() + tag.length(), encrypted->length());
      quiche::test::CompareCharArraysWithHexError(
          "ciphertext", encrypted->data(), ct.length(), ct.data(), ct.length());
      quiche::test::CompareCharArraysWithHexError(
          "authentication tag", encrypted->data() + ct.length(), tag.length(),
          tag.data(), tag.length());
    }
  }
}

TEST_F(Aes128Gcm12EncrypterTest, GetMaxPlaintextSize) {
  Aes128Gcm12Encrypter encrypter;
  EXPECT_EQ(1000u, encrypter.GetMaxPlaintextSize(1012));
  EXPECT_EQ(100u, encrypter.GetMaxPlaintextSize(112));
  EXPECT_EQ(10u, encrypter.GetMaxPlaintextSize(22));
  EXPECT_EQ(0u, encrypter.GetMaxPlaintextSize(11));
}

TEST_F(Aes128Gcm12EncrypterTest, GetCiphertextSize) {
  Aes128Gcm12Encrypter encrypter;
  EXPECT_EQ(1012u, encrypter.GetCiphertextSize(1000));
  EXPECT_EQ(112u, encrypter.GetCiphertextSize(100));
  EXPECT_EQ(22u, encrypter.GetCiphertextSize(10));
}

}  // namespace test
}  // namespace quic
