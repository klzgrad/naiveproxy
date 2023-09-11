// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/testing/utils.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/constants.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/status_utils.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "openssl/base.h"
#include "openssl/bn.h"
#include "openssl/rsa.h"

namespace private_membership {
namespace anonymous_tokens {

absl::StatusOr<std::string> TestSign(const absl::string_view blinded_data,
                                     RSA* rsa_key) {
  if (blinded_data.empty()) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  }
  const size_t mod_size = RSA_size(rsa_key);
  if (blinded_data.size() != mod_size) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", mod_size,
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }
  // Compute a raw RSA signature.
  std::string signature(mod_size, 0);
  size_t out_len;
  if (RSA_sign_raw(/*rsa=*/rsa_key, /*out_len=*/&out_len,
                   /*out=*/reinterpret_cast<uint8_t*>(&signature[0]),
                   /*max_out=*/mod_size,
                   /*in=*/reinterpret_cast<const uint8_t*>(&blinded_data[0]),
                   /*in_len=*/mod_size,
                   /*padding=*/RSA_NO_PADDING) != kBsslSuccess) {
    return absl::InternalError(
        "RSA_sign_raw failed when called from RsaBlindSigner::Sign");
  }
  if (out_len != mod_size || out_len != signature.size()) {
    return absl::InternalError(absl::StrCat(
        "Expected value of out_len and signature.size() = ", mod_size,
        " bytes, actual value of out_len and signature.size() = ", out_len,
        " and ", signature.size(), " bytes."));
  }
  return signature;
}

absl::StatusOr<std::string> TestSignWithPublicMetadata(
    const absl::string_view blinded_data, absl::string_view public_metadata,
    const RSA& rsa_key, const bool use_rsa_public_exponent) {
  if (blinded_data.empty()) {
    return absl::InvalidArgumentError("blinded_data string is empty.");
  } else if (blinded_data.size() != RSA_size(&rsa_key)) {
    return absl::InternalError(absl::StrCat(
        "Expected blind data size = ", RSA_size(&rsa_key),
        " actual blind data size = ", blinded_data.size(), " bytes."));
  }
  // Compute new public exponent using the public metadata.
  bssl::UniquePtr<BIGNUM> new_e;
  if (use_rsa_public_exponent) {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        new_e,
        ComputeExponentWithPublicMetadataAndPublicExponent(
            *RSA_get0_n(&rsa_key), *RSA_get0_e(&rsa_key), public_metadata));
  } else {
    ANON_TOKENS_ASSIGN_OR_RETURN(
        new_e, ComputeExponentWithPublicMetadata(*RSA_get0_n(&rsa_key),
                                                 public_metadata));
  }

  // Compute phi(p) = p-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_p, NewBigNum());
  if (BN_sub(phi_p.get(), RSA_get0_p(&rsa_key), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(p): ", GetSslErrors()));
  }
  // Compute phi(q) = q-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_q, NewBigNum());
  if (BN_sub(phi_q.get(), RSA_get0_q(&rsa_key), BN_value_one()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(q): ", GetSslErrors()));
  }
  // Compute phi(n) = phi(p)*phi(q)
  ANON_TOKENS_ASSIGN_OR_RETURN(auto ctx, GetAndStartBigNumCtx());
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> phi_n, NewBigNum());
  if (BN_mul(phi_n.get(), phi_p.get(), phi_q.get(), ctx.get()) != 1) {
    return absl::InternalError(
        absl::StrCat("Unable to compute phi(n): ", GetSslErrors()));
  }
  // Compute lcm(phi(p), phi(q)).
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> lcm, NewBigNum());
  if (BN_rshift1(lcm.get(), phi_n.get()) != 1) {
    return absl::InternalError(absl::StrCat(
        "Could not compute LCM(phi(p), phi(q)): ", GetSslErrors()));
  }

  // Compute the new private exponent new_d
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_d, NewBigNum());
  if (!BN_mod_inverse(new_d.get(), new_e.get(), lcm.get(), ctx.get())) {
    return absl::InternalError(
        absl::StrCat("Could not compute private exponent d: ", GetSslErrors()));
  }

  // Compute new_dpm1 = new_d mod p-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_dpm1, NewBigNum());
  BN_mod(new_dpm1.get(), new_d.get(), phi_p.get(), ctx.get());
  // Compute new_dqm1 = new_d mod q-1
  ANON_TOKENS_ASSIGN_OR_RETURN(bssl::UniquePtr<BIGNUM> new_dqm1, NewBigNum());
  BN_mod(new_dqm1.get(), new_d.get(), phi_q.get(), ctx.get());

  bssl::UniquePtr<RSA> derived_private_key(RSA_new_private_key_large_e(
      RSA_get0_n(&rsa_key), new_e.get(), new_d.get(), RSA_get0_p(&rsa_key),
      RSA_get0_q(&rsa_key), new_dpm1.get(), new_dqm1.get(),
      RSA_get0_iqmp(&rsa_key)));
  if (!derived_private_key.get()) {
    return absl::InternalError(
        absl::StrCat("RSA_new_private_key_large_e failed: ", GetSslErrors()));
  }

  return TestSign(blinded_data, derived_private_key.get());
}

IetfStandardRsaBlindSignatureTestVector
GetIetfStandardRsaBlindSignatureTestVector() {
  IetfStandardRsaBlindSignatureTestVector test_vector = {
      // n
      absl::HexStringToBytes(
          "aec4d69addc70b990ea66a5e70603b6fee27aafebd08f2d94cbe1250c556e047a9"
          "28d635c3f45ee9b66d1bc628a03bac9b7c3f416fe20dabea8f3d7b4bbf7f963be3"
          "35d2328d67e6c13ee4a8f955e05a3283720d3e1f139c38e43e0338ad058a9495c5"
          "3377fc35be64d208f89b4aa721bf7f7d3fef837be2a80e0f8adf0bcd1eec5bb040"
          "443a2b2792fdca522a7472aed74f31a1ebe1eebc1f408660a0543dfe2a850f106a"
          "617ec6685573702eaaa21a5640a5dcaf9b74e397fa3af18a2f1b7c03ba91a63361"
          "58de420d63188ee143866ee415735d155b7c2d854d795b7bc236cffd71542df342"
          "34221a0413e142d8c61355cc44d45bda94204974557ac2704cd8b593f035a5724b"
          "1adf442e78c542cd4414fce6f1298182fb6d8e53cef1adfd2e90e1e4deec52999b"
          "dc6c29144e8d52a125232c8c6d75c706ea3cc06841c7bda33568c63a6c03817f72"
          "2b50fcf898237d788a4400869e44d90a3020923dc646388abcc914315215fcd1ba"
          "e11b1c751fd52443aac8f601087d8d42737c18a3fa11ecd4131ecae017ae0a14ac"
          "fc4ef85b83c19fed33cfd1cd629da2c4c09e222b398e18d822f77bb378dea3cb36"
          "0b605e5aa58b20edc29d000a66bd177c682a17e7eb12a63ef7c2e4183e0d898f3d"
          "6bf567ba8ae84f84f1d23bf8b8e261c3729e2fa6d07b832e07cddd1d14f55325c6"
          "f924267957121902dc19b3b32948bdead5"),
      // e
      absl::HexStringToBytes("010001"),
      // d
      absl::HexStringToBytes(
          "0d43242aefe1fb2c13fbc66e20b678c4336d20b1808c558b6e62ad16a287077180b1"
          "77e1f01b12f9c6cd6c52630257ccef26a45135a990928773f3bd2fc01a313f1dac97"
          "a51cec71cb1fd7efc7adffdeb05f1fb04812c924ed7f4a8269925dad88bd7dcfbc4e"
          "f01020ebfc60cb3e04c54f981fdbd273e69a8a58b8ceb7c2d83fbcbd6f784d052201"
          "b88a9848186f2a45c0d2826870733e6fd9aa46983e0a6e82e35ca20a439c5ee7b502"
          "a9062e1066493bdadf8b49eb30d9558ed85abc7afb29b3c9bc644199654a4676681a"
          "f4babcea4e6f71fe4565c9c1b85d9985b84ec1abf1a820a9bbebee0df1398aae2c85"
          "ab580a9f13e7743afd3108eb32100b870648fa6bc17e8abac4d3c99246b1f0ea9f7f"
          "93a5dd5458c56d9f3f81ff2216b3c3680a13591673c43194d8e6fc93fc1e37ce2986"
          "bd628ac48088bc723d8fbe293861ca7a9f4a73e9fa63b1b6d0074f5dea2a624c5249"
          "ff3ad811b6255b299d6bc5451ba7477f19c5a0db690c3e6476398b1483d10314afd3"
          "8bbaf6e2fbdbcd62c3ca9797a420ca6034ec0a83360a3ee2adf4b9d4ba29731d131b"
          "099a38d6a23cc463db754603211260e99d19affc902c915d7854554aabf608e3ac52"
          "c19b8aa26ae042249b17b2d29669b5c859103ee53ef9bdc73ba3c6b537d5c34b6d8f"
          "034671d7f3a8a6966cc4543df223565343154140fd7391c7e7be03e241f4ecfeb877"
          "a051"),
      // p
      absl::HexStringToBytes(
          "e1f4d7a34802e27c7392a3cea32a262a34dc3691bd87f3f310dc75673488930559c1"
          "20fd0410194fb8a0da55bd0b81227e843fdca6692ae80e5a5d414116d4803fca7d8c"
          "30eaaae57e44a1816ebb5c5b0606c536246c7f11985d731684150b63c9a3ad9e41b0"
          "4c0b5b27cb188a692c84696b742a80d3cd00ab891f2457443dadfeba6d6daf108602"
          "be26d7071803c67105a5426838e6889d77e8474b29244cefaf418e381b312048b457"
          "d73419213063c60ee7b0d81820165864fef93523c9635c22210956e53a8d96322493"
          "ffc58d845368e2416e078e5bcb5d2fd68ae6acfa54f9627c42e84a9d3f2774017e32"
          "ebca06308a12ecc290c7cd1156dcccfb2311"),
      // q
      absl::HexStringToBytes(
          "c601a9caea66dc3835827b539db9df6f6f5ae77244692780cd334a006ab353c80642"
          "6b60718c05245650821d39445d3ab591ed10a7339f15d83fe13f6a3dfb20b9452c6a"
          "9b42eaa62a68c970df3cadb2139f804ad8223d56108dfde30ba7d367e9b0a7a80c4f"
          "dba2fd9dde6661fc73fc2947569d2029f2870fc02d8325acf28c9afa19ecf962daa7"
          "916e21afad09eb62fe9f1cf91b77dc879b7974b490d3ebd2e95426057f35d0a3c9f4"
          "5f79ac727ab81a519a8b9285932d9b2e5ccd347e59f3f32ad9ca359115e7da008ab7"
          "406707bd0e8e185a5ed8758b5ba266e8828f8d863ae133846304a2936ad7bc7c9803"
          "879d2fc4a28e69291d73dbd799f8bc238385"),
      // message
      absl::HexStringToBytes("8f3dc6fb8c4a02f4d6352edf0907822c1210a"
                             "9b32f9bdda4c45a698c80023aa6b5"
                             "9f8cfec5fdbb36331372ebefedae7d"),
      // salt
      absl::HexStringToBytes("051722b35f458781397c3a671a7d3bd3096503940e4c4f1aa"
                             "a269d60300ce449555cd7340100df9d46944c5356825abf"),
      // inv
      absl::HexStringToBytes(
          "80682c48982407b489d53d1261b19ec8627d02b8cda5336750b8cee332ae260de57b"
          "02d72609c1e0e9f28e2040fc65b6f02d56dbd6aa9af8fde656f70495dfb723ba0117"
          "3d4707a12fddac628ca29f3e32340bd8f7ddb557cf819f6b01e445ad96f874ba2355"
          "84ee71f6581f62d4f43bf03f910f6510deb85e8ef06c7f09d9794a008be7ff2529f0"
          "ebb69decef646387dc767b74939265fec0223aa6d84d2a8a1cc912d5ca25b4e144ab"
          "8f6ba054b54910176d5737a2cff011da431bd5f2a0d2d66b9e70b39f4b050e45c0d9"
          "c16f02deda9ddf2d00f3e4b01037d7029cd49c2d46a8e1fc2c0c17520af1f4b5e25b"
          "a396afc4cd60c494a4c426448b35b49635b337cfb08e7c22a39b256dd032c00addda"
          "fb51a627f99a0e1704170ac1f1912e49d9db10ec04c19c58f420212973e0cb329524"
          "223a6aa56c7937c5dffdb5d966b6cd4cbc26f3201dd25c80960a1a111b32947bb789"
          "73d269fac7f5186530930ed19f68507540eed9e1bab8b00f00d8ca09b3f099aae461"
          "80e04e3584bd7ca054df18a1504b89d1d1675d0966c4ae1407be325cdf623cf13ff1"
          "3e4a28b594d59e3eadbadf6136eee7a59d6a444c9eb4e2198e8a974f27a39eb63af2"
          "c9af3870488b8adaad444674f512133ad80b9220e09158521614f1faadfe8505ef57"
          "b7df6813048603f0dd04f4280177a11380fbfc861dbcbd7418d62155248dad5fdec0"
          "991f"),
      // encoded_message
      absl::HexStringToBytes(
          "6e0c464d9c2f9fbc147b43570fc4f238e0d0b38870b3addcf7a4217df912ccef17a7"
          "f629aa850f63a063925f312d61d6437be954b45025e8282f9c0b1131bc8ff19a8a92"
          "8d859b37113db1064f92a27f64761c181c1e1f9b251ae5a2f8a4047573b67a270584"
          "e089beadcb13e7c82337797119712e9b849ff56e04385d144d3ca9d8d92bf78adb20"
          "b5bbeb3685f17038ec6afade3ef354429c51c687b45a7018ee3a6966b3af15c9ba8f"
          "40e6461ba0a17ef5a799672ad882bab02b518f9da7c1a962945c2e9b0f02f29b31b9"
          "cdf3e633f9d9d2a22e96e1de28e25241ca7dd04147112f578973403e0f4fd8086596"
          "5475d22294f065e17a1c4a201de93bd14223e6b1b999fd548f2f759f52db71964528"
          "b6f15b9c2d7811f2a0a35d534b8216301c47f4f04f412cae142b48c4cdff78bc54df"
          "690fd43142d750c671dd8e2e938e6a440b2f825b6dbb3e19f1d7a3c0150428a47948"
          "037c322365b7fe6fe57ac88d8f80889e9ff38177bad8c8d8d98db42908b389cb5969"
          "2a58ce275aa15acb032ca951b3e0a3404b7f33f655b7c7d83a2f8d1b6bbff49d5fce"
          "df2e030e80881aa436db27a5c0dea13f32e7d460dbf01240c2320c2bb5b3225b1714"
          "5c72d61d47c8f84d1e19417ebd8ce3638a82d395cc6f7050b6209d9283dc7b93fecc"
          "04f3f9e7f566829ac41568ef799480c733c09759aa9734e2013d7640dc6151018ea9"
          "02bc"),
      // blinded_message
      absl::HexStringToBytes(
          "10c166c6a711e81c46f45b18e5873cc4f494f003180dd7f115585d871a2893025965"
          "4fe28a54dab319cc5011204c8373b50a57b0fdc7a678bd74c523259dfe4fd5ea9f52"
          "f170e19dfa332930ad1609fc8a00902d725cfe50685c95e5b2968c9a2828a21207fc"
          "f393d15f849769e2af34ac4259d91dfd98c3a707c509e1af55647efaa31290ddf48e"
          "0133b798562af5eabd327270ac2fb6c594734ce339a14ea4fe1b9a2f81c0bc230ca5"
          "23bda17ff42a377266bc2778a274c0ae5ec5a8cbbe364fcf0d2403f7ee178d77ff28"
          "b67a20c7ceec009182dbcaa9bc99b51ebbf13b7d542be337172c6474f2cd3561219f"
          "e0dfa3fb207cff89632091ab841cf38d8aa88af6891539f263adb8eac6402c41b6eb"
          "d72984e43666e537f5f5fe27b2b5aa114957e9a580730308a5f5a9c63a1eb599f093"
          "ab401d0c6003a451931b6d124180305705845060ebba6b0036154fcef3e5e9f9e4b8"
          "7e8f084542fd1dd67e7782a5585150181c01eb6d90cb95883837384a5b91dbb606f2"
          "66059ecc51b5acbaa280e45cfd2eec8cc1cdb1b7211c8e14805ba683f9b78824b2eb"
          "005bc8a7d7179a36c152cb87c8219e5569bba911bb32a1b923ca83de0e03fb10fba7"
          "5d85c55907dda5a2606bf918b056c3808ba496a4d95532212040a5f44f37e1097f26"
          "dc27b98a51837daa78f23e532156296b64352669c94a8a855acf30533d8e0594ace7"
          "c442"),
      // blinded_signature
      absl::HexStringToBytes(
          "364f6a40dbfbc3bbb257943337eeff791a0f290898a6791283bba581d9eac90a6376"
          "a837241f5f73a78a5c6746e1306ba3adab6067c32ff69115734ce014d354e2f259d4"
          "cbfb890244fd451a497fe6ecf9aa90d19a2d441162f7eaa7ce3fc4e89fd4e76b7ae5"
          "85be2a2c0fd6fb246b8ac8d58bcb585634e30c9168a434786fe5e0b74bfe8187b47a"
          "c091aa571ffea0a864cb906d0e28c77a00e8cd8f6aba4317a8cc7bf32ce566bd1ef8"
          "0c64de041728abe087bee6cadd0b7062bde5ceef308a23bd1ccc154fd0c3a26110df"
          "6193464fc0d24ee189aea8979d722170ba945fdcce9b1b4b63349980f3a92dc2e541"
          "8c54d38a862916926b3f9ca270a8cf40dfb9772bfbdd9a3e0e0892369c18249211ba"
          "857f35963d0e05d8da98f1aa0c6bba58f47487b8f663e395091275f82941830b050b"
          "260e4767ce2fa903e75ff8970c98bfb3a08d6db91ab1746c86420ee2e909bf681cac"
          "173697135983c3594b2def673736220452fde4ddec867d40ff42dd3da36c84e3e525"
          "08b891a00f50b4f62d112edb3b6b6cc3dbd546ba10f36b03f06c0d82aeec3b25e127"
          "af545fac28e1613a0517a6095ad18a98ab79f68801e05c175e15bae21f821e80c80a"
          "b4fdec6fb34ca315e194502b8f3dcf7892b511aee45060e3994cd15e003861bc7220"
          "a2babd7b40eda03382548a34a7110f9b1779bf3ef6011361611e6bc5c0dc851e1509"
          "de1a"),
      // signature
      absl::HexStringToBytes(
          "6fef8bf9bc182cd8cf7ce45c7dcf0e6f3e518ae48f06f3c670c649ac737a8b8119"
          "a34d51641785be151a697ed7825fdfece82865123445eab03eb4bb91cecf4d6951"
          "738495f8481151b62de869658573df4e50a95c17c31b52e154ae26a04067d5ecdc"
          "1592c287550bb982a5bb9c30fd53a768cee6baabb3d483e9f1e2da954c7f4cf492"
          "fe3944d2fe456c1ecaf0840369e33fb4010e6b44bb1d721840513524d8e9a3519f"
          "40d1b81ae34fb7a31ee6b7ed641cb16c2ac999004c2191de0201457523f5a4700d"
          "d649267d9286f5c1d193f1454c9f868a57816bf5ff76c838a2eeb616a3fc9976f6"
          "5d4371deecfbab29362caebdff69c635fe5a2113da4d4d8c24f0b16a0584fa05e8"
          "0e607c5d9a2f765f1f069f8d4da21f27c2a3b5c984b4ab24899bef46c6d9323df4"
          "862fe51ce300fca40fb539c3bb7fe2dcc9409e425f2d3b95e70e9c49c5feb6ecc9"
          "d43442c33d50003ee936845892fb8be475647da9a080f5bc7f8a716590b3745c22"
          "09fe05b17992830ce15f32c7b22cde755c8a2fe50bd814a0434130b807dc1b7218"
          "d4e85342d70695a5d7f29306f25623ad1e8aa08ef71b54b8ee447b5f64e73d09bd"
          "d6c3b7ca224058d7c67cc7551e9241688ada12d859cb7646fbd3ed8b34312f3b49"
          "d69802f0eaa11bc4211c2f7a29cd5c01ed01a39001c5856fab36228f5ee2f2e111"
          "0811872fe7c865c42ed59029c706195d52"),
  };
  return test_vector;
}

std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfRsaBlindSignatureWithPublicMetadataTestVectors() {
  std::string n = absl::HexStringToBytes(
      "d6930820f71fe517bf3259d14d40209b02a5c0d3d61991c731dd7da39f8d69821552"
      "e2318d6c9ad897e603887a476ea3162c1205da9ac96f02edf31df049bd55f142134c"
      "17d4382a0e78e275345f165fbe8e49cdca6cf5c726c599dd39e09e75e0f330a33121"
      "e73976e4facba9cfa001c28b7c96f8134f9981db6750b43a41710f51da4240fe0310"
      "6c12acb1e7bb53d75ec7256da3fddd0718b89c365410fce61bc7c99b115fb4c3c318"
      "081fa7e1b65a37774e8e50c96e8ce2b2cc6b3b367982366a2bf9924c4bafdb3ff5e7"
      "22258ab705c76d43e5f1f121b984814e98ea2b2b8725cd9bc905c0bc3d75c2a8db70"
      "a7153213c39ae371b2b5dc1dafcb19d6fae9");
  std::string e = absl::HexStringToBytes("010001");
  std::string d = absl::HexStringToBytes(
      "4e21356983722aa1adedb084a483401c1127b781aac89eab103e1cfc52215494981d"
      "18dd8028566d9d499469c25476358de23821c78a6ae43005e26b394e3051b5ca206a"
      "a9968d68cae23b5affd9cbb4cb16d64ac7754b3cdba241b72ad6ddfc000facdb0f0d"
      "d03abd4efcfee1730748fcc47b7621182ef8af2eeb7c985349f62ce96ab373d2689b"
      "aeaea0e28ea7d45f2d605451920ca4ea1f0c08b0f1f6711eaa4b7cca66d58a6b916f"
      "9985480f90aca97210685ac7b12d2ec3e30a1c7b97b65a18d38a93189258aa346bf2"
      "bc572cd7e7359605c20221b8909d599ed9d38164c9c4abf396f897b9993c1e805e57"
      "4d704649985b600fa0ced8e5427071d7049d");
  std::string p = absl::HexStringToBytes(
      "dcd90af1be463632c0d5ea555256a20605af3db667475e190e3af12a34a3324c46a3"
      "094062c59fb4b249e0ee6afba8bee14e0276d126c99f4784b23009bf6168ff628ac1"
      "486e5ae8e23ce4d362889de4df63109cbd90ef93db5ae64372bfe1c55f832766f21e"
      "94ea3322eb2182f10a891546536ba907ad74b8d72469bea396f3");
  std::string q = absl::HexStringToBytes(
      "f8ba5c89bd068f57234a3cf54a1c89d5b4cd0194f2633ca7c60b91a795a56fa8c868"
      "6c0e37b1c4498b851e3420d08bea29f71d195cfbd3671c6ddc49cf4c1db5b478231e"
      "a9d91377ffa98fe95685fca20ba4623212b2f2def4da5b281ed0100b651f6db32112"
      "e4017d831c0da668768afa7141d45bbc279f1e0f8735d74395b3");

  std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector> test_vectors;
  // test_vector 1.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      absl::HexStringToBytes(
          "64b5c5d2b2ca672690df59bab774a389606d85d56f92a18a57c42eb4cb164d43"),
      // blinded_message
      absl::HexStringToBytes(
          "1b9e1057dd2d05a17ad2feba5f87a4083cc825fe06fc70f0b782062ea0043fa65ec8"
          "096ce5d403cfa2aa3b11195b2a655d694386058f6266450715a936b5764f42977c0a"
          "0933ff3054d456624734fd2c019def792f00d30b3ac2f27859ea56d835f80564a3ba"
          "59f3c876dc926b2a785378ca83f177f7b378513b36a074e7db59448fd4007b54c647"
          "91a33b61721ab3b5476165193af30f25164d480684d045a8d0782a53dd73774563e8"
          "d29e48b175534f696763abaab49fa03a055ec9246c5e398a5563cc88d02eb57d725d"
          "3fc9231ae5139aa7fcb9941060b0bf0192b8c81944fa0c54568b0ab4ea9c4c4c9829"
          "d6dbcbf8b48006b322ee51d784ac93e4bf13"),
      // blinded_signature
      absl::HexStringToBytes(
          "7ef75d9887f29f2232602acab43263afaea70313a0c90374388df5a7a7440d2584c4"
          "b4e5b886accc065bf4824b4b22370ddde7fea99d4cd67f8ed2e4a6a2b7b5869e8d4d"
          "0c52318320c5bf7b9f02bb132af7365c471e799edd111ca9441934c7db76c164b051"
          "5afc5607b8ceb584f5b1d2177d5180e57218265c07aec9ebde982f3961e7ddaa432e"
          "47297884da8f4512fe3dc9ab820121262e6a73850920299999c293b017cd800c6ec9"
          "94f76b6ace35ff4232f9502e6a52262e19c03de7cc27d95ccbf4c381d698fcfe1f20"
          "0209814e04ae2d6279883015bbf36cabf3e2350be1e175020ee9f4bb861ba409b467"
          "e23d08027a699ac36b2e5ab988390f3c0ee9"),
      // signature
      absl::HexStringToBytes(
          "abd6813bb4bbe3bc8dc9f8978655b22305e5481b35c5bdc4869b60e2d5cc74b84356"
          "416abaaca0ca8602cd061248587f0d492fee3534b19a3fe089de18e4df9f3a6ad289"
          "afb5323d7934487b8fafd25943766072bab873fa9cd69ce7328a57344c2c529fe969"
          "83ca701483ca353a98a1a9610391b7d32b13e14e8ef87d04c0f56a724800655636cf"
          "ff280d35d6b468f68f09f56e1b3acdb46bc6634b7a1eab5c25766cec3b5d97c37bbc"
          "a302286c17ff557bcf1a4a0e342ea9b2713ab7f935c8174377bace2e5926b3983407"
          "9761d9121f5df1fad47a51b03eab3d84d050c99cf1f68718101735267cca3213c0a4"
          "6c0537887ffe92ca05371e26d587313cc3f4"),
  });

  // test_vector 2.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      "",
      // message_mask
      absl::HexStringToBytes(
          "ebb56541b9a1758028033cfb085a4ffe048f072c6c82a71ce21d40842b5c0a89"),
      // blinded_message
      absl::HexStringToBytes(
          "d1fc97f30efbf116fadd9895130cdd55f939211f7db19ce9a85287227a02b33fb698"
          "b52399f81be0e1f598482000202ec89968085753eae1810f14676b514e08238c8aa7"
          "9d8b999af54e9f4282c6220d4d760716e48e5413f3228cc59ce10b8252916640de7b"
          "9b5c7dc9c2bff9f53b4fb5eb4a5f8bab49af3fd1b955d34312073d15030e7fdb44bd"
          "b23460d1c5662597f9947092def7fff955a5f3e63419ae9858c6405f9609b63c4331"
          "e0cf90d24c196bee554f2b78e0d8f6da3d4308c8d4ae9fbe18a8bb7fa4fc3b9cacd4"
          "263e5bd6e12ed891cfdfba8b50d0f37d7a9abe065238367907c685ed2c224924caf5"
          "d8fe41f5db898b09a0501d318d9f65d88cb8"),
      // blinded_signature
      absl::HexStringToBytes(
          "400c1bcdfa56624f15d04f6954908b5605dbeff4cd56f384d7531669970290d70652"
          "9d44cde4c972a1399635525a2859ef1d914b4130068ed407cfda3bd9d1259790a30f"
          "6d8c07d190aa98bf21ae9581e5d61801565d96e9eec134335958b3d0b905739e2fd9"
          "f39074da08f869089fe34de2d218062afa16170c1505c67b65af4dcc2f1aeccd4827"
          "5c3dacf96116557b7f8c7044d84e296a0501c511ba1e6201703e1dd834bf47a96e1a"
          "c4ec9b935233ed751239bd4b514b031522cd51615c1555e520312ed1fa43f55d4abe"
          "b222ee48b4746c79006966590004714039bac7fd18cdd54761924d91a4648e871458"
          "937061ef6549dd12d76e37ed417634d88914"),
      // signature
      absl::HexStringToBytes(
          "4062960edb71cc071e7d101db4f595aae4a98e0bfe6843aca3e5f48c9dfb46d505e8"
          "c19806ffa07f040313d44d0996ef9f69a86fa5946cb818a32627fe2df2a0e8035028"
          "8ae4fedfbee4193554cc1433d9d27639db8b4635265504d87dca7054c85e0c882d32"
          "887534405e6cc4e7eb4b174383e5ce4eebbfffb217f353102f6d1a0461ef89238de3"
          "1b0a0c134dfac0d2a8c533c807ccdd557c6510637596a490d5258b77410421be4076"
          "ecdf2d7e9044327e36e349751f3239681bba10fe633f1b246f5a9f694706316898c9"
          "00af2294f47267f2e9ad1e61c7f56bf643280258875d29f3745dfdb74b9bbcd5fe3d"
          "ea62d9be85e2c6f5aed68bc79f8b4a27b3de"),
  });

  // test_vector 3.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      absl::HexStringToBytes(
          "f2a4ed7c5aa338430c7026d7d92017f994ca1c8b123b236dae8666b1899059d0"),
      // blinded_message
      absl::HexStringToBytes(
          "7756a1f89fa33cfc083567e02fd865d07d6e5cd4943f030a2f94b5c23f3fe79c83c4"
          "9c594247d02885e2cd161638cff60803184c9e802a659d76a1c53340972e62e728cc"
          "70cf684ef03ce2d05cefc729e6eee2ae46afa17b6b27a64f91e4c46cc12adc58d9cb"
          "61a4306dac732c9789199cfe8bd28359d1911678e9709bc159dae34ac7aa59fd0c95"
          "962c9f4904bf04aaba8a7e774735bd03be4a02fb0864a53354a2e2f3502506318a5b"
          "03961366005c7b120f0e6b87b44bc15658c3e8985d69f6adea38c24fe5e7b4bafa1a"
          "d6dc7d729281c26dffc88bd34fcc5a5f9df9b9781f99ea47472ba8bd679aaada5952"
          "5b978ebc8a3ea2161de84b7398e4878b751b"),
      // blinded_signature
      absl::HexStringToBytes(
          "2a13f73e4e255a9d5bc6f76cf48dfbf189581c2b170600fd3ab1a3def14884621323"
          "9b9d0a981537541cb4f481a602aeebca9ef28c9fcdc63d15d4296f85d864f799edf0"
          "8e9045180571ce1f1d3beff293b18aae9d8845068cc0d9a05b822295042dc56a1a2b"
          "604c51aa65fd89e6d163fe1eac63cf603774797b7936a8b7494d43fa37039d3777b8"
          "e57cf0d95227ab29d0bd9c01b3eae9dde5fca7141919bd83a17f9b1a3b401507f3e3"
          "a8e8a2c8eb6c5c1921a781000fee65b6dd851d53c89cba2c3375f0900001c0485594"
          "9b7fa499f2a78089a6f0c9b4d36fdfcac2d846076736c5eaedaf0ae70860633e51b0"
          "de21d96c8b43c600afa2e4cc64cd66d77a8f"),
      // signature
      absl::HexStringToBytes(
          "67985949f4e7c91edd5647223170d2a9b6611a191ca48ceadb6c568828b4c415b627"
          "0b037cd8a68b5bca1992eb769aaef04549422889c8b156b9378c50e8a31c07dc1fe0"
          "a80d25b870fadbcc1435197f0a31723740f3084ecb4e762c623546f6bd7d072aa565"
          "bc2105b954244a2b03946c7d4093ba1216ec6bb65b8ca8d2f3f3c43468e80b257c54"
          "a2c2ea15f640a08183a00488c7772b10df87232ee7879bee93d17e194d6b703aeceb"
          "348c1b02ec7ce202086b6494f96a0f2d800f12e855f9c33dcd3abf6bd8044efd69d4"
          "594a974d6297365479fe6c11f6ecc5ea333031c57deb6e14509777963a25cdf8db62"
          "d6c8c68aa038555e4e3ae4411b28e43c8f57"),
  });

  // test_vector 4.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      "",
      // message_mask
      absl::HexStringToBytes(
          "ba3ea4b1e475eebe11d4bfe3a48521d3ba8cd62f3baed9ec29fbbf7ff0478bc0"),
      // blinded_message
      absl::HexStringToBytes(
          "99d725c5613ff87d16464b0375b0976bf4d47319d6946e85f0d0c2ca79eb02a4c0c2"
          "82642e090a910b80fee288f0b3b6777e517b757fc6c96ea44ac570216c8fcd868e15"
          "da4b389b0c70898c5a2ed25c1d13451e4d407fe1301c231b4dc76826b1d4cc5e64b0"
          "e28fb9c71f928ba48c87e308d851dd07fb5a7e0aa5d0dce61d1348afb4233355374e"
          "5898f63adbd5ba215332d3329786fb7c30ef04c181b267562828d8cf1295f2ef4a05"
          "ef1e03ed8fee65efb7725d8c8ae476f61a35987e40efc481bcb4b89cb363addfb2ad"
          "acf690aff5425107d29b2a75b4665d49f255c5caa856cdc0c5667de93dbf3f500db8"
          "fcce246a70a159526729d82c34df69c926a8"),
      // blinded_signature
      absl::HexStringToBytes(
          "a9678acee80b528a836e4784f0690fdddce147e5d4ac506e9ec51c11b16ee2fd5a32"
          "e382a3c3d276a681bb638b63040388d53894afab79249e159835cd6bd65849e5d139"
          "7666f03d1351aaec3eae8d3e7cba3135e7ec4e7b478ef84d79d81039693adc6b130b"
          "0771e3d6f0879723a20b7f72b476fe6fef6f21e00b9e3763a364ed918180f939c351"
          "0bb5f46b35c06a00e51f049ade9e47a8e1c3d5689bd5a43df20b73d70dcacfeed9fa"
          "23cabfbe750779997da6bc7269d08b2620acaa3daa0d9e9d4b87ef841ebcc06a4c0a"
          "f13f1d13f0808f512c50898586b4fc76d2b32858a7ddf715a095b7989d8df50654e3"
          "e05120a83cec275709cf79571d8f46af2b8e"),
      // signature
      absl::HexStringToBytes(
          "ba57951810dbea7652209eb73e3b8edafc56ca7061475a048751cbfb995aeb4ccda2"
          "e9eb309698b7c61012accc4c0414adeeb4b89cd29ba2b49b1cc661d5e7f30caee7a1"
          "2ab36d6b52b5e4d487dbff98eb2d27d552ecd09ca022352c9480ae27e10c3a49a1fd"
          "4912699cc01fba9dbbfd18d1adcec76ca4bc44100ea67b9f1e00748d80255a03371a"
          "7b8f2c160cf632499cea48f99a6c2322978bd29107d0dffdd2e4934bb7dc81c90dd6"
          "3ae744fd8e57bff5e83f98014ca502b6ace876b455d1e3673525ba01687dce998406"
          "e89100f55316147ad510e854a064d99835554de8949d3662708d5f1e43bca473c14a"
          "8b1729846c6092f18fc0e08520e9309a32de"),
  });
  return test_vectors;
}

std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector>
GetIetfPartiallyBlindRSASignatureNoPublicExponentTestVectors() {
  std::string n = absl::HexStringToBytes(
      "d6930820f71fe517bf3259d14d40209b02a5c0d3d61991c731dd7da39f8d69821552"
      "e2318d6c9ad897e603887a476ea3162c1205da9ac96f02edf31df049bd55f142134c"
      "17d4382a0e78e275345f165fbe8e49cdca6cf5c726c599dd39e09e75e0f330a33121"
      "e73976e4facba9cfa001c28b7c96f8134f9981db6750b43a41710f51da4240fe0310"
      "6c12acb1e7bb53d75ec7256da3fddd0718b89c365410fce61bc7c99b115fb4c3c318"
      "081fa7e1b65a37774e8e50c96e8ce2b2cc6b3b367982366a2bf9924c4bafdb3ff5e7"
      "22258ab705c76d43e5f1f121b984814e98ea2b2b8725cd9bc905c0bc3d75c2a8db70"
      "a7153213c39ae371b2b5dc1dafcb19d6fae9");
  std::string e = absl::HexStringToBytes("010001");
  std::string d = absl::HexStringToBytes(
      "4e21356983722aa1adedb084a483401c1127b781aac89eab103e1cfc52215494981d"
      "18dd8028566d9d499469c25476358de23821c78a6ae43005e26b394e3051b5ca206a"
      "a9968d68cae23b5affd9cbb4cb16d64ac7754b3cdba241b72ad6ddfc000facdb0f0d"
      "d03abd4efcfee1730748fcc47b7621182ef8af2eeb7c985349f62ce96ab373d2689b"
      "aeaea0e28ea7d45f2d605451920ca4ea1f0c08b0f1f6711eaa4b7cca66d58a6b916f"
      "9985480f90aca97210685ac7b12d2ec3e30a1c7b97b65a18d38a93189258aa346bf2"
      "bc572cd7e7359605c20221b8909d599ed9d38164c9c4abf396f897b9993c1e805e57"
      "4d704649985b600fa0ced8e5427071d7049d");
  std::string p = absl::HexStringToBytes(
      "dcd90af1be463632c0d5ea555256a20605af3db667475e190e3af12a34a3324c46a3"
      "094062c59fb4b249e0ee6afba8bee14e0276d126c99f4784b23009bf6168ff628ac1"
      "486e5ae8e23ce4d362889de4df63109cbd90ef93db5ae64372bfe1c55f832766f21e"
      "94ea3322eb2182f10a891546536ba907ad74b8d72469bea396f3");
  std::string q = absl::HexStringToBytes(
      "f8ba5c89bd068f57234a3cf54a1c89d5b4cd0194f2633ca7c60b91a795a56fa8c868"
      "6c0e37b1c4498b851e3420d08bea29f71d195cfbd3671c6ddc49cf4c1db5b478231e"
      "a9d91377ffa98fe95685fca20ba4623212b2f2def4da5b281ed0100b651f6db32112"
      "e4017d831c0da668768afa7141d45bbc279f1e0f8735d74395b3");

  std::vector<IetfRsaBlindSignatureWithPublicMetadataTestVector> test_vectors;
  // test_vector 1.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      "",
      // blinded_message
      absl::HexStringToBytes(
          "cfd613e27b8eb15ee0b1df0e1bdda7809a61a29e9b6e9f3ec7c345353437638e8559"
          "3a7309467e36396b0515686fe87330b312b6f89df26dc1cc88dd222186ca0bfd4ffa"
          "0fd16a9749175f3255425eb299e1807b76235befa57b28f50db02f5df76cf2f8bcb5"
          "5c3e2d39d8c4b9a0439e71c5362f35f3db768a5865b864fdf979bc48d4a29ae9e7c2"
          "ea259dc557503e2938b9c3080974bd86ad8b0daaf1d103c31549dcf767798079f888"
          "33b579424ed5b3d700162136459dc29733256f18ceb74ccf0bc542db8829ca5e0346"
          "ad3fe36654715a3686ceb69f73540efd20530a59062c13880827607c68d00993b47a"
          "d6ba017b95dfc52e567c4bf65135072b12a4"),
      // blinded_signature
      absl::HexStringToBytes(
          "ca7d4fd21085de92b514fbe423c5745680cace6ddfa864a9bd97d29f3454d5d475c6"
          "c1c7d45f5da2b7b6c3b3bc68978bb83929317da25f491fee86ef7e051e7195f35586"
          "79b18d6cd3788ac989a3960429ad0b7086945e8c4d38a1b3b52a3903381d9b1bf9f3"
          "d48f75d9bb7a808d37c7ecebfd2fea5e89df59d4014a1a149d5faecfe287a3e9557e"
          "f153299d49a4918a6dbdef3e086eeb264c0c3621bcd73367195ae9b14e67597eaa9e"
          "3796616e30e264dc8c86897ae8a6336ed2cd93416c589a058211688cf35edbd22d16"
          "e31c28ff4a5c20f1627d09a71c71af372edc18d2d7a6e39df9365fe58a34605fa1d9"
          "dc53efd5a262de849fb083429e20586e210e"),
      // signature
      absl::HexStringToBytes(
          "cdc6243cd9092a8db6175b346912f3cc55e0cf3e842b4582802358dddf6f61decc37"
          "b7a9ded0a108e0c857c12a8541985a6efad3d17f7f6cce3b5ee20016e5c36c7d552c"
          "8e8ff6b5f3f7b4ed60d62eaec7fc11e4077d7e67fc6618ee092e2005964b8cf394e3"
          "e409f331dca20683f5a631b91cae0e5e2aa89eeef4504d24b45127abdb3a79f9c71d"
          "2f95e4d16c9db0e7571a7f524d2f64438dfb32001c00965ff7a7429ce7d26136a36e"
          "be14644559d3cefc477859dcd6908053907b325a34aaf654b376fade40df4016ecb3"
          "f5e1c89fe3ec500a04dfe5c8a56cad5b086047d2f963ca73848e74cf24bb8bf1720c"
          "c9de4c78c64449e8af3e7cddb0dab1821998"),
  });

  // test_vector 2.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      absl::HexStringToBytes("68656c6c6f20776f726c64"),
      // public_metadata
      "",
      // message_mask
      "",
      // blinded_message
      absl::HexStringToBytes(
          "5e6568cd0bf7ea71ad91e0a9708abb5e97661c41812eb994b672f10aa8983151113a"
          "eaabcf1306fa5a493e3dbdd58fc8bdb61aac934fae832676bcab7abacdcc1b9c1f2a"
          "f3586ae009042293b6945fee0aeffb2d2b8a24f82614b8be39bab71a535f6d65f163"
          "1e927dbd471b0753e7a63a201c7ecd26e7fbbb5e21e02f865b64e20731004c395b0e"
          "059a92fffa4c636ac4c00db9aa086b5dd1a3dd101bb04970b12ca3f4936f246e32d3"
          "94f328cea2510554060e8d291acdbee04b8bc91e967241ba45f3509d63ded5f9b358"
          "f4216f37a885e563b7baa93a717ca7cdbe10e398d14bb2d5a1376b4a5f83226ce2c5"
          "75087bc28d743caeff9c1b11cc8bd02f5f14"),
      // blinded_signature
      absl::HexStringToBytes(
          "72c4e0f4f677aa1dbb686e23b5944b3afdc7f824711a1f7486d1ed6fa20aad255a14"
          "12885aee04c64359964e694a713da2a1684325c1c31401cac1ea39a9e454675b55f7"
          "43ff144ac605d0ed254b12d9bdd43b0e8a17c0d4711239732e45e4166261d0b16d2f"
          "29403c5f2584a29b225daa7530ba15fc9af15ed2ce8fcb126ad0b0758fd522fbf99a"
          "83e4cfe0539aa264d06a1633deee0053f45fc8a944f1468a0c0c449155139779a323"
          "0c8fa41a81858418151fa195f57ea645699f550d3cb37c549542d436071d1af74e62"
          "9f938fa4717ca9def382fc35089e4caec9e5d740c38ecb2aa88c90176d2f322866ac"
          "fd50e2b92313161e81327f889aca0c94bcb8"),
      // signature
      absl::HexStringToBytes(
          "a7ace477c1f416a40e93ddf8a454f9c626b33c5a20067d81bdfef7b88bc15de2b046"
          "24478b2134b4b23d91285d72ca4eb9c6c911cd7be2437f4e3b24426bce1a1cb52e2c"
          "8a4d13f7fd5c9b0f943b92b8bbcba805b847a0ea549dbc249f2e812bf03dd6b2588c"
          "8af22bf8b6bba56ffd8d2872b2f0ebd42ac8bd8339e5e63806199deec3cf392c078f"
          "66e72d9be817787d4832c45c1f192465d87f6f6c333ce1e8c5641c7069280443d222"
          "7f6f28ff2045acdc368f2f94c38a3c909591a27c93e1778630aeeeb623805f37c575"
          "213091f096be14ffa739ee55b3f264450210a4b2e61a9b12141ca36dd45e3b81116f"
          "c286e469b707864b017634b8a409ae99c9f1"),
  });

  // test_vector 3.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      absl::HexStringToBytes("6d65746164617461"),
      // message_mask
      "",
      // blinded_message
      absl::HexStringToBytes(
          "92d5456738e0cfe0fa770b51e6a72d633d7cec3a945459f1db96dbc500a5d1bca34a"
          "839059579759301c098231b102fb1e114bf9f892f42f902a336f4a3585b23efa906d"
          "fcb94213f4d3b39951551cedecbf51efa213ad030cf821ee3fa46a57d67429f838ff"
          "728f47111f7f1b22000a979c0f56cc581396935780d76173410d2a8a5688cd596229"
          "03008fe50af1fcc5e7cf96affad7e60fbed67996c7a377effa0f08d9273cd33536b2"
          "625c9575d10636cc964636a1500f4fcb22aabbef77fe415cbc7245c1032d34bd480e"
          "e338f55be0a79c0076d9cf9c94c0db3003a33b23c62dbd1a85f2b15db5d153b318cc"
          "a53c6d68e1e63bafa39c9a43be72f36d2569"),
      // blinded_signature
      absl::HexStringToBytes(
          "a76a1c53566a9781de04d87e8c3a0bc902b47819e7b900580654215b0a710cb563b0"
          "85b5e9fff150791f759da03a139dfc9159c21410f1e3d345b8c5dcca35211772900f"
          "85c5eec065987cbdbf303e9651196223263a713e4135d6b20bfa8fb8212341665647"
          "a9a7e07a831ccbf9e62d9366ec9ac0bbe96228e6fbb848f8f6f474cce68e3556dc88"
          "2847e9e61b5b5e02bbfd6152aeca74e8782a54ffe6552d63fb837738a05044b38f7e"
          "908c4989b202bd858695c61e12cf9d47ef276a17917e39f942871defd9747541957b"
          "1e2f8950da43c9a05ba4835bded23c24cf64edfee10dd0c70b071427cfcbb8b5eb22"
          "5daf149a6b4d42bebcc536380a9d753a8b1e"),
      // signature
      absl::HexStringToBytes(
          "02bc0f2728e2b8cd1c1b9873d4b7f5a62017430398165a6f8964842eaa19c1de2922"
          "07b74dc25ee0aa90493216d3fbf8e1b2947fd64335277b34767f987c482c69262967"
          "c8a8aaf180a4006f456c804cdc7b92d956a351ad89703cc76f69ed45f24d68e1ae03"
          "61479e0f6faf10c3b1582de2dcd2af432d57c0c89c8efb1cf3ac5f991fe9c4f0ad24"
          "473939b053674a2582518b4bd57da109f4f37bc91a2f806e82bb2b80d486d0694e66"
          "3992c9517c946607b978f557bbb769d4cd836d693c77da480cd89b916e5e4190f317"
          "711d9c7e64528a314a14bf0b9256f4c60e9ddb550583c21755ab882bdfdf22dc8402"
          "49389b1e0a2189f58e19b41c5f313cddce29"),
  });

  // test_vector 4.
  test_vectors.push_back({
      n,
      e,
      d,
      p,
      q,
      // message
      "",
      // public_metadata
      "",
      // message_mask
      "",
      // blinded_message
      absl::HexStringToBytes(
          "ba562cba0e69070dc50384456391defa410d36fa853fd235902ff5d015d688a44def"
          "6b6a7e71a69bff8ee510f5a9aa44e9afddd3e766f2423b3fc783fd1a9ab618586110"
          "987c1b3ddce62d25cae500aa92a6b886cb609829d06e67fbf28fbbf3ee7d5cc12548"
          "1dd002b908097732e0df06f288cc6eb54565f8153d480085b56ab6cb5801b482d12f"
          "50558eb3cb0eb7a4ff8fcc54d4d7fcc2f8913a401ae1d1303ead7964f2746e4804e2"
          "848bba87f53cf1412afedc82d9c383dd095e0eb6f90cc74bc4bb5ea7529ded9cde2d"
          "489575d549b884379abe6d7b71969e6a9c09f1963d2719eefccd5f2a407845961ccc"
          "1fa580a93c72902b2499d96f89e6c53fc888"),
      // blinded_signature
      absl::HexStringToBytes(
          "280c5934022fd17f7f810d4f7adf1d29ced47d098834411d672163cc793bcaad239d"
          "07c4c45048a682995950ce84703064cd8c16d6f2579f7a65b66c274faccc6c73c9d2"
          "99dcf35c96338c9b81af2f93554a78528551e04be931c8502ee6a21ef65d1fa3cd04"
          "9a993e261f85c841b75857d6bf02dd4532e14702f8f5e1261f7543535cdf9379243b"
          "5b8ca5cd69d2576276a6c25b78ab7c69d2b0c568eb57cf1731983016dece5b59e753"
          "01ca1a148154f2592c8406fee83a434f7b3192649c5be06000866ff40bf09b558c7a"
          "f4bbb9a79d5d13151e7b6e602e30c4ab70bbbce9c098c386e51b98aefab67b8efc03"
          "f048210a785fd538ee6b75ecd484c1340d91"),
      // signature
      absl::HexStringToBytes(
          "b7d45ec4db11f9b74a6b33806e486f7ee5f87c4fa7c57d08caf0ca6d3ba55e66bf07"
          "69c84b9187b9a86e49ba0cb58348f01156ac5bc2e9570fe0a8c33d0ad049a965aeb2"
          "a8d8a3cbb30f89a3da6732a9bb3d9415141be4e9052f49d422301a9cfce49947db7d"
          "52a1c620b7106ae43afbcb7cb29b9c215e0c2b4cf8d62db67224dd3de9f448f7b660"
          "7977c608595d29380b591a2bff2dff57ea2c77e9cdf69c1821ff183a7626d45bbe11"
          "97767ac577715473d18571790b1cf59ee35e64362c826246ae83923d749117b7ec1b"
          "4478ee15f990dc200745a45f175d23c8a13d2dbe58b1f9d10db71917708b19eeeab2"
          "30fe6026c249342216ee785d9422c3a8dc89"),
  });
  return test_vectors;
}

std::string RandomString(int n, std::uniform_int_distribution<int>* distr_u8,
                         std::mt19937_64* generator) {
  std::string rand(n, 0);
  for (int i = 0; i < n; ++i) {
    rand[i] = static_cast<uint8_t>((*distr_u8)(*generator));
  }
  return rand;
}

std::pair<anonymous_tokens::TestRsaPublicKey,
          anonymous_tokens::TestRsaPrivateKey>
GetStrongTestRsaKeyPair2048() {
  anonymous_tokens::TestRsaPublicKey public_key = {
      /*n=*/
      absl::HexStringToBytes(
          "b31928fd04c205d364cab9f7a5620dd8db9992dfaa41c1d29b11df91204ddc0d28a5"
          "869cfc4c8ee2fca229c487b0f529c7d782303d4f5b9d85019031b159e4a7ad7d172c"
          "cd73915f10550a7f19d63bfe438d6801a226dedc054bee2958c599cfd8513ed26ae2"
          "9a5521f6ab7ae4991404b6888d60a76eadec189492a988e4c941d3ffd8feb7bdf715"
          "ec0ceaf53707d83e3cc743ec3b7d88d5dc46b615a63d4fee9a0a391546069b811e29"
          "095d5a1319fbb70248c35711a46d3c16f1444be285aeddb33256ca775562e755ac94"
          "49bfec12cdd099c8dac96b3469764c474a88bc7e1dd19db68e9275606a8142861655"
          "4a918a951bde14ee093dbdbdbbd0892486f9"),
      /*e=*/absl::HexStringToBytes("010001")};
  anonymous_tokens::TestRsaPrivateKey private_key = {
      public_key.n, public_key.e,
      /*d=*/
      absl::HexStringToBytes(
          "1bcda61d5165c57dc1c1ef08d0f5ddec727aeee026103b44b4aa1ba8edf8e8566a9e"
          "f7bcdb360f609193a3244d645d4af529319ec785d0552dd6c649d09c81f0bdf0136e"
          "f31e23cd3c3dd7794fcb8058c2a7eb2385c6bf062d14528ebca7406f91c75b17535c"
          "8654fd06cc2c31dcc9ccc9817d6129dcf6c71631ca6ae3439132921a9c18111b4b11"
          "b421868feac7c9ed6c73c437a24dbc5b364790cf4e7ac1573e72bab1b1e456b55e2e"
          "a0a673986f2305c50122ba924db6d281a5e3efc6c03d0fdc690d4d8e4fb5f45a1c4c"
          "e5c4595fde5563e8be01170e6e7ef6396bd8d435a14028748d4ef182fbffcc4aa1b9"
          "9f86a6155cd26da9bb218a1e3b2cdce38e19"),
      /*p=*/
      absl::HexStringToBytes(
          "edec7eb7cca858e3fc1c0f4eef5f4574216c96614d3bdee1830930a0036f85f277eb"
          "6ff33a757fcf6323325b1967eae0f802dffd2a79c2c222f17c6378bc8d08e3d6ba97"
          "5e13c62e5b93e2bb561fb1587dfeb14b20cf5cce9f4518b8eb052c8e48c0b891dd94"
          "fa2fef904d45ffe00f7a1a8e77c3c34e337612eba4b40a16078f"),
      /*q=*/
      absl::HexStringToBytes(
          "c0b4895d14c4e4aca5eee0bf0e58b0da5a210a2793ca06ba8f6b8a6b70202cabc545"
          "c220922f02ca849f4ee79313e3fbdfdbdb85367b307f8fe663e108d3bdac4399836e"
          "225f1956c3d112167f24db0e429a71d2ad191465f3b99cd3370bfd7b3e8d1a5e5e78"
          "8dcfab21ddb00f1aaa73d7cb62f0228449a51d032c9f636b04f7"),
      /*dp=*/
      absl::HexStringToBytes(
          "2707b7d5f105e0e72d9170d573213ee48923261c3a2e4b26d5772979e6766213dfa6"
          "48cc2ed7ddaaa8c9ba560579eda710287094386697137fe5fb90d9da9c8c4bcc0afa"
          "0fddd0920445e358f60ce6ebec675eb04366a103e84ece7a6f5b7eeeac72a9148cb4"
          "06c2dc5ae0c24df274b78429c0ede5592bc9ffda963f4eb44473"),
      /*dq=*/
      absl::HexStringToBytes(
          "22c0ac41201cbe0cb0c41abdf9ed5ebf921f81404ff3d030d6ea9304fb2ca241bc0a"
          "ef8e862e7a3761a1854e5804ef499e3e7d215208f75f19e977bbbea6c8ff0715e950"
          "f45be82af09784c68fd96ab3f0a8ffbbf9c19b1f23cc268f24cf41c07730653ffd93"
          "8a27987a3c0ba33db0ddc15e0992baf6d67d33753e17e48b0953"),
      /*crt=*/
      absl::HexStringToBytes(
          "29402f48481599b7e44c7ab9f0cfc673266dfd9ff0e2d7f9f40b6b1d8061808eb631"
          "935fd5c1e536dd99266d79c04842b121adf361e8c7a8bc04fdb7c5ad053a8b9117cf"
          "2068142e117bdda6d2a5a01ff8f0ba28d42287612c35e5ff267a20b5da454205cdf6"
          "d24d22d4968511c16b0f1a1e55865d0b5ace0beaae0ba3bbd68e")};
  return std::make_pair(public_key, private_key);
}

std::pair<anonymous_tokens::TestRsaPublicKey,
          anonymous_tokens::TestRsaPrivateKey>
GetAnotherStrongTestRsaKeyPair2048() {
  anonymous_tokens::TestRsaPublicKey public_key = {
      /*n=*/
      absl::HexStringToBytes(
          "cfe2049a15de49dd75e828eb8f5321b44f3d4169f53f9b58b37f1aba52f87ea749b8"
          "30284857eab7f0ea3bac6b866e5f485be31cea03a7ff2c0ba7cfdbe6f070fc49e37e"
          "28f2afe90b61e12a877febb1d4ba6fc0932df332afe51e8fa702a762b944a3f80a5f"
          "ea2612cc75c59400e00df62ba4be83cc50198c39b6ac4bc6a5b4f6edaf0bdeac025d"
          "6dd03d9f0f7c2127cf3c046a7e4e7cc7bc33f246f52408df49b29696d994e190076a"
          "78142cd353c4d5fe38d9708466e49131efa416d35218cde5c380d548599b8ce39a9e"
          "fcfe04df6aa317e122ac981346dbde6af84544d8f04e1c19749e6a8fb1efff4b3c3d"
          "c3d7d2c95eefc1acd2dd80b5ff585eabfb09"),
      /*e=*/absl::HexStringToBytes("010001")};
  anonymous_tokens::TestRsaPrivateKey private_key = {
      public_key.n, public_key.e,
      /*d=*/
      absl::HexStringToBytes(
          "1176a1bf55fdf603922f9e1c67ce6a82ecb32f271910ae5aadbd8c3fc1cf99483163"
          "b53bf513d9a679291c393851333d72e53137911b1c864dab6efe01b1ad5a387f768a"
          "7723280ef24357388ce87ca2d4459334c0c877e936a88f402f1e0474c12e987db255"
          "6b64a668a1ae26e849ea325769400def607d3cefee3e1c218472ffea639163dd7e80"
          "2b20e35b3d0cd7c11229cde6ad4d73cb097c1b348f5586585d2727ff62789385665d"
          "11b16eceffd85582b58a858ca356d7011bb5e4777bf3b67fef77cc528c56a147d6d7"
          "229398bb7bb057139a9b9e7d33e5ac6f302c538b4c81901ef28adb6c530cd549d61e"
          "c78e9402fb0deaab176027fda9b0801403e7"),
      /*p=*/
      absl::HexStringToBytes(
          "fda22fbc727c67fa8b5c72c54bf5136a564de2f46697f1953f751da1cc5bde428f5a"
          "5f7007c775a14ab25d1b6996b374bfc1df6665b8e9d2914754ad1a3cebd8bf6da17e"
          "9ea0a98d289e609681fd295500d0803522696662a1564eb6d4f1422db8d8da48826d"
          "f937cd19176e41889481d1309086aee3968c2692dd893f59288b"),
      /*q=*/
      absl::HexStringToBytes(
          "d1d28de5df823cea723f6979d73d44d86c202328cd4914abffd7b2e11245c075d4e5"
          "01dca7b90249bdb273fe6f78dbc4fdf0229dcb333b9fc24ec6ffd02fcda1a8fa788e"
          "3b49f0376be5ce222ccdf92e17e651a5a53507d9687f62835b08825f53f7e3d760e9"
          "8e83533e71721b10cd8832dc1c471875655d66cb19e58bb0493b"),
      /*dp=*/
      absl::HexStringToBytes(
          "8d8e547827a9795ae326e0c36ec64464c7f04667c34eb03d6d224f3c7b5316b42d4f"
          "f20e13b965d4745d220be79d7d60fe9914b710b4e8836623da85962c44313f7dcf71"
          "5cd52c6c252c6799f8c8b3a5c68397da8fef257e8caf1fd578f981c704f0babb5758"
          "4b8cb2427bca447716f3712e5aab60b692d27bc0e235f48e2d4b"),
      /*dq=*/
      absl::HexStringToBytes(
          "72c12850379ca03a4cffb76d26b5e0a849028e982b26340319eadb6f533208dfa8ef"
          "12c49e8a85e0d4b9fbcc8524e1756cb8e005d2f393417de0dddf5cfa380999445b98"
          "d67e4abdd4ea1b81ff652b49f55247074442aba7510a92536aff4d665ba330de43a7"
          "9904e40b3bba7f69022fe23915d220635c6be7e35ea7776d93af"),
      /*crt=*/
      absl::HexStringToBytes(
          "6b7f1d159c6be9a9c4d6d4171f6e90b3c9d40abee51b891f538a653c06da423ece64"
          "7713a6192babbdc8580cfa941f4cc88952f982fe197fd2fcd29d0b6b01960361419a"
          "74182cc94acaac94ad88b000677bba8f97f4ba362019a0fe1ffeb64691ca17039ebd"
          "6ad5fec8269090d2163b54ca25f4840f46f0395fdfec83cac4eb")};
  return std::make_pair(public_key, private_key);
}

std::pair<anonymous_tokens::TestRsaPublicKey,
          anonymous_tokens::TestRsaPrivateKey>
GetStrongTestRsaKeyPair3072() {
  anonymous_tokens::TestRsaPublicKey public_key = {
      /*n=*/
      absl::HexStringToBytes(
          "bd8be57544c2b43220d80b377fa22d69226e968b9f04e321e7c9e82ec4a4849386d2"
          "c4377cf2b8ec93145fbebb6f4508266169e4a83b37671f28285fe91c75a4b721804e"
          "71a7eaea97d42cd3055e4e46e78ed10898472f92c61d981d1df20d55f89e0558eb95"
          "a13f5f8ae04aa2cbfbf99c4599702b1498ab337fe36396a39a073c5d5dbedf557e6d"
          "245f807c28a4c2f44197ae256190d9a410392ede4fdf9d337fc201bb26447fabc442"
          "b19c79c531e12922a90bada53615b12e9a54ecb033f9a22be859984e296d632c9eb2"
          "87825bb4bfb7f3d16c4f2ba30b2ca5a04512e62c993351c7039a64d865ba49eb960b"
          "176dbe7c4853db37911f7bae782732441e428992422754ca3d78a20e9cedbafa8ec2"
          "460403997c381772be64b72133c1585b0d1fe5e96a3f7e2388228826989766da37f9"
          "949d1040230cb78f88005e5e92796a285b3acdd90733ed4a111d35f4632eda15dc66"
          "9e595380331acab1e98cf872126dac05c2d7a7beff889ff39ea60cf7ac69f62bd35e"
          "6c2ff193c9037d0f500d"),
      /*e=*/absl::HexStringToBytes("010001")};
  anonymous_tokens::TestRsaPrivateKey private_key = {
      public_key.n, public_key.e,
      /*d=*/
      absl::HexStringToBytes(
          "2de57b093b3e1e1de94006ef48537fc56e55f2d41a0c37e754d5da07c10bc92263ca"
          "134310594197df4156b1bb7704f3253fff4123cf3aea186c43e27d72abb5d7b61ff8"
          "5ea2f74a18bb82a31230b4a98c96535d4e6a2645d6fd0181436801fca837b339c5c9"
          "b482c0e2c2ceafbecee3b108555008ce72ed398a25084f488c1a666e812d9fac76f1"
          "7c96376958fa144ecab72caed68219811580932db78f80e420725cb2f16032bde7c6"
          "f274de3376917bc16dc76b238f060fa226329c214a642417795cc3efa5337b1b89d6"
          "b14ac31e681c2e2a8962c086feaf590eb54769d05d5eaa2b96113ab27fd8ecca8e5a"
          "c717604af7c9e2572f05859d22b5658ba76206ca3f5a8c780bc664f5448927348427"
          "ac08e5713ebe160d2a4968093fad401547669487775baf5c5605cff96e8170e5cde4"
          "eab215ee05d3a8a3416426573f2026157aaea1b8626102e969cb7fdfa67d4585d497"
          "0dd708308a6bd7f1cad1bc916ae3e8be82f2a9444a43cd171ad636f62b5c5b76d970"
          "9c39ae36f03ec6bbceed"),
      /*p=*/
      absl::HexStringToBytes(
          "e9ca59fe1ddb5c5050192692145220e04623867aff99f70a0224c11144c167dc79f2"
          "1df61b64c378c82940b78dd5608ff07a00bb83261e6f328ddea1f53a40a7b9a6bc97"
          "02e05afd1717456416f26b199cdb704d0d5b555deaf4d1d6e738b86db8096fc57c4d"
          "3c8cd3b510a6d5fa90c05135aec2dc161fd9e38771b7f4d26ff0e8a1d0ec0dd4d832"
          "128df1adbdf33125f723717efe947c65539ddeadc95e8960b79f0c77ec8761c38bce"
          "d50a76f145176c0b5dace6b7e3aa0b2ba16646357ec3"),
      /*q=*/
      absl::HexStringToBytes(
          "cf8d8e9c9102b69b76e28aa06431c177320e8ae147db85432507d51a68c587ac5481"
          "97cc73666ae65ba4de3c5a974a4344f1f804969431537ff31e3f23f3cc50f90d69b4"
          "f994b41040aef3072b2cf2679094860924a6404b7196386463a074a6fd1b0b4bfcbc"
          "ab82f81549f44a65ff33a6ce5788fc1a7710759ca59c2040c21f1c97d66ee0f110c4"
          "f37da1c07508b0e60ea1878ea6133ddf8ba4b29fc1761e5b43b7830ab87768058eec"
          "47c22a3ff8bbde4f6b10849b78daa6a072c30f7aa8ef"),
      /*dp=*/
      absl::HexStringToBytes(
          "65203e25094d257527f0791a9ee79788eb4dda91c1961ba19ca3c14f72ea25bedc90"
          "ba1d066463990f1ba8febcbf1b71a7975e51bdbcf3552e0ce7cc2e82f00c9ce55e96"
          "038c804f1179e36e13eef01cb818c34ed1043cbccf30eec38268aa7deb2949cba6a4"
          "d218284b1dd4cca20192ee8dc5f64bb4d63a2d8d1cc77182c520f3bf6adb70702cc4"
          "1bfa821ba11a5c9c0b76ad553d51852d5f29de7455b22ac2472ae8fdc6b618b7b8f5"
          "d2792051e48ce9135185c496ae4793655fff19477279"),
      /*dq=*/
      absl::HexStringToBytes(
          "1d4340102301d6ed245ddc5db0c2b31c331a89ca296f71e27d9e15159c1ffd78f691"
          "2eedcc776c2afe50c8648a013a9f31614c2e996c5b68026a2ca18a581d3e6d5ecec0"
          "8d4fc1f368ab41e888d5d5777492fc32ddcff2d0b03b15c851a395ced570b2af0bfb"
          "2dd35156ef0e5a4ef72439286e7f09cc516d28a7e55195da8b84076c00f7b10f4be5"
          "f8ce85b7b4c87ce872b7a37d213d25441754293b0cf3b263fbb02bf19f0076d211cc"
          "8e7179b37b464199c0e69b4bb04663a7cb8664f04e51"),
      /*crt=*/
      absl::HexStringToBytes(
          "b5b84f7c4868e4de55d37efe7ada9865b0cc73b4b08e111cb8502b39210b17d81a54"
          "2ea793b970d03557c30b5243e066c7ff46e3abfcf3972a9a6199927d05f64fefb7ef"
          "bb336d716599e7cf507e87f274541ef5216235fdedfba524879fecedf4455a60071a"
          "f52d36a0df37b3f4c64b75e564fbdaadc36356e2382efd783ab4e82f4f708fb1addd"
          "288658dfd4afc14c427e2699d8ed178fb343ebef2afd343d0f3aeb30a96dcac9f6a1"
          "36d54347a42e318daf23d1d57b1cd964bd665a3f2a67")};
  return std::make_pair(public_key, private_key);
}

std::pair<anonymous_tokens::TestRsaPublicKey,
          anonymous_tokens::TestRsaPrivateKey>
GetStrongTestRsaKeyPair4096() {
  anonymous_tokens::TestRsaPublicKey public_key = {
      /*n=*/
      absl::HexStringToBytes(
          "cd7d928f252a882c2ba68c1705970f61b7f63c5e907ea5f34e650e3c35edd7467873"
          "4d626fca38a1230c52147cb8b16e2db9adbfe7ce4647ef2eb49b4ade458c80ef0e29"
          "ac4109233d0f512643106fb2e42308fbc2db13c1db24c672a3bfc32acfb429ae5104"
          "507f2b342473a9aa5eab8a9c24d7fe08fb59bea4049d14fea781484591460e5eef62"
          "bd67d3c28aa8e360c50b936998565ca12fbc647d32c446f3f326fe0a36388bfb3ed7"
          "a4c1e8c900a299c88bdaf6dc9ebb032f810f682ddfc2d5fa46e8fa28b8bdfa32131f"
          "259615f85bde8a4eb8258ccbda83e62cf12795c0cae1498c2b435e27c31b9ef8a1ef"
          "bf9552bc6f929a76d9d3a997bfe6fe11c155a571446decdb5032b80482d0bcb8ab0a"
          "23ab82451049a1af692764b69187620005a9d3b5d530d38bfc41938066f505a6e248"
          "4795ce70a69e5df5a551b5179ff1ed3a34eceb09834317de137d9c2d6b35c745c67b"
          "05a1412fc0f616581a051f41bf14c48dcc8b558f92cdee22f5d0f4a75c232e4acf45"
          "d3d2491a2eda3d7ed40fcef81058b8b3b019ef7492453dd3220d5a1ee706abcf4da4"
          "4a572376eee594dca796f8be05ba104ea08881e68c09132622f233574bd0c3f9dfaa"
          "9ae7c6579b90312851aeec02b2678c5e530cc8fbc30e389799df92a2898c34208367"
          "63e199488adc8e5464ff4a67debf35ac2011d4723c3cf1ea1326ce555f80611b2094"
          "4a31"),
      /*e=*/absl::HexStringToBytes("010001")};
  anonymous_tokens::TestRsaPrivateKey private_key = {
      public_key.n, public_key.e,
      /*d=*/
      absl::HexStringToBytes(
          "1d618f83851a64370094c322058c18486e0fb88902db00ea5d72a88ae66117ef3d08"
          "ab6f603187504edd139d5749e720ac4c08b2503817a77064fab0db8f155da60fc834"
          "202b7a5d7dfd032ad7daf145a045fc22573590c91e86cf131423b689980218159302"
          "ed6989695eaee4faf5a74c5dd00ccc0747bd08bb95e749d9b164944b521eb4ae5147"
          "0a72de7dc9eaa4fc30a05b96f50fa015f1e7db6c65465828c842f27ece4ade84f172"
          "cedd64e5dc7fe3421ff1126bf00c2843f20d9c6536c1ba6b9b18f3afbfde75f813f0"
          "d7a47286bcc8007989ede0884339a9bf124a0928f4392b156e18274dc3215f65086e"
          "69b3b58d38dcbad6348605912b80a12233c4c418ab6cedeb313207c2567e0754a9f0"
          "b4ac5365cfbc699ccc3a967a668e9ee9c272c4dfac1a7024bd98ccb7e6de98fe5a3a"
          "43fcb01e0d354ca7b31c266253a35f7ee1109c59f2523bd03fa6d8c6f03c5b347fc5"
          "97c3d0011a0d984105b74a2a406a7ab815657da88c8ee56d78925409df32f8698a75"
          "af8fb2b3576b5676c1ffc8026421b73e72698b3d10695f369874fa681df1b4f1e781"
          "55ff7238b23a1f1b73541fd4a60831a5d78c6a8b2b86d9a5d24f36c9437f5b8e5e52"
          "2d078c9f23c6bbd24e0b261b575b4d31b3d05434afb3b45442f981d33954d0b43380"
          "8aa0cacaba9530f3f6083dd059a0ad36ade853997c575a0036a691851f34c391be7e"
          "6f43"),
      /*p=*/
      absl::HexStringToBytes(
          "e6503c05c40a5db99f52ae1ae7ae3a313802821e2d93a431f71c21206e7cf683603d"
          "e565b0788038841f761025f4f50b090a2a828240460d5eba1fc49cec36d93cb7ee2a"
          "bda6dadeda381b83c3e6f18c1ddea7651a7fe87ee65ce089817baa7998c6db994132"
          "850d6b47f9afbf6c6fbf7d813173d2d2f904892288dc603f4b11c96d67228b0591f4"
          "9311f227f81cad39161039028b009155a703ea581d3f10b4b668e59d07f0ca90bc26"
          "970b854ac17abdd86789ee0d61db5942226f498099076ce05aaa72a52cf6006216a8"
          "f7d1afbd64e9449b068c65faeec6cdb3b02a2d0f9320d85d963067c38093ad6a3483"
          "a3db7e5964ba29634540de9ed60b8e1423ab"),
      /*q=*/
      absl::HexStringToBytes(
          "e4689c2d46a1e63dc955942bc34a948b50cc1047cd61b67aec389f7315aac62d9d24"
          "971525a1d925a93d4da005280298587b3559aba6c2329c63baaa37ab7fabb88c349a"
          "d34f7cfd3a57d5c4dc2c9a623fdb5724af0e808a00ec3a02d503b02905fa8dbb97d4"
          "7d588dd9dab46cc03709f54fff79d0c5941372faa9f9b6ff7524b4cb1740b6af34ce"
          "d5c39b47ce4902387dffffdb7ab6c38a54e55d42b47359cef31e1d993abdaf15fab9"
          "17a15db3a558660ad5fe3bcd298c2625481bc61b3aecfc960c6c7d732c560fcd99cf"
          "1d6d56da6c0ed876b2b957d0c2d7e86a1cd57a08380f526f18e4d3ca9000271cbf8e"
          "87f66e4f908834df312c6a6d62b9137c6d93"),
      /*dp=*/
      absl::HexStringToBytes(
          "8d017a7e1d342b85c5e19ceea401ab670edf9a4257ad18cdee78ae5f68c5e13735e9"
          "2f553ee1c7bed24560c72a35fb00b29c22c29c74356f621b99ef8a13a4d103b7a87d"
          "4a77a970df3192c6ed5dab6d19ac83d8068d610eb08314859b5cd756730eeccbbb7a"
          "eeb2f487b07ac53be27ede9c0666df20838d1f58a16a2b131526e2a7b489158c677b"
          "d1bf1eff118c9d11624cb45ab637b6c335e9d3c3f6c3f1ba72236ed0e157aeed4604"
          "6a5d8751e97af85851abc4af34c652b386d993aac40623c6883beaccede5fefe0ed9"
          "8c4038d43fc0015cd87984c64902365658f8b975dba23455b7ea12dd430f2710eaed"
          "dd9838970a705f7e839bdfb06763d3acc8d9"),
      /*dq=*/
      absl::HexStringToBytes(
          "469418a658ec103449715b4ec692d621d27eac0d33e69cb79124d20882ca796080ed"
          "5c8e1949d0cab5680f0382746190e7ce72a6d9c6b6bd62dbe24354de769dfe71bc93"
          "96f639fe19b828832331d926c0eaab1bd7c8186a0c6cf2640ba48f1bae104519918a"
          "048d878fa8e815aeb3932d2d6219272cd65bc82cb2b74a17d7ffd6a9e6ee8544d081"
          "9546534635f5136d9769b28b04795324fca4bf53ac64f47c615d8df1da57e0b15eff"
          "30d1191e38da7ef59c386a0c34696d241a0b130539091fe7d1c0f866cd6d6e86ae9f"
          "744d64082c59ce03a7a863fd4b27e2565fc08b6bdcbec74f33170a66ce666daf9175"
          "9e87c4806b7ddb3098864c00aeffd7889c67"),
      /*crt=*/
      absl::HexStringToBytes(
          "a4e8c9443c2619b6c92c9dd9941422274431e80503dc8a143ce8d97cde3e331fca29"
          "e1de60ea50f7520d19192e39d0e106b37e20cc3a084afab1ab09c3205e1d7e59050a"
          "b76101ea7bf014dcccc7f948ff5fb14ddd83ee804de5c659672142b4b7e661e0be8e"
          "95eddee3b815f1f26741639fd04e5015153375ee1dfaa87ebf5b4340948538d3bfa1"
          "b4cdc7e81b68c7c0c85879bd5026ea66735e4c3b56294f6c63ac1ba0709edeefc252"
          "c90723039f1fe227086a2b57299d7f7bcd1f09b82985c7710bb43d342167142629a2"
          "3094981f3908d0a1be38a5e3f823fad1ef96aa643fb5811cbafe8b134725075d4b66"
          "4409de70b2571ea6ef53a44615db16b7bda5")};
  return std::make_pair(public_key, private_key);
}

}  // namespace anonymous_tokens
}  // namespace private_membership
