// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_256_gcm_decrypter.h"

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

// The AES GCM test vectors come from the file gcmDecrypt256.rsp
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
// Key = f5a2b27c74355872eb3ef6c5feafaa740e6ae990d9d48c3bd9bb8235e589f010
// IV = 58d2240f580a31c1d24948e9
// CT =
// AAD =
// Tag = 15e051a5e4a5f5da6cea92e2ebee5bac
// PT =
//
// Count = 1
// Key = e5a8123f2e2e007d4e379ba114a2fb66e6613f57c72d4e4f024964053028a831
// IV = 51e43385bf533e168427e1ad
// CT =
// AAD =
// Tag = 38fe845c66e66bdd884c2aecafd280e6
// FAIL
//
// ...
//
// The gcmDecrypt256.rsp file is huge (3.0 MB), so a few test vectors were
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
    {256, 96, 0, 0, 128},     {256, 96, 0, 128, 128},   {256, 96, 128, 0, 128},
    {256, 96, 408, 160, 128}, {256, 96, 408, 720, 128}, {256, 96, 104, 0, 128},
};

const TestVector test_group_0[] = {
    {"f5a2b27c74355872eb3ef6c5feafaa740e6ae990d9d48c3bd9bb8235e589f010",
     "58d2240f580a31c1d24948e9", "", "", "15e051a5e4a5f5da6cea92e2ebee5bac",
     ""},
    {
        "e5a8123f2e2e007d4e379ba114a2fb66e6613f57c72d4e4f024964053028a831",
        "51e43385bf533e168427e1ad", "", "", "38fe845c66e66bdd884c2aecafd280e6",
        nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_1[] = {
    {"6dfdafd6703c285c01f14fd10a6012862b2af950d4733abb403b2e745b26945d",
     "3749d0b3d5bacb71be06ade6", "", "c0d249871992e70302ae008193d1e89f",
     "4aa4cc69f84ee6ac16d9bfb4e05de500", ""},
    {
        "2c392a5eb1a9c705371beda3a901c7c61dca4d93b4291de1dd0dd15ec11ffc45",
        "0723fb84a08f4ea09841f32a", "", "140be561b6171eab942c486a94d33d43",
        "aa0e1c9b57975bfc91aa137231977d2c", nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_2[] = {
    {"4c8ebfe1444ec1b2d503c6986659af2c94fafe945f72c1e8486a5acfedb8a0f8",
     "473360e0ad24889959858995", "d2c78110ac7e8f107c0df0570bd7c90c", "",
     "c26a379b6d98ef2852ead8ce83a833a7", "7789b41cb3ee548814ca0b388c10b343"},
    {"3934f363fd9f771352c4c7a060682ed03c2864223a1573b3af997e2ababd60ab",
     "efe2656d878c586e41c539c4", "e0de64302ac2d04048d65a87d2ad09fe", "",
     "33cbd8d2fb8a3a03e30c1eb1b53c1d99", "697aff2d6b77e5ed6232770e400c1ead"},
    {
        "c997768e2d14e3d38259667a6649079de77beb4543589771e5068e6cd7cd0b14",
        "835090aed9552dbdd45277e2", "9f6607d68e22ccf21928db0986be126e", "",
        "f32617f67c574fd9f44ef76ff880ab9f", nullptr  // FAIL
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_3[] = {
    {
        "e9d381a9c413bee66175d5586a189836e5c20f5583535ab4d3f3e612dc21700e",
        "23e81571da1c7821c681c7ca",
        "a25f3f580306cd5065d22a6b7e9660110af7204bb77d370f7f34bee547feeff7b32a59"
        "6fce29c9040e68b1589aad48da881990",
        "6f39c9ae7b8e8a58a95f0dd8ea6a9087cbccdfd6",
        "5b6dcd70eefb0892fab1539298b92a4b",
        nullptr  // FAIL
    },
    {"6450d4501b1e6cfbe172c4c8570363e96b496591b842661c28c2f6c908379cad",
     "7e4262035e0bf3d60e91668a",
     "5a99b336fd3cfd82f10fb08f7045012415f0d9a06bb92dcf59c6f0dbe62d433671aacb8a1"
     "c52ce7bbf6aea372bf51e2ba79406",
     "f1c522f026e4c5d43851da516a1b78768ab18171",
     "fe93b01636f7bb0458041f213e98de65",
     "17449e236ef5858f6d891412495ead4607bfae2a2d735182a2a0242f9d52fc5345ef912db"
     "e16f3bb4576fe3bcafe336dee6085"},
    {"90f2e71ccb1148979cb742efc8f921de95457d898c84ce28edeed701650d3a26",
     "aba58ad60047ba553f6e4c98",
     "3fc77a5fe9203d091c7916587c9763cf2e4d0d53ca20b078b851716f1dab4873fe342b7b3"
     "01402f015d00263bf3f77c58a99d6",
     "2abe465df6e5be47f05b92c9a93d76ae3611fac5",
     "9cb3d04637048bc0bddef803ffbb56cf",
     "1d21639640e11638a2769e3fab78778f84be3f4a8ce28dfd99cb2e75171e05ea8e94e30aa"
     "78b54bb402b39d613616a8ed951dc"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_4[] = {
    {
        "e36aca93414b13f5313e76a7244588ee116551d1f34c32859166f2eb0ac1a9b7",
        "e9e701b1ccef6bddd03391d8",
        "5b059ac6733b6de0e8cf5b88b7301c02c993426f71bb12abf692e9deeacfac1ff1644c"
        "87d4df130028f515f0feda636309a24d",
        "6a08fe6e55a08f283cec4c4b37676e770f402af6102f548ad473ec6236da764f7076ff"
        "d41bbd9611b439362d899682b7b0f839fc5a68d9df54afd1e2b3c4e7d072454ee27111"
        "d52193d28b9c4f925d2a8b451675af39191a2cba",
        "43c7c9c93cc265fc8e192000e0417b5b",
        nullptr  // FAIL
    },
    {"5f72046245d3f4a0877e50a86554bfd57d1c5e073d1ed3b5451f6d0fc2a8507a",
     "ea6f5b391e44b751b26bce6f",
     "0e6e0b2114c40769c15958d965a14dcf50b680e0185a4409d77d894ca15b1e698dd83b353"
     "6b18c05d8cd0873d1edce8150ecb5",
     "9b3a68c941d42744673fb60fea49075eae77322e7e70e34502c115b6495ebfc796d629080"
     "7653c6b53cd84281bd0311656d0013f44619d2748177e99e8f8347c989a7b59f9d8dcf00f"
     "31db0684a4a83e037e8777bae55f799b0d",
     "fdaaff86ceb937502cd9012d03585800",
     "b0a881b751cc1eb0c912a4cf9bd971983707dbd2411725664503455c55db25cdb19bc669c"
     "2654a3a8011de6bf7eff3f9f07834"},
    {"ab639bae205547607506522bd3cdca7861369e2b42ef175ff135f6ba435d5a8e",
     "5fbb63eb44bd59fee458d8f6",
     "9a34c62bed0972285503a32812877187a54dedbd55d2317fed89282bf1af4ba0b6bb9f9e1"
     "6dd86da3b441deb7841262bc6bd63",
     "1ef2b1768b805587935ffaf754a11bd2a305076d6374f1f5098b1284444b78f55408a786d"
     "a37e1b7f1401c330d3585ef56f3e4d35eaaac92e1381d636477dc4f4beaf559735e902d6b"
     "e58723257d4ac1ed9bd213de387f35f3c4",
     "e0299e079bff46fd12e36d1c60e41434",
     "e5a3ce804a8516cdd12122c091256b789076576040dbf3c55e8be3c016025896b8a72532b"
     "fd51196cc82efca47aa0fd8e2e0dc"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

const TestVector test_group_5[] = {
    {
        "8b37c4b8cf634704920059866ad96c49e9da502c63fca4a3a7a4dcec74cb0610",
        "cb59344d2b06c4ae57cd0ea4", "66ab935c93555e786b775637a3", "",
        "d8733acbb564d8afaa99d7ca2e2f92a9", nullptr  // FAIL
    },
    {"a71dac1377a3bf5d7fb1b5e36bee70d2e01de2a84a1c1009ba7448f7f26131dc",
     "c5b60dda3f333b1146e9da7c", "43af49ec1ae3738a20755034d6", "",
     "6f80b6ef2d8830a55eb63680a8dff9e0", "5b87141335f2becac1a559e05f"},
    {"dc1f64681014be221b00793bbcf5a5bc675b968eb7a3a3d5aa5978ef4fa45ecc",
     "056ae9a1a69e38af603924fe", "33013a48d9ea0df2911d583271", "",
     "5b8f9cc22303e979cd1524187e9f70fe", "2a7e05612191c8bce2f529dca9"},
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
QuicData* DecryptWithNonce(Aes256GcmDecrypter* decrypter,
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

class Aes256GcmDecrypterTest : public QuicTest {};

TEST_F(Aes256GcmDecrypterTest, Decrypt) {
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
      std::string ciphertext = ct + tag;

      Aes256GcmDecrypter decrypter;
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

TEST_F(Aes256GcmDecrypterTest, GenerateHeaderProtectionMask) {
  Aes256GcmDecrypter decrypter;
  std::string key = quiche::QuicheTextUtils::HexDecode(
      "ed23ecbf54d426def5c52c3dcfc84434e62e57781d3125bb21ed91b7d3e07788");
  std::string sample =
      quiche::QuicheTextUtils::HexDecode("4d190c474be2b8babafb49ec4e38e810");
  QuicDataReader sample_reader(sample.data(), sample.size());
  ASSERT_TRUE(decrypter.SetHeaderProtectionKey(key));
  std::string mask = decrypter.GenerateHeaderProtectionMask(&sample_reader);
  std::string expected_mask =
      quiche::QuicheTextUtils::HexDecode("db9ed4e6ccd033af2eae01407199c56e");
  quiche::test::CompareCharArraysWithHexError(
      "header protection mask", mask.data(), mask.size(), expected_mask.data(),
      expected_mask.size());
}

}  // namespace test
}  // namespace quic
