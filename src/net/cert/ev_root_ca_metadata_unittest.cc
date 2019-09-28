// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/ev_root_ca_metadata.h"

#include "build/build_config.h"
#include "net/cert/x509_cert_types.h"
#include "net/der/input.h"
#include "net/test/cert_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_NSS_CERTS)
#include "crypto/nss_util.h"
#include "crypto/scoped_nss_types.h"
#endif

namespace net {

namespace {

#if defined(USE_NSS_CERTS) || defined(OS_WIN)
const char kVerisignPolicyStr[] = "2.16.840.1.113733.1.7.23.6";
const char kThawtePolicyStr[] = "2.16.840.1.113733.1.7.48.1";
const char kFakePolicyStr[] = "2.16.840.1.42";
const char kCabEvPolicyStr[] = "2.23.140.1.1";
#elif defined(OS_MACOSX)
const char kFakePolicyStr[] = "2.16.840.1.42";
#endif

#if defined(USE_NSS_CERTS) || defined(OS_WIN) || defined(OS_MACOSX)
// DER OID values (no tag or length).
const uint8_t kVerisignPolicyBytes[] = {0x60, 0x86, 0x48, 0x01, 0x86, 0xf8,
                                        0x45, 0x01, 0x07, 0x17, 0x06};
const uint8_t kThawtePolicyBytes[] = {0x60, 0x86, 0x48, 0x01, 0x86, 0xf8,
                                      0x45, 0x01, 0x07, 0x30, 0x01};
const uint8_t kFakePolicyBytes[] = {0x60, 0x86, 0x48, 0x01, 0x2a};
const uint8_t kCabEvPolicyBytes[] = {0x67, 0x81, 0x0c, 0x01, 0x01};

const SHA256HashValue kVerisignFingerprint = {
    {0xe7, 0x68, 0x56, 0x34, 0xef, 0xac, 0xf6, 0x9a, 0xce, 0x93, 0x9a,
     0x6b, 0x25, 0x5b, 0x7b, 0x4f, 0xab, 0xef, 0x42, 0x93, 0x5b, 0x50,
     0xa2, 0x65, 0xac, 0xb5, 0xcb, 0x60, 0x27, 0xe4, 0x4e, 0x70}};
const SHA256HashValue kFakeFingerprint = {
    {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
     0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
     0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};

class EVOidData {
 public:
  EVOidData();
  bool Init();

  EVRootCAMetadata::PolicyOID verisign_policy;
  der::Input verisign_policy_bytes;

  EVRootCAMetadata::PolicyOID thawte_policy;
  der::Input thawte_policy_bytes;

  EVRootCAMetadata::PolicyOID fake_policy;
  der::Input fake_policy_bytes;

  EVRootCAMetadata::PolicyOID cab_ev_policy;
  der::Input cab_ev_policy_bytes;
};

#endif  // defined(USE_NSS_CERTS) || defined(OS_WIN) || defined(OS_MACOSX)

#if defined(USE_NSS_CERTS)

SECOidTag RegisterOID(PLArenaPool* arena, const char* oid_string) {
  SECOidData oid_data;
  memset(&oid_data, 0, sizeof(oid_data));
  oid_data.offset = SEC_OID_UNKNOWN;
  oid_data.desc = oid_string;
  oid_data.mechanism = CKM_INVALID_MECHANISM;
  oid_data.supportedExtension = INVALID_CERT_EXTENSION;

  SECStatus rv = SEC_StringToOID(arena, &oid_data.oid, oid_string, 0);
  if (rv != SECSuccess)
    return SEC_OID_UNKNOWN;

  return SECOID_AddEntry(&oid_data);
}

EVOidData::EVOidData()
    : verisign_policy(SEC_OID_UNKNOWN),
      verisign_policy_bytes(kVerisignPolicyBytes),
      thawte_policy(SEC_OID_UNKNOWN),
      thawte_policy_bytes(kThawtePolicyBytes),
      fake_policy(SEC_OID_UNKNOWN),
      fake_policy_bytes(kFakePolicyBytes),
      cab_ev_policy(SEC_OID_UNKNOWN),
      cab_ev_policy_bytes(kCabEvPolicyBytes) {}

bool EVOidData::Init() {
  crypto::EnsureNSSInit();
  crypto::ScopedPLArenaPool pool(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!pool.get())
    return false;

  verisign_policy = RegisterOID(pool.get(), kVerisignPolicyStr);
  thawte_policy = RegisterOID(pool.get(), kThawtePolicyStr);
  fake_policy = RegisterOID(pool.get(), kFakePolicyStr);
  cab_ev_policy = RegisterOID(pool.get(), kCabEvPolicyStr);

  return verisign_policy != SEC_OID_UNKNOWN &&
         thawte_policy != SEC_OID_UNKNOWN && fake_policy != SEC_OID_UNKNOWN &&
         cab_ev_policy != SEC_OID_UNKNOWN;
}

#elif defined(OS_WIN)

EVOidData::EVOidData()
    : verisign_policy(kVerisignPolicyStr),
      verisign_policy_bytes(kVerisignPolicyBytes),
      thawte_policy(kThawtePolicyStr),
      thawte_policy_bytes(kThawtePolicyBytes),
      fake_policy(kFakePolicyStr),
      fake_policy_bytes(kFakePolicyBytes),
      cab_ev_policy(kCabEvPolicyStr),
      cab_ev_policy_bytes(kCabEvPolicyBytes) {}

bool EVOidData::Init() {
  return true;
}

#elif defined(OS_MACOSX)

EVOidData::EVOidData()
    : verisign_policy(kVerisignPolicyBytes),
      verisign_policy_bytes(kVerisignPolicyBytes),
      thawte_policy(kThawtePolicyBytes),
      thawte_policy_bytes(kThawtePolicyBytes),
      fake_policy(kFakePolicyBytes),
      fake_policy_bytes(kFakePolicyBytes),
      cab_ev_policy(kCabEvPolicyBytes),
      cab_ev_policy_bytes(kCabEvPolicyBytes) {}

bool EVOidData::Init() {
  return true;
}

#endif

#if defined(USE_NSS_CERTS) || defined(OS_WIN) || defined(OS_MACOSX)

class EVRootCAMetadataTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(ev_oid_data.Init()); }

  EVOidData ev_oid_data;
};

TEST_F(EVRootCAMetadataTest, Basic) {
  EVRootCAMetadata* ev_metadata(EVRootCAMetadata::GetInstance());

  EXPECT_TRUE(ev_metadata->IsEVPolicyOID(ev_oid_data.verisign_policy));
  EXPECT_TRUE(
      ev_metadata->IsEVPolicyOIDGivenBytes(ev_oid_data.verisign_policy_bytes));

  EXPECT_FALSE(ev_metadata->IsEVPolicyOID(ev_oid_data.fake_policy));
  EXPECT_FALSE(
      ev_metadata->IsEVPolicyOIDGivenBytes(ev_oid_data.fake_policy_bytes));

  EXPECT_TRUE(ev_metadata->HasEVPolicyOID(kVerisignFingerprint,
                                          ev_oid_data.verisign_policy));
  EXPECT_TRUE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kVerisignFingerprint, ev_oid_data.verisign_policy_bytes));

  EXPECT_FALSE(ev_metadata->HasEVPolicyOID(kFakeFingerprint,
                                           ev_oid_data.verisign_policy));
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kFakeFingerprint, ev_oid_data.verisign_policy_bytes));

  EXPECT_FALSE(ev_metadata->HasEVPolicyOID(kVerisignFingerprint,
                                           ev_oid_data.fake_policy));
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kVerisignFingerprint, ev_oid_data.fake_policy_bytes));

  EXPECT_FALSE(ev_metadata->HasEVPolicyOID(kVerisignFingerprint,
                                           ev_oid_data.thawte_policy));
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kVerisignFingerprint, ev_oid_data.thawte_policy_bytes));

  // Test a completely bogus OID given bytes.
  const uint8_t bad_oid[] = {0};
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(kVerisignFingerprint,
                                                     der::Input(bad_oid)));
}

TEST_F(EVRootCAMetadataTest, AddRemove) {
  EVRootCAMetadata* ev_metadata(EVRootCAMetadata::GetInstance());

  EXPECT_FALSE(ev_metadata->IsEVPolicyOID(ev_oid_data.fake_policy));
  EXPECT_FALSE(
      ev_metadata->IsEVPolicyOIDGivenBytes(ev_oid_data.fake_policy_bytes));

  EXPECT_FALSE(
      ev_metadata->HasEVPolicyOID(kFakeFingerprint, ev_oid_data.fake_policy));
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kFakeFingerprint, ev_oid_data.fake_policy_bytes));

  {
    ScopedTestEVPolicy test_ev_policy(ev_metadata, kFakeFingerprint,
                                      kFakePolicyStr);

    EXPECT_TRUE(ev_metadata->IsEVPolicyOID(ev_oid_data.fake_policy));
    EXPECT_TRUE(
        ev_metadata->IsEVPolicyOIDGivenBytes(ev_oid_data.fake_policy_bytes));

    EXPECT_TRUE(
        ev_metadata->HasEVPolicyOID(kFakeFingerprint, ev_oid_data.fake_policy));
    EXPECT_TRUE(ev_metadata->HasEVPolicyOIDGivenBytes(
        kFakeFingerprint, ev_oid_data.fake_policy_bytes));
  }

  EXPECT_FALSE(ev_metadata->IsEVPolicyOID(ev_oid_data.fake_policy));
  EXPECT_FALSE(
      ev_metadata->IsEVPolicyOIDGivenBytes(ev_oid_data.fake_policy_bytes));

  EXPECT_FALSE(
      ev_metadata->HasEVPolicyOID(kFakeFingerprint, ev_oid_data.fake_policy));
  EXPECT_FALSE(ev_metadata->HasEVPolicyOIDGivenBytes(
      kFakeFingerprint, ev_oid_data.fake_policy_bytes));
}

TEST_F(EVRootCAMetadataTest, IsCaBrowserForumEvOid) {
  EXPECT_TRUE(
      EVRootCAMetadata::IsCaBrowserForumEvOid(ev_oid_data.cab_ev_policy));

  EXPECT_FALSE(
      EVRootCAMetadata::IsCaBrowserForumEvOid(ev_oid_data.fake_policy));
  EXPECT_FALSE(
      EVRootCAMetadata::IsCaBrowserForumEvOid(ev_oid_data.verisign_policy));
}

#endif  // defined(USE_NSS_CERTS) || defined(OS_WIN) || defined(OS_MACOSX)

}  // namespace

}  // namespace net
