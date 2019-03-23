// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/x509_cert_types.h"

#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/test/test_certificate_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

TEST(X509TypesTest, ParseDNVerisign) {
  CertPrincipal verisign;
  EXPECT_TRUE(verisign.ParseDistinguishedName(VerisignDN, sizeof(VerisignDN)));
  EXPECT_EQ("", verisign.common_name);
  EXPECT_EQ("US", verisign.country_name);
  ASSERT_EQ(1U, verisign.organization_names.size());
  EXPECT_EQ("VeriSign, Inc.", verisign.organization_names[0]);
  ASSERT_EQ(1U, verisign.organization_unit_names.size());
  EXPECT_EQ("Class 1 Public Primary Certification Authority",
            verisign.organization_unit_names[0]);
}

TEST(X509TypesTest, ParseDNStartcom) {
  CertPrincipal startcom;
  EXPECT_TRUE(startcom.ParseDistinguishedName(StartComDN, sizeof(StartComDN)));
  EXPECT_EQ("StartCom Certification Authority", startcom.common_name);
  EXPECT_EQ("IL", startcom.country_name);
  ASSERT_EQ(1U, startcom.organization_names.size());
  EXPECT_EQ("StartCom Ltd.", startcom.organization_names[0]);
  ASSERT_EQ(1U, startcom.organization_unit_names.size());
  EXPECT_EQ("Secure Digital Certificate Signing",
            startcom.organization_unit_names[0]);
}

TEST(X509TypesTest, ParseDNUserTrust) {
  CertPrincipal usertrust;
  EXPECT_TRUE(usertrust.ParseDistinguishedName(UserTrustDN,
                                               sizeof(UserTrustDN)));
  EXPECT_EQ("UTN-USERFirst-Client Authentication and Email",
            usertrust.common_name);
  EXPECT_EQ("US", usertrust.country_name);
  EXPECT_EQ("UT", usertrust.state_or_province_name);
  EXPECT_EQ("Salt Lake City", usertrust.locality_name);
  ASSERT_EQ(1U, usertrust.organization_names.size());
  EXPECT_EQ("The USERTRUST Network", usertrust.organization_names[0]);
  ASSERT_EQ(1U, usertrust.organization_unit_names.size());
  EXPECT_EQ("http://www.usertrust.com",
            usertrust.organization_unit_names[0]);
}

TEST(X509TypesTest, ParseDNTurkTrust) {
  // Note: This tests parsing UTF8STRINGs.
  CertPrincipal turktrust;
  EXPECT_TRUE(turktrust.ParseDistinguishedName(TurkTrustDN,
                                               sizeof(TurkTrustDN)));
  EXPECT_EQ("TÜRKTRUST Elektronik Sertifika Hizmet Sağlayıcısı",
            turktrust.common_name);
  EXPECT_EQ("TR", turktrust.country_name);
  EXPECT_EQ("Ankara", turktrust.locality_name);
  ASSERT_EQ(1U, turktrust.organization_names.size());
  EXPECT_EQ("TÜRKTRUST Bilgi İletişim ve Bilişim Güvenliği Hizmetleri A.Ş. (c) Kasım 2005",
            turktrust.organization_names[0]);
}

TEST(X509TypesTest, ParseDNATrust) {
  // Note: This tests parsing 16-bit BMPSTRINGs.
  CertPrincipal atrust;
  EXPECT_TRUE(atrust.ParseDistinguishedName(ATrustQual01DN,
                                            sizeof(ATrustQual01DN)));
  EXPECT_EQ("A-Trust-Qual-01",
            atrust.common_name);
  EXPECT_EQ("AT", atrust.country_name);
  ASSERT_EQ(1U, atrust.organization_names.size());
  EXPECT_EQ("A-Trust Ges. für Sicherheitssysteme im elektr. Datenverkehr GmbH",
            atrust.organization_names[0]);
  ASSERT_EQ(1U, atrust.organization_unit_names.size());
  EXPECT_EQ("A-Trust-Qual-01",
            atrust.organization_unit_names[0]);
}

TEST(X509TypesTest, ParseDNEntrust) {
  // Note: This tests parsing T61STRINGs and fields with multiple values.
  CertPrincipal entrust;
  EXPECT_TRUE(entrust.ParseDistinguishedName(EntrustDN,
                                             sizeof(EntrustDN)));
  EXPECT_EQ("Entrust.net Certification Authority (2048)",
            entrust.common_name);
  EXPECT_EQ("", entrust.country_name);
  ASSERT_EQ(1U, entrust.organization_names.size());
  EXPECT_EQ("Entrust.net",
            entrust.organization_names[0]);
  ASSERT_EQ(2U, entrust.organization_unit_names.size());
  EXPECT_EQ("www.entrust.net/CPS_2048 incorp. by ref. (limits liab.)",
            entrust.organization_unit_names[0]);
  EXPECT_EQ("(c) 1999 Entrust.net Limited",
            entrust.organization_unit_names[1]);
}

const struct CertDateTestData {
  CertDateFormat format;
  const char* date_string;
  bool is_valid;
  base::Time::Exploded expected_result;
} kCertDateTimeData[] = {
    {CERT_DATE_FORMAT_UTC_TIME,
     "120101000000Z",
     true,
     {2012, 1, 0, 1, 0, 0, 0}},
    {CERT_DATE_FORMAT_UTC_TIME, "-90101000000Z", false, {0}},
    {CERT_DATE_FORMAT_UTC_TIME, "+90101000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "2012+1+1000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "2012-101000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "2012 101000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME,
     "20120101000000Z",
     true,
     {2012, 1, 0, 1, 0, 0, 0}},
    {CERT_DATE_FORMAT_UTC_TIME,
     "490101000000Z",
     true,
     {2049, 1, 0, 1, 0, 0, 0}},
    {CERT_DATE_FORMAT_UTC_TIME,
     "500101000000Z",
     true,
     {1950, 1, 0, 1, 0, 0, 0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME,
     "19500101000000Z",
     true,
     {1950, 1, 0, 1, 0, 0, 0}},
    {CERT_DATE_FORMAT_UTC_TIME, "AB0101000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "19AB0101000000Z", false, {0}},
    {CERT_DATE_FORMAT_UTC_TIME, "", false, {0}},
    {CERT_DATE_FORMAT_UTC_TIME, "A", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "20121301000000Z", false, {0}},
    {CERT_DATE_FORMAT_GENERALIZED_TIME,
     "20120101123000Z",
     true,
     {2012, 1, 0, 1, 12, 30, 0}},
    // test 31st of April
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "20160431121000Z", false, {0}},
    // test 31st of February
    {CERT_DATE_FORMAT_GENERALIZED_TIME, "20160231121000Z", false, {0}},
};

// GTest pretty printer.
void PrintTo(const CertDateTestData& data, std::ostream* os) {
  base::Time out_time;
  bool result = base::Time::FromUTCExploded(data.expected_result, &out_time);
  *os << " format: " << data.format
      << "; date string: " << base::StringPiece(data.date_string)
      << "; valid: " << data.is_valid << "; expected date: "
      << (data.is_valid ? out_time.ToInternalValue() : 0U)
      << "; FromUTCExploded conversion result: " << result;
}

class X509CertTypesDateTest : public testing::TestWithParam<CertDateTestData> {
  public:
   virtual ~X509CertTypesDateTest() = default;
   void SetUp() override { test_data_ = GetParam(); }

  protected:
   CertDateTestData test_data_;
};

TEST_P(X509CertTypesDateTest, Parse) {
  base::Time parsed_date;
  bool parsed = ParseCertificateDate(
      test_data_.date_string, test_data_.format, &parsed_date);
  if (!parsed && test_data_.is_valid &&
      test_data_.expected_result.year >= 2038 && sizeof(time_t) == 4) {
    // Some of the valid test data will fail on 32-bit POSIX systems
    return;
  }

  if (!test_data_.is_valid)
    return;
  // Convert the expected value to a base::Time(). This ensures that
  // systems that only support 32-bit times will pass the tests, by ensuring at
  // least that the times have the same truncating behavior.
  // Note: Compared as internal values so that mismatches can be cleanly
  // printed by GTest (e.g.: without PrintTo overrides).
  base::Time out_time;
  EXPECT_TRUE(
      base::Time::FromUTCExploded(test_data_.expected_result, &out_time));
  EXPECT_EQ(out_time.ToInternalValue(), parsed_date.ToInternalValue());
}
INSTANTIATE_TEST_CASE_P(,
                        X509CertTypesDateTest,
                        testing::ValuesIn(kCertDateTimeData));

}  // namespace

}  // namespace net
