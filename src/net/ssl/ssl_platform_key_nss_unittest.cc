// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_platform_key_nss.h"

#include <keyhi.h>
#include <pk11pub.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "crypto/ec_private_key.h"
#include "crypto/nss_crypto_module_delegate.h"
#include "crypto/scoped_nss_types.h"
#include "crypto/scoped_test_nss_db.h"
#include "net/cert/x509_util_nss.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/ssl_private_key_test_util.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/ec_key.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

namespace {

struct TestKey {
  const char* name;
  const char* cert_file;
  const char* key_file;
  int type;
};

const TestKey kTestKeys[] = {
    {"RSA", "client_1.pem", "client_1.pk8", EVP_PKEY_RSA},
    {"ECDSA_P256", "client_4.pem", "client_4.pk8", EVP_PKEY_EC},
    {"ECDSA_P384", "client_5.pem", "client_5.pk8", EVP_PKEY_EC},
    {"ECDSA_P521", "client_6.pem", "client_6.pk8", EVP_PKEY_EC},
};

std::string TestKeyToString(const testing::TestParamInfo<TestKey>& params) {
  return params.param.name;
}

}  // namespace

class SSLPlatformKeyNSSTest : public testing::TestWithParam<TestKey>,
                              public WithScopedTaskEnvironment {};

TEST_P(SSLPlatformKeyNSSTest, KeyMatches) {
  const TestKey& test_key = GetParam();

  std::string pkcs8;
  base::FilePath pkcs8_path =
      GetTestCertsDirectory().AppendASCII(test_key.key_file);
  ASSERT_TRUE(base::ReadFileToString(pkcs8_path, &pkcs8));

  // Import the key into a test NSS database.
  crypto::ScopedTestNSSDB test_db;
  scoped_refptr<X509Certificate> cert;
  ScopedCERTCertificate nss_cert;
  if (test_key.type == EVP_PKEY_EC) {
    // NSS cannot import unencrypted ECDSA keys, so we encrypt it with an empty
    // password and import manually.
    std::vector<uint8_t> pkcs8_vector(pkcs8.begin(), pkcs8.end());
    std::unique_ptr<crypto::ECPrivateKey> ec_private_key =
        crypto::ECPrivateKey::CreateFromPrivateKeyInfo(pkcs8_vector);
    ASSERT_TRUE(ec_private_key);
    std::vector<uint8_t> encrypted;
    ASSERT_TRUE(ec_private_key->ExportEncryptedPrivateKey(&encrypted));

    SECItem encrypted_item = {siBuffer, encrypted.data(),
                              static_cast<unsigned>(encrypted.size())};
    SECKEYEncryptedPrivateKeyInfo epki;
    memset(&epki, 0, sizeof(epki));
    crypto::ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    ASSERT_EQ(SECSuccess,
              SEC_QuickDERDecodeItem(
                  arena.get(), &epki,
                  SEC_ASN1_GET(SECKEY_EncryptedPrivateKeyInfoTemplate),
                  &encrypted_item));

    // NSS uses the serialized public key in X9.62 form as the "public value"
    // for key ID purposes.
    bssl::ScopedCBB cbb;
    ASSERT_TRUE(CBB_init(cbb.get(), 0));
    EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(ec_private_key->key());
    ASSERT_TRUE(EC_POINT_point2cbb(cbb.get(), EC_KEY_get0_group(ec_key),
                                   EC_KEY_get0_public_key(ec_key),
                                   POINT_CONVERSION_UNCOMPRESSED, nullptr));
    uint8_t* public_value;
    size_t public_value_len;
    ASSERT_TRUE(CBB_finish(cbb.get(), &public_value, &public_value_len));
    bssl::UniquePtr<uint8_t> scoped_public_value(public_value);
    SECItem public_item = {siBuffer, public_value,
                           static_cast<unsigned>(public_value_len)};

    SECItem password_item = {siBuffer, nullptr, 0};
    ASSERT_EQ(SECSuccess,
              PK11_ImportEncryptedPrivateKeyInfo(
                  test_db.slot(), &epki, &password_item, nullptr /* nickname */,
                  &public_item, PR_TRUE /* permanent */, PR_TRUE /* private */,
                  ecKey, KU_DIGITAL_SIGNATURE, nullptr /* wincx */));

    cert = ImportCertFromFile(GetTestCertsDirectory(), test_key.cert_file);
    ASSERT_TRUE(cert);
    nss_cert = ImportClientCertToSlot(cert, test_db.slot());
    ASSERT_TRUE(nss_cert);
  } else {
    cert = ImportClientCertAndKeyFromFile(GetTestCertsDirectory(),
                                          test_key.cert_file, test_key.key_file,
                                          test_db.slot(), &nss_cert);
    ASSERT_TRUE(cert);
    ASSERT_TRUE(nss_cert);
  }

  // Look up the key.
  scoped_refptr<SSLPrivateKey> key =
      FetchClientCertPrivateKey(cert.get(), nss_cert.get(), nullptr);
  ASSERT_TRUE(key);

  // All NSS keys are expected to have the default preferences.
  EXPECT_EQ(SSLPrivateKey::DefaultAlgorithmPreferences(test_key.type,
                                                       true /* supports PSS */),
            key->GetAlgorithmPreferences());

  TestSSLPrivateKeyMatches(key.get(), pkcs8);
}

INSTANTIATE_TEST_SUITE_P(,
                         SSLPlatformKeyNSSTest,
                         testing::ValuesIn(kTestKeys),
                         TestKeyToString);

}  // namespace net
