// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_versions.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mock_log.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class QuicVersionsTest : public QuicTest {
 protected:
  QuicVersionLabel MakeVersionLabel(char a, char b, char c, char d) {
    return MakeQuicTag(d, c, b, a);
  }
};

TEST_F(QuicVersionsTest, QuicVersionToQuicVersionLabel) {
  // If you add a new version to the QuicTransportVersion enum you will need to
  // add a new case to QuicVersionToQuicVersionLabel, otherwise this test will
  // fail.

  // Any logs would indicate an unsupported version which we don't expect.
  CREATE_QUIC_MOCK_LOG(log);
  EXPECT_QUIC_LOG_CALL(log).Times(0);
  log.StartCapturingLogs();

  // Explicitly test a specific version.
  EXPECT_EQ(MakeQuicTag('3', '4', '0', 'Q'),
            QuicVersionToQuicVersionLabel(QUIC_VERSION_43));

  // Loop over all supported versions and make sure that we never hit the
  // default case (i.e. all supported versions should be successfully converted
  // to valid QuicVersionLabels).
  for (QuicTransportVersion transport_version : SupportedTransportVersions()) {
    if (!ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO, transport_version)) {
      continue;
    }
    EXPECT_LT(0u, QuicVersionToQuicVersionLabel(transport_version));
  }
}

TEST_F(QuicVersionsTest, QuicVersionToQuicVersionLabelUnsupported) {
  EXPECT_QUIC_BUG(CreateQuicVersionLabel(UnsupportedQuicVersion()),
                  "Invalid HandshakeProtocol: 0");
}

TEST_F(QuicVersionsTest, KnownAndValid) {
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    EXPECT_TRUE(version.IsKnown());
    EXPECT_TRUE(ParsedQuicVersionIsValid(version.handshake_protocol,
                                         version.transport_version));
  }
  ParsedQuicVersion unsupported = UnsupportedQuicVersion();
  EXPECT_FALSE(unsupported.IsKnown());
  EXPECT_TRUE(ParsedQuicVersionIsValid(unsupported.handshake_protocol,
                                       unsupported.transport_version));
  ParsedQuicVersion reserved = QuicVersionReservedForNegotiation();
  EXPECT_TRUE(reserved.IsKnown());
  EXPECT_TRUE(ParsedQuicVersionIsValid(reserved.handshake_protocol,
                                       reserved.transport_version));
  // Check that invalid combinations are not valid.
  EXPECT_FALSE(ParsedQuicVersionIsValid(PROTOCOL_TLS1_3, QUIC_VERSION_43));
  EXPECT_FALSE(ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO,
                                        QUIC_VERSION_IETF_DRAFT_27));
  // Check that deprecated versions are not valid.
  EXPECT_FALSE(ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO,
                                        static_cast<QuicTransportVersion>(33)));
  EXPECT_FALSE(ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO,
                                        static_cast<QuicTransportVersion>(99)));
  EXPECT_FALSE(ParsedQuicVersionIsValid(PROTOCOL_TLS1_3,
                                        static_cast<QuicTransportVersion>(99)));
}

TEST_F(QuicVersionsTest, QuicVersionLabelToQuicTransportVersion) {
  // If you add a new version to the QuicTransportVersion enum you will need to
  // add a new case to QuicVersionLabelToQuicTransportVersion, otherwise this
  // test will fail.

  // Any logs would indicate an unsupported version which we don't expect.
  CREATE_QUIC_MOCK_LOG(log);
  EXPECT_QUIC_LOG_CALL(log).Times(0);
  log.StartCapturingLogs();

  // Explicitly test specific versions.
  EXPECT_EQ(QUIC_VERSION_43,
            QuicVersionLabelToQuicVersion(MakeQuicTag('3', '4', '0', 'Q')));

  for (QuicTransportVersion transport_version : SupportedTransportVersions()) {
    if (!ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO, transport_version)) {
      continue;
    }

    // Get the label from the version (we can loop over QuicVersions easily).
    QuicVersionLabel version_label =
        QuicVersionToQuicVersionLabel(transport_version);
    EXPECT_LT(0u, version_label);

    // Now try converting back.
    QuicTransportVersion label_to_transport_version =
        QuicVersionLabelToQuicVersion(version_label);
    EXPECT_EQ(transport_version, label_to_transport_version);
    EXPECT_NE(QUIC_VERSION_UNSUPPORTED, label_to_transport_version);
  }
}

TEST_F(QuicVersionsTest, QuicVersionLabelToQuicVersionUnsupported) {
  CREATE_QUIC_MOCK_LOG(log);
  if (QUIC_DLOG_INFO_IS_ON()) {
    EXPECT_QUIC_LOG_CALL_CONTAINS(log, INFO,
                                  "Unsupported QuicVersionLabel version: EKAF")
        .Times(1);
  }
  log.StartCapturingLogs();

  EXPECT_EQ(QUIC_VERSION_UNSUPPORTED,
            QuicVersionLabelToQuicVersion(MakeQuicTag('F', 'A', 'K', 'E')));
}

TEST_F(QuicVersionsTest, QuicVersionLabelToHandshakeProtocol) {
  CREATE_QUIC_MOCK_LOG(log);
  EXPECT_QUIC_LOG_CALL(log).Times(0);
  log.StartCapturingLogs();

  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.handshake_protocol != PROTOCOL_QUIC_CRYPTO) {
      continue;
    }
    QuicVersionLabel version_label =
        QuicVersionToQuicVersionLabel(version.transport_version);
    EXPECT_EQ(PROTOCOL_QUIC_CRYPTO,
              QuicVersionLabelToHandshakeProtocol(version_label));
  }

  // Test a TLS version:
  QuicTag tls_tag = MakeQuicTag('0', '5', '0', 'T');
  EXPECT_EQ(PROTOCOL_TLS1_3, QuicVersionLabelToHandshakeProtocol(tls_tag));
}

TEST_F(QuicVersionsTest, ParseQuicVersionLabel) {
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '3')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '6')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '8')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '5', '0')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '5', '0')));
}

TEST_F(QuicVersionsTest, ParseQuicVersionString) {
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43),
            ParseQuicVersionString("Q043"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionString("QUIC_VERSION_46"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionString("46"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionString("Q046"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48),
            ParseQuicVersionString("Q048"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50),
            ParseQuicVersionString("Q050"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50),
            ParseQuicVersionString("50"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50),
            ParseQuicVersionString("h3-Q050"));

  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString(""));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("Q 46"));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("Q046 "));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("99"));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("70"));

  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50),
            ParseQuicVersionString("T050"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50),
            ParseQuicVersionString("h3-T050"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27),
            ParseQuicVersionString("ff00001b"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27),
            ParseQuicVersionString("h3-27"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25),
            ParseQuicVersionString("ff000019"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25),
            ParseQuicVersionString("h3-25"));
}

TEST_F(QuicVersionsTest, ParseQuicVersionVectorString) {
  ParsedQuicVersion version_q046(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46);
  ParsedQuicVersion version_q050(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50);
  ParsedQuicVersion version_t050(PROTOCOL_TLS1_3, QUIC_VERSION_50);
  ParsedQuicVersion version_draft_25(PROTOCOL_TLS1_3,
                                     QUIC_VERSION_IETF_DRAFT_25);
  ParsedQuicVersion version_draft_27(PROTOCOL_TLS1_3,
                                     QUIC_VERSION_IETF_DRAFT_27);

  EXPECT_THAT(ParseQuicVersionVectorString(""), IsEmpty());

  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-Q050"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-T050"),
              ElementsAre(version_t050));

  EXPECT_THAT(ParseQuicVersionVectorString("h3-25, h3-27"),
              ElementsAre(version_draft_25, version_draft_27));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-25,h3-27"),
              ElementsAre(version_draft_25, version_draft_27));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-25,h3-27,h3-25"),
              ElementsAre(version_draft_25, version_draft_27));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-25,h3-27, h3-25"),
              ElementsAre(version_draft_25, version_draft_27));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-27,h3-25"),
              ElementsAre(version_draft_27, version_draft_25));

  EXPECT_THAT(ParseQuicVersionVectorString("h3-27,50"),
              ElementsAre(version_draft_27, version_q050));

  EXPECT_THAT(ParseQuicVersionVectorString("h3-Q050, h3-T050"),
              ElementsAre(version_q050, version_t050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-T050, h3-Q050"),
              ElementsAre(version_t050, version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50,h3-T050"),
              ElementsAre(version_q050, version_t050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-T050,QUIC_VERSION_50"),
              ElementsAre(version_t050, version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50, h3-T050"),
              ElementsAre(version_q050, version_t050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-T050, QUIC_VERSION_50"),
              ElementsAre(version_t050, version_q050));

  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50,QUIC_VERSION_46"),
              ElementsAre(version_q050, version_q046));
  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_46,QUIC_VERSION_50"),
              ElementsAre(version_q046, version_q050));

  // Regression test for https://crbug.com/1044952.
  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50, QUIC_VERSION_50"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-Q050, h3-Q050"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-T050, h3-T050"),
              ElementsAre(version_t050));
  EXPECT_THAT(ParseQuicVersionVectorString("h3-Q050, QUIC_VERSION_50"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString(
                  "QUIC_VERSION_50, h3-Q050, QUIC_VERSION_50, h3-Q050"),
              ElementsAre(version_q050));
  EXPECT_THAT(ParseQuicVersionVectorString("QUIC_VERSION_50, h3-T050, h3-Q050"),
              ElementsAre(version_q050, version_t050));

  EXPECT_THAT(ParseQuicVersionVectorString("99"), IsEmpty());
  EXPECT_THAT(ParseQuicVersionVectorString("70"), IsEmpty());
  EXPECT_THAT(ParseQuicVersionVectorString("h3-01"), IsEmpty());
  EXPECT_THAT(ParseQuicVersionVectorString("h3-01,h3-25"),
              ElementsAre(version_draft_25));
}

TEST_F(QuicVersionsTest, CreateQuicVersionLabel) {
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '3'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '6'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '8'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '5', '0'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50)));

  // Test a TLS version:
  EXPECT_EQ(MakeVersionLabel('T', '0', '5', '0'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50)));

  // Make sure the negotiation reserved version is in the IETF reserved space.
  EXPECT_EQ(MakeVersionLabel(0xda, 0x5a, 0x3a, 0x3a) & 0x0f0f0f0f,
            CreateQuicVersionLabel(ParsedQuicVersion(
                PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_RESERVED_FOR_NEGOTIATION)) &
                0x0f0f0f0f);

  // Make sure that disabling randomness works.
  SetQuicFlag(FLAGS_quic_disable_version_negotiation_grease_randomness, true);
  EXPECT_EQ(MakeVersionLabel(0xda, 0x5a, 0x3a, 0x3a),
            CreateQuicVersionLabel(ParsedQuicVersion(
                PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_RESERVED_FOR_NEGOTIATION)));
}

TEST_F(QuicVersionsTest, QuicVersionLabelToString) {
  QuicVersionLabelVector version_labels = {
      MakeVersionLabel('Q', '0', '3', '5'),
      MakeVersionLabel('Q', '0', '3', '7'),
      MakeVersionLabel('T', '0', '3', '8'),
  };

  EXPECT_EQ("Q035", QuicVersionLabelToString(version_labels[0]));
  EXPECT_EQ("T038", QuicVersionLabelToString(version_labels[2]));

  EXPECT_EQ("Q035,Q037,T038", QuicVersionLabelVectorToString(version_labels));
  EXPECT_EQ("Q035:Q037:T038",
            QuicVersionLabelVectorToString(version_labels, ":", 2));
  EXPECT_EQ("Q035|Q037|...",
            QuicVersionLabelVectorToString(version_labels, "|", 1));
}

TEST_F(QuicVersionsTest, QuicVersionToString) {
  EXPECT_EQ("QUIC_VERSION_UNSUPPORTED",
            QuicVersionToString(QUIC_VERSION_UNSUPPORTED));

  QuicTransportVersion single_version[] = {QUIC_VERSION_43};
  QuicTransportVersionVector versions_vector;
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(single_version); ++i) {
    versions_vector.push_back(single_version[i]);
  }
  EXPECT_EQ("QUIC_VERSION_43",
            QuicTransportVersionVectorToString(versions_vector));

  QuicTransportVersion multiple_versions[] = {QUIC_VERSION_UNSUPPORTED,
                                              QUIC_VERSION_43};
  versions_vector.clear();
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(multiple_versions); ++i) {
    versions_vector.push_back(multiple_versions[i]);
  }
  EXPECT_EQ("QUIC_VERSION_UNSUPPORTED,QUIC_VERSION_43",
            QuicTransportVersionVectorToString(versions_vector));

  // Make sure that all supported versions are present in QuicVersionToString.
  for (QuicTransportVersion transport_version : SupportedTransportVersions()) {
    EXPECT_NE("QUIC_VERSION_UNSUPPORTED",
              QuicVersionToString(transport_version));
  }
}

TEST_F(QuicVersionsTest, ParsedQuicVersionToString) {
  ParsedQuicVersion unsupported = UnsupportedQuicVersion();
  ParsedQuicVersion version43(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43);
  EXPECT_EQ("Q043", ParsedQuicVersionToString(version43));
  EXPECT_EQ("0", ParsedQuicVersionToString(unsupported));

  ParsedQuicVersionVector versions_vector = {version43};
  EXPECT_EQ("Q043", ParsedQuicVersionVectorToString(versions_vector));

  versions_vector = {unsupported, version43};
  EXPECT_EQ("0,Q043", ParsedQuicVersionVectorToString(versions_vector));
  EXPECT_EQ("0:Q043", ParsedQuicVersionVectorToString(versions_vector, ":",
                                                      versions_vector.size()));
  EXPECT_EQ("0|...", ParsedQuicVersionVectorToString(versions_vector, "|", 0));

  // Make sure that all supported versions are present in
  // ParsedQuicVersionToString.
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    EXPECT_NE("0", ParsedQuicVersionToString(version));
  }
}

TEST_F(QuicVersionsTest, FilterSupportedVersionsAllVersions) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_enable_version_draft_27, true);
  SetQuicReloadableFlag(quic_enable_version_draft_25_v3, true);
  SetQuicReloadableFlag(quic_enable_version_t050, true);
  SetQuicReloadableFlag(quic_disable_version_q050, false);
  SetQuicReloadableFlag(quic_disable_version_q049, false);
  SetQuicReloadableFlag(quic_disable_version_q048, false);
  SetQuicReloadableFlag(quic_disable_version_q046, false);
  SetQuicReloadableFlag(quic_disable_version_q043, false);

  ParsedQuicVersionVector expected_parsed_versions;
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50));

  ASSERT_EQ(expected_parsed_versions,
            FilterSupportedVersions(AllSupportedVersions()));
  ASSERT_EQ(expected_parsed_versions, AllSupportedVersions());
}

TEST_F(QuicVersionsTest, FilterSupportedVersionsNo99) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_enable_version_draft_27, false);
  SetQuicReloadableFlag(quic_enable_version_draft_25_v3, true);
  SetQuicReloadableFlag(quic_enable_version_t050, true);
  SetQuicReloadableFlag(quic_disable_version_q050, false);
  SetQuicReloadableFlag(quic_disable_version_q049, false);
  SetQuicReloadableFlag(quic_disable_version_q048, false);
  SetQuicReloadableFlag(quic_disable_version_q046, false);
  SetQuicReloadableFlag(quic_disable_version_q043, false);

  ParsedQuicVersionVector expected_parsed_versions;
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50));

  ASSERT_EQ(expected_parsed_versions,
            FilterSupportedVersions(AllSupportedVersions()));
}

TEST_F(QuicVersionsTest, FilterSupportedVersionsNoFlags) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_enable_version_draft_27, false);
  SetQuicReloadableFlag(quic_enable_version_draft_25_v3, false);
  SetQuicReloadableFlag(quic_enable_version_t050, false);
  SetQuicReloadableFlag(quic_disable_version_q050, false);
  SetQuicReloadableFlag(quic_disable_version_q049, false);
  SetQuicReloadableFlag(quic_disable_version_q048, false);
  SetQuicReloadableFlag(quic_disable_version_q046, false);
  SetQuicReloadableFlag(quic_disable_version_q043, false);

  ParsedQuicVersionVector expected_parsed_versions;
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43));

  ASSERT_EQ(expected_parsed_versions,
            FilterSupportedVersions(AllSupportedVersions()));
}

TEST_F(QuicVersionsTest, LookUpVersionByIndex) {
  QuicTransportVersionVector all_versions = {QUIC_VERSION_43};
  int version_count = all_versions.size();
  for (int i = -5; i <= version_count + 1; ++i) {
    if (i >= 0 && i < version_count) {
      EXPECT_EQ(all_versions[i], VersionOfIndex(all_versions, i)[0]);
    } else {
      EXPECT_EQ(QUIC_VERSION_UNSUPPORTED, VersionOfIndex(all_versions, i)[0]);
    }
  }
}

TEST_F(QuicVersionsTest, LookUpParsedVersionByIndex) {
  ParsedQuicVersionVector all_versions = AllSupportedVersions();
  int version_count = all_versions.size();
  for (int i = -5; i <= version_count + 1; ++i) {
    if (i >= 0 && i < version_count) {
      EXPECT_EQ(all_versions[i], ParsedVersionOfIndex(all_versions, i)[0]);
    } else {
      EXPECT_EQ(UnsupportedQuicVersion(),
                ParsedVersionOfIndex(all_versions, i)[0]);
    }
  }
}

TEST_F(QuicVersionsTest, ParsedVersionsToTransportVersions) {
  ParsedQuicVersionVector all_versions = AllSupportedVersions();
  QuicTransportVersionVector transport_versions =
      ParsedVersionsToTransportVersions(all_versions);
  ASSERT_EQ(all_versions.size(), transport_versions.size());
  for (size_t i = 0; i < all_versions.size(); ++i) {
    EXPECT_EQ(transport_versions[i], all_versions[i].transport_version);
  }
}

// This test may appear to be so simplistic as to be unnecessary,
// yet a typo was made in doing the #defines and it was caught
// only in some test far removed from here... Better safe than sorry.
TEST_F(QuicVersionsTest, CheckTransportVersionNumbersForTypos) {
  static_assert(SupportedTransportVersions().size() == 7u,
                "Supported versions out of sync");
  EXPECT_EQ(QUIC_VERSION_43, 43);
  EXPECT_EQ(QUIC_VERSION_46, 46);
  EXPECT_EQ(QUIC_VERSION_48, 48);
  EXPECT_EQ(QUIC_VERSION_49, 49);
  EXPECT_EQ(QUIC_VERSION_50, 50);
  EXPECT_EQ(QUIC_VERSION_IETF_DRAFT_25, 70);
  EXPECT_EQ(QUIC_VERSION_IETF_DRAFT_27, 71);
}

TEST_F(QuicVersionsTest, AlpnForVersion) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  ParsedQuicVersion parsed_version_q048 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48);
  ParsedQuicVersion parsed_version_q049 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49);
  ParsedQuicVersion parsed_version_q050 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50);
  ParsedQuicVersion parsed_version_t050 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50);
  ParsedQuicVersion parsed_version_draft_25 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25);
  ParsedQuicVersion parsed_version_draft_27 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27);

  EXPECT_EQ("h3-Q048", AlpnForVersion(parsed_version_q048));
  EXPECT_EQ("h3-Q049", AlpnForVersion(parsed_version_q049));
  EXPECT_EQ("h3-Q050", AlpnForVersion(parsed_version_q050));
  EXPECT_EQ("h3-T050", AlpnForVersion(parsed_version_t050));
  EXPECT_EQ("h3-25", AlpnForVersion(parsed_version_draft_25));
  EXPECT_EQ("h3-27", AlpnForVersion(parsed_version_draft_27));
}

TEST_F(QuicVersionsTest, QuicEnableVersion) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  ParsedQuicVersion parsed_version_draft_27 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27);
  ParsedQuicVersion parsed_version_draft_25 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25);
  ParsedQuicVersion parsed_version_q050 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50);
  ParsedQuicVersion parsed_version_t050 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50);

  {
    QuicFlagSaver flag_saver;
    SetQuicReloadableFlag(quic_enable_version_draft_27, false);
    QuicEnableVersion(parsed_version_draft_27);
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_draft_27));
  }

  {
    QuicFlagSaver flag_saver;
    SetQuicReloadableFlag(quic_enable_version_draft_25_v3, false);
    QuicEnableVersion(parsed_version_draft_25);
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_draft_25_v3));
  }

  {
    QuicFlagSaver flag_saver;
    SetQuicReloadableFlag(quic_disable_version_q050, true);
    QuicEnableVersion(parsed_version_q050);
    EXPECT_FALSE(GetQuicReloadableFlag(quic_disable_version_q050));
  }

  {
    QuicFlagSaver flag_saver;
    SetQuicReloadableFlag(quic_enable_version_t050, false);
    QuicEnableVersion(parsed_version_t050);
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_t050));
  }

  {
    QuicFlagSaver flag_saver;
    for (const ParsedQuicVersion& version : SupportedVersions()) {
      QuicEnableVersion(version);
    }
    ASSERT_EQ(AllSupportedVersions(), CurrentSupportedVersions());
  }
}

TEST_F(QuicVersionsTest, ReservedForNegotiation) {
  EXPECT_EQ(QUIC_VERSION_RESERVED_FOR_NEGOTIATION,
            QuicVersionReservedForNegotiation().transport_version);
  // QUIC_VERSION_RESERVED_FOR_NEGOTIATION MUST NOT be added to
  // kSupportedTransportVersions.
  for (QuicTransportVersion transport_version : SupportedTransportVersions()) {
    EXPECT_NE(QUIC_VERSION_RESERVED_FOR_NEGOTIATION, transport_version);
  }
}

TEST_F(QuicVersionsTest, SupportedVersionsHasCorrectList) {
  size_t index = 0;
  for (HandshakeProtocol handshake_protocol : SupportedHandshakeProtocols()) {
    for (QuicTransportVersion transport_version :
         SupportedTransportVersions()) {
      SCOPED_TRACE(index);
      if (ParsedQuicVersionIsValid(handshake_protocol, transport_version)) {
        EXPECT_EQ(SupportedVersions()[index],
                  ParsedQuicVersion(handshake_protocol, transport_version));
        index++;
      }
    }
  }
  EXPECT_EQ(SupportedVersions().size(), index);
}

TEST_F(QuicVersionsTest, SupportedVersionsAllDistinct) {
  for (size_t index1 = 0; index1 < SupportedVersions().size(); ++index1) {
    ParsedQuicVersion version1 = SupportedVersions()[index1];
    for (size_t index2 = index1 + 1; index2 < SupportedVersions().size();
         ++index2) {
      ParsedQuicVersion version2 = SupportedVersions()[index2];
      EXPECT_NE(version1, version2) << version1 << " " << version2;
      EXPECT_NE(CreateQuicVersionLabel(version1),
                CreateQuicVersionLabel(version2))
          << version1 << " " << version2;
      EXPECT_NE(AlpnForVersion(version1), AlpnForVersion(version2))
          << version1 << " " << version2;
    }
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
