// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_versions.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mock_log.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

using testing::_;

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
  EXPECT_EQ(MakeQuicTag('9', '3', '0', 'Q'),
            QuicVersionToQuicVersionLabel(QUIC_VERSION_39));

  // Loop over all supported versions and make sure that we never hit the
  // default case (i.e. all supported versions should be successfully converted
  // to valid QuicVersionLabels).
  for (size_t i = 0; i < QUIC_ARRAYSIZE(kSupportedTransportVersions); ++i) {
    QuicTransportVersion version = kSupportedTransportVersions[i];
    EXPECT_LT(0u, QuicVersionToQuicVersionLabel(version));
  }
}

TEST_F(QuicVersionsTest, QuicVersionToQuicVersionLabelUnsupported) {
  EXPECT_QUIC_BUG(QuicVersionToQuicVersionLabel(QUIC_VERSION_UNSUPPORTED),
                  "Unsupported QuicTransportVersion: 0");
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
  EXPECT_EQ(QUIC_VERSION_39,
            QuicVersionLabelToQuicVersion(MakeQuicTag('9', '3', '0', 'Q')));

  for (size_t i = 0; i < QUIC_ARRAYSIZE(kSupportedTransportVersions); ++i) {
    QuicTransportVersion version = kSupportedTransportVersions[i];

    // Get the label from the version (we can loop over QuicVersions easily).
    QuicVersionLabel version_label = QuicVersionToQuicVersionLabel(version);
    EXPECT_LT(0u, version_label);

    // Now try converting back.
    QuicTransportVersion label_to_transport_version =
        QuicVersionLabelToQuicVersion(version_label);
    EXPECT_EQ(version, label_to_transport_version);
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

  for (size_t i = 0; i < QUIC_ARRAYSIZE(kSupportedTransportVersions); ++i) {
    QuicVersionLabel version_label =
        QuicVersionToQuicVersionLabel(kSupportedTransportVersions[i]);
    EXPECT_EQ(PROTOCOL_QUIC_CRYPTO,
              QuicVersionLabelToHandshakeProtocol(version_label));
  }

  // Test a TLS version:
  QuicTag tls_tag = MakeQuicTag('3', '4', '0', 'T');
  EXPECT_EQ(PROTOCOL_TLS1_3, QuicVersionLabelToHandshakeProtocol(tls_tag));
}

TEST_F(QuicVersionsTest, ParseQuicVersionLabel) {
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '3', '9')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '3')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '6')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '7')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48),
            ParseQuicVersionLabel(MakeVersionLabel('Q', '0', '4', '8')));

  // Test TLS versions:
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_39),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '3', '9')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_43),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '4', '3')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_46),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '4', '6')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_47),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '4', '7')));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_48),
            ParseQuicVersionLabel(MakeVersionLabel('T', '0', '4', '8')));
}

TEST_F(QuicVersionsTest, ParseQuicVersionString) {
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39),
            ParseQuicVersionString("Q039"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43),
            ParseQuicVersionString("Q043"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
            ParseQuicVersionString("Q046"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47),
            ParseQuicVersionString("Q047"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48),
            ParseQuicVersionString("Q048"));

  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString(""));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("Q 47"));
  EXPECT_EQ(UnsupportedQuicVersion(), ParseQuicVersionString("Q047 "));

  // Test a TLS version:
  SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_39),
            ParseQuicVersionString("T039"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_43),
            ParseQuicVersionString("T043"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_46),
            ParseQuicVersionString("T046"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_47),
            ParseQuicVersionString("T047"));
  EXPECT_EQ(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_48),
            ParseQuicVersionString("T048"));
}

TEST_F(QuicVersionsTest, CreateQuicVersionLabel) {
  EXPECT_EQ(MakeVersionLabel('Q', '0', '3', '9'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '3'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '6'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '7'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47)));
  EXPECT_EQ(MakeVersionLabel('Q', '0', '4', '8'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48)));

  // Test a TLS version:
  EXPECT_EQ(MakeVersionLabel('T', '0', '3', '9'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_39)));
  EXPECT_EQ(MakeVersionLabel('T', '0', '4', '3'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_43)));
  EXPECT_EQ(MakeVersionLabel('T', '0', '4', '6'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_46)));
  EXPECT_EQ(MakeVersionLabel('T', '0', '4', '7'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_47)));
  EXPECT_EQ(MakeVersionLabel('T', '0', '4', '8'),
            CreateQuicVersionLabel(
                ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_48)));

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
  EXPECT_EQ("QUIC_VERSION_39", QuicVersionToString(QUIC_VERSION_39));
  EXPECT_EQ("QUIC_VERSION_UNSUPPORTED",
            QuicVersionToString(QUIC_VERSION_UNSUPPORTED));

  QuicTransportVersion single_version[] = {QUIC_VERSION_39};
  QuicTransportVersionVector versions_vector;
  for (size_t i = 0; i < QUIC_ARRAYSIZE(single_version); ++i) {
    versions_vector.push_back(single_version[i]);
  }
  EXPECT_EQ("QUIC_VERSION_39",
            QuicTransportVersionVectorToString(versions_vector));

  QuicTransportVersion multiple_versions[] = {QUIC_VERSION_UNSUPPORTED,
                                              QUIC_VERSION_39};
  versions_vector.clear();
  for (size_t i = 0; i < QUIC_ARRAYSIZE(multiple_versions); ++i) {
    versions_vector.push_back(multiple_versions[i]);
  }
  EXPECT_EQ("QUIC_VERSION_UNSUPPORTED,QUIC_VERSION_39",
            QuicTransportVersionVectorToString(versions_vector));

  // Make sure that all supported versions are present in QuicVersionToString.
  for (size_t i = 0; i < QUIC_ARRAYSIZE(kSupportedTransportVersions); ++i) {
    QuicTransportVersion version = kSupportedTransportVersions[i];
    EXPECT_NE("QUIC_VERSION_UNSUPPORTED", QuicVersionToString(version));
  }
}

TEST_F(QuicVersionsTest, ParsedQuicVersionToString) {
  ParsedQuicVersion unsupported = UnsupportedQuicVersion();
  ParsedQuicVersion version39(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_39);
  EXPECT_EQ("Q039", ParsedQuicVersionToString(version39));
  EXPECT_EQ("0", ParsedQuicVersionToString(unsupported));

  ParsedQuicVersionVector versions_vector = {version39};
  EXPECT_EQ("Q039", ParsedQuicVersionVectorToString(versions_vector));

  versions_vector = {unsupported, version39};
  EXPECT_EQ("0,Q039", ParsedQuicVersionVectorToString(versions_vector));
  EXPECT_EQ("0:Q039", ParsedQuicVersionVectorToString(versions_vector, ":",
                                                      versions_vector.size()));
  EXPECT_EQ("0|...", ParsedQuicVersionVectorToString(versions_vector, "|", 0));

  // Make sure that all supported versions are present in
  // ParsedQuicVersionToString.
  for (QuicTransportVersion transport_version : kSupportedTransportVersions) {
    for (HandshakeProtocol protocol : kSupportedHandshakeProtocols) {
      EXPECT_NE("0", ParsedQuicVersionToString(
                         ParsedQuicVersion(protocol, transport_version)));
    }
  }
}
TEST_F(QuicVersionsTest, AllSupportedTransportVersions) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  ASSERT_EQ(QUIC_ARRAYSIZE(kSupportedTransportVersions), all_versions.size());
  for (size_t i = 0; i < all_versions.size(); ++i) {
    EXPECT_EQ(kSupportedTransportVersions[i], all_versions[i]);
  }
}

TEST_F(QuicVersionsTest, FilterSupportedTransportVersionsAllVersions) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, true);
  SetQuicReloadableFlag(quic_enable_version_48_2, true);
  SetQuicReloadableFlag(quic_enable_version_99, true);
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : all_versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  QuicTransportVersionVector expected_versions = {
      QUIC_VERSION_99, QUIC_VERSION_48, QUIC_VERSION_47,
      QUIC_VERSION_46, QUIC_VERSION_43, QUIC_VERSION_39};
  ParsedQuicVersionVector expected_parsed_versions;
  for (QuicTransportVersion version : expected_versions) {
    expected_parsed_versions.push_back(
        ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }

  ASSERT_EQ(expected_versions, FilterSupportedTransportVersions(all_versions));
  ASSERT_EQ(expected_parsed_versions, FilterSupportedVersions(parsed_versions));
}

TEST_F(QuicVersionsTest, FilterSupportedTransportVersionsNo99) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, true);
  SetQuicReloadableFlag(quic_enable_version_48_2, true);
  SetQuicReloadableFlag(quic_enable_version_99, false);
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : all_versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  QuicTransportVersionVector expected_versions = {
      QUIC_VERSION_48, QUIC_VERSION_47, QUIC_VERSION_46, QUIC_VERSION_43,
      QUIC_VERSION_39};
  ParsedQuicVersionVector expected_parsed_versions;
  for (QuicTransportVersion version : expected_versions) {
    expected_parsed_versions.push_back(
        ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }

  ASSERT_EQ(expected_versions, FilterSupportedTransportVersions(all_versions));
  ASSERT_EQ(expected_parsed_versions, FilterSupportedVersions(parsed_versions));
}

TEST_F(QuicVersionsTest, FilterSupportedTransportVersionsNo48) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, true);
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  SetQuicReloadableFlag(quic_enable_version_99, false);
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : all_versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  QuicTransportVersionVector expected_versions = {
      QUIC_VERSION_47, QUIC_VERSION_46, QUIC_VERSION_43, QUIC_VERSION_39};
  ParsedQuicVersionVector expected_parsed_versions;
  for (QuicTransportVersion version : expected_versions) {
    expected_parsed_versions.push_back(
        ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }

  ASSERT_EQ(expected_versions, FilterSupportedTransportVersions(all_versions));
  ASSERT_EQ(expected_parsed_versions, FilterSupportedVersions(parsed_versions));
}

TEST_F(QuicVersionsTest, FilterSupportedTransportVersionsNo47) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, false);
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  SetQuicReloadableFlag(quic_enable_version_99, false);
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : all_versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  QuicTransportVersionVector expected_versions = {
      QUIC_VERSION_46, QUIC_VERSION_43, QUIC_VERSION_39};
  ParsedQuicVersionVector expected_parsed_versions;
  for (QuicTransportVersion version : expected_versions) {
    expected_parsed_versions.push_back(
        ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }

  ASSERT_EQ(expected_versions, FilterSupportedTransportVersions(all_versions));
  ASSERT_EQ(expected_parsed_versions, FilterSupportedVersions(parsed_versions));
}

TEST_F(QuicVersionsTest, FilterSupportedTransportVersionsNo39) {
  QuicTransportVersionVector all_versions = AllSupportedTransportVersions();
  SetQuicReloadableFlag(quic_disable_version_39, true);
  SetQuicReloadableFlag(quic_enable_version_47, false);
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  SetQuicReloadableFlag(quic_enable_version_99, false);
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : all_versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  QuicTransportVersionVector expected_versions = {QUIC_VERSION_46,
                                                  QUIC_VERSION_43};
  ParsedQuicVersionVector expected_parsed_versions;
  for (QuicTransportVersion version : expected_versions) {
    expected_parsed_versions.push_back(
        ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }

  ASSERT_EQ(expected_versions, FilterSupportedTransportVersions(all_versions));
  ASSERT_EQ(expected_parsed_versions, FilterSupportedVersions(parsed_versions));
}

TEST_F(QuicVersionsTest, LookUpVersionByIndex) {
  QuicTransportVersionVector all_versions = {QUIC_VERSION_39};
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
TEST_F(QuicVersionsTest, CheckVersionNumbersForTypos) {
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Supported versions out of sync");
  EXPECT_EQ(QUIC_VERSION_39, 39);
  EXPECT_EQ(QUIC_VERSION_43, 43);
  EXPECT_EQ(QUIC_VERSION_46, 46);
  EXPECT_EQ(QUIC_VERSION_47, 47);
  EXPECT_EQ(QUIC_VERSION_48, 48);
  EXPECT_EQ(QUIC_VERSION_99, 99);
}

TEST_F(QuicVersionsTest, AlpnForVersion) {
  ParsedQuicVersion parsed_version_q047 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47);
  ParsedQuicVersion parsed_version_t047 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_47);
  ParsedQuicVersion parsed_version_q048 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48);
  ParsedQuicVersion parsed_version_t048 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_48);
  ParsedQuicVersion parsed_version_t099 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99);

  EXPECT_EQ("h3-Q047", AlpnForVersion(parsed_version_q047));
  EXPECT_EQ("h3-T047", AlpnForVersion(parsed_version_t047));
  EXPECT_EQ("h3-Q048", AlpnForVersion(parsed_version_q048));
  EXPECT_EQ("h3-T048", AlpnForVersion(parsed_version_t048));
  EXPECT_EQ("h3-22", AlpnForVersion(parsed_version_t099));
}

TEST_F(QuicVersionsTest, QuicEnableVersion) {
  SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
  ParsedQuicVersion parsed_version_q047 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_47);
  ParsedQuicVersion parsed_version_t047 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_47);
  ParsedQuicVersion parsed_version_q048 =
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48);
  ParsedQuicVersion parsed_version_t048 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_48);
  ParsedQuicVersion parsed_version_t099 =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99);
  SetQuicFlag(FLAGS_quic_supports_tls_handshake, false);
  SetQuicReloadableFlag(quic_disable_version_39, false);
  SetQuicReloadableFlag(quic_enable_version_47, false);
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  SetQuicReloadableFlag(quic_enable_version_99, false);

  {
    QuicFlagSaver flag_saver;
    QuicEnableVersion(parsed_version_q047);
    EXPECT_FALSE(GetQuicFlag(FLAGS_quic_supports_tls_handshake));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_47));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_48_2));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_99));
  }

  {
    QuicFlagSaver flag_saver;
    QuicEnableVersion(parsed_version_t047);
    EXPECT_TRUE(GetQuicFlag(FLAGS_quic_supports_tls_handshake));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_47));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_48_2));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_99));
  }

  {
    QuicFlagSaver flag_saver;
    QuicEnableVersion(parsed_version_q048);
    EXPECT_FALSE(GetQuicFlag(FLAGS_quic_supports_tls_handshake));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_47));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_48_2));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_99));
  }

  {
    QuicFlagSaver flag_saver;
    QuicEnableVersion(parsed_version_t048);
    EXPECT_TRUE(GetQuicFlag(FLAGS_quic_supports_tls_handshake));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_47));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_48_2));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_99));
  }

  {
    QuicFlagSaver flag_saver;
    QuicEnableVersion(parsed_version_t099);
    EXPECT_TRUE(GetQuicFlag(FLAGS_quic_supports_tls_handshake));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_47));
    EXPECT_FALSE(GetQuicReloadableFlag(quic_enable_version_48_2));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_enable_version_99));
  }
}

TEST_F(QuicVersionsTest, ReservedForNegotiation) {
  EXPECT_EQ(QUIC_VERSION_RESERVED_FOR_NEGOTIATION,
            QuicVersionReservedForNegotiation().transport_version);
  // QUIC_VERSION_RESERVED_FOR_NEGOTIATION MUST NOT be added to
  // kSupportedTransportVersions.
  for (size_t i = 0; i < QUIC_ARRAYSIZE(kSupportedTransportVersions); ++i) {
    EXPECT_NE(QUIC_VERSION_RESERVED_FOR_NEGOTIATION,
              kSupportedTransportVersions[i]);
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
