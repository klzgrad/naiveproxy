// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/certificate_policies.h"

#include "net/cert/internal/test_helpers.h"
#include "net/der/input.h"
#include "net/der/parser.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

::testing::AssertionResult LoadTestData(const std::string& name,
                                        std::string* result) {
  std::string path = "net/data/certificate_policies_unittest/" + name;

  const PemBlockMapping mappings[] = {
      {"CERTIFICATE POLICIES", result},
  };

  return ReadTestDataFromPemFile(path, mappings);
}

const uint8_t policy_1_2_3_der[] = {0x2A, 0x03};
const uint8_t policy_1_2_4_der[] = {0x2A, 0x04};

class ParseCertificatePoliciesExtensionTest
    : public testing::TestWithParam<bool> {
 protected:
  bool fail_parsing_unknown_qualifier_oids() const { return GetParam(); }
};

// Run the tests with all possible values for
// |fail_parsing_unknown_qualifier_oids|.
INSTANTIATE_TEST_SUITE_P(All,
                         ParseCertificatePoliciesExtensionTest,
                         testing::Bool());

TEST_P(ParseCertificatePoliciesExtensionTest, InvalidEmpty) {
  std::string der;
  ASSERT_TRUE(LoadTestData("invalid-empty.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest, InvalidIdentifierNotOid) {
  std::string der;
  ASSERT_TRUE(LoadTestData("invalid-policy_identifier_not_oid.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest, AnyPolicy) {
  std::string der;
  ASSERT_TRUE(LoadTestData("anypolicy.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(1U, policies.size());
  EXPECT_EQ(AnyPolicy(), policies[0]);
}

TEST_P(ParseCertificatePoliciesExtensionTest, AnyPolicyWithQualifier) {
  std::string der;
  ASSERT_TRUE(LoadTestData("anypolicy_with_qualifier.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(1U, policies.size());
  EXPECT_EQ(AnyPolicy(), policies[0]);
}

TEST_P(ParseCertificatePoliciesExtensionTest,
       InvalidAnyPolicyWithCustomQualifier) {
  std::string der;
  ASSERT_TRUE(
      LoadTestData("invalid-anypolicy_with_custom_qualifier.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest, OnePolicy) {
  std::string der;
  ASSERT_TRUE(LoadTestData("policy_1_2_3.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(1U, policies.size());
  EXPECT_EQ(der::Input(policy_1_2_3_der), policies[0]);
}

TEST_P(ParseCertificatePoliciesExtensionTest, OnePolicyWithQualifier) {
  std::string der;
  ASSERT_TRUE(LoadTestData("policy_1_2_3_with_qualifier.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(1U, policies.size());
  EXPECT_EQ(der::Input(policy_1_2_3_der), policies[0]);
}

TEST_P(ParseCertificatePoliciesExtensionTest, OnePolicyWithCustomQualifier) {
  std::string der;
  ASSERT_TRUE(LoadTestData("policy_1_2_3_with_custom_qualifier.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  bool result = ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors);

  if (fail_parsing_unknown_qualifier_oids()) {
    EXPECT_FALSE(result);
  } else {
    EXPECT_TRUE(result);
    ASSERT_EQ(1U, policies.size());
    EXPECT_EQ(der::Input(policy_1_2_3_der), policies[0]);
  }
}

TEST_P(ParseCertificatePoliciesExtensionTest,
       InvalidPolicyWithDuplicatePolicyOid) {
  std::string der;
  ASSERT_TRUE(LoadTestData("invalid-policy_1_2_3_dupe.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest,
       InvalidPolicyWithEmptyQualifiersSequence) {
  std::string der;
  ASSERT_TRUE(LoadTestData(
      "invalid-policy_1_2_3_with_empty_qualifiers_sequence.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest,
       InvalidPolicyInformationHasUnconsumedData) {
  std::string der;
  ASSERT_TRUE(LoadTestData(
      "invalid-policy_1_2_3_policyinformation_unconsumed_data.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest,
       InvalidPolicyQualifierInfoHasUnconsumedData) {
  std::string der;
  ASSERT_TRUE(LoadTestData(
      "invalid-policy_1_2_3_policyqualifierinfo_unconsumed_data.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_FALSE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
}

TEST_P(ParseCertificatePoliciesExtensionTest, TwoPolicies) {
  std::string der;
  ASSERT_TRUE(LoadTestData("policy_1_2_3_and_1_2_4.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(2U, policies.size());
  EXPECT_EQ(der::Input(policy_1_2_3_der), policies[0]);
  EXPECT_EQ(der::Input(policy_1_2_4_der), policies[1]);
}

TEST_P(ParseCertificatePoliciesExtensionTest, TwoPoliciesWithQualifiers) {
  std::string der;
  ASSERT_TRUE(LoadTestData("policy_1_2_3_and_1_2_4_with_qualifiers.pem", &der));
  std::vector<der::Input> policies;
  CertErrors errors;
  EXPECT_TRUE(ParseCertificatePoliciesExtension(
      der::Input(&der), fail_parsing_unknown_qualifier_oids(), &policies,
      &errors));
  ASSERT_EQ(2U, policies.size());
  EXPECT_EQ(der::Input(policy_1_2_3_der), policies[0]);
  EXPECT_EQ(der::Input(policy_1_2_4_der), policies[1]);
}

// NOTE: The tests for ParseCertificatePolicies() are part of
// parsed_certificate_unittest.cc

// NOTE: The tests for ParseInhibitAnyPolicy() are part of
// parsed_certificate_unittest.cc

}  // namespace
}  // namespace net
