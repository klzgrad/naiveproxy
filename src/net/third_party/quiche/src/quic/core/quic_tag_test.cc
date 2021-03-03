// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_tag.h"

#include "quic/core/crypto/crypto_protocol.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicTagTest : public QuicTest {};

TEST_F(QuicTagTest, TagToString) {
  EXPECT_EQ("SCFG", QuicTagToString(kSCFG));
  EXPECT_EQ("SNO ", QuicTagToString(kServerNonceTag));
  EXPECT_EQ("CRT ", QuicTagToString(kCertificateTag));
  EXPECT_EQ("CHLO", QuicTagToString(MakeQuicTag('C', 'H', 'L', 'O')));
  // A tag that contains a non-printing character will be printed as hex.
  EXPECT_EQ("43484c1f", QuicTagToString(MakeQuicTag('C', 'H', 'L', '\x1f')));
}

TEST_F(QuicTagTest, MakeQuicTag) {
  QuicTag tag = MakeQuicTag('A', 'B', 'C', 'D');
  char bytes[4];
  memcpy(bytes, &tag, 4);
  EXPECT_EQ('A', bytes[0]);
  EXPECT_EQ('B', bytes[1]);
  EXPECT_EQ('C', bytes[2]);
  EXPECT_EQ('D', bytes[3]);
}

TEST_F(QuicTagTest, ParseQuicTag) {
  QuicTag tag_abcd = MakeQuicTag('A', 'B', 'C', 'D');
  EXPECT_EQ(ParseQuicTag("ABCD"), tag_abcd);
  EXPECT_EQ(ParseQuicTag("ABCDE"), tag_abcd);
  QuicTag tag_efgh = MakeQuicTag('E', 'F', 'G', 'H');
  EXPECT_EQ(ParseQuicTag("EFGH"), tag_efgh);
  QuicTag tag_ijk = MakeQuicTag('I', 'J', 'K', 0);
  EXPECT_EQ(ParseQuicTag("IJK"), tag_ijk);
  QuicTag tag_l = MakeQuicTag('L', 0, 0, 0);
  EXPECT_EQ(ParseQuicTag("L"), tag_l);
  QuicTag tag_hex = MakeQuicTag('M', 'N', 'O', static_cast<char>(255));
  EXPECT_EQ(ParseQuicTag("4d4e4fff"), tag_hex);
  EXPECT_EQ(ParseQuicTag("4D4E4FFF"), tag_hex);
  QuicTag tag_with_numbers = MakeQuicTag('P', 'Q', '1', '2');
  EXPECT_EQ(ParseQuicTag("PQ12"), tag_with_numbers);
  QuicTag tag_with_custom_chars = MakeQuicTag('r', '$', '_', '7');
  EXPECT_EQ(ParseQuicTag("r$_7"), tag_with_custom_chars);
  QuicTag tag_zero = 0;
  EXPECT_EQ(ParseQuicTag(""), tag_zero);
  QuicTagVector tag_vector;
  EXPECT_EQ(ParseQuicTagVector(""), tag_vector);
  EXPECT_EQ(ParseQuicTagVector(" "), tag_vector);
  tag_vector.push_back(tag_abcd);
  EXPECT_EQ(ParseQuicTagVector("ABCD"), tag_vector);
  tag_vector.push_back(tag_efgh);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH"), tag_vector);
  tag_vector.push_back(tag_ijk);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK"), tag_vector);
  tag_vector.push_back(tag_l);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK,L"), tag_vector);
  tag_vector.push_back(tag_hex);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK,L,4d4e4fff"), tag_vector);
  tag_vector.push_back(tag_with_numbers);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK,L,4d4e4fff,PQ12"), tag_vector);
  tag_vector.push_back(tag_with_custom_chars);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK,L,4d4e4fff,PQ12,r$_7"),
            tag_vector);
  tag_vector.push_back(tag_zero);
  EXPECT_EQ(ParseQuicTagVector("ABCD,EFGH,IJK,L,4d4e4fff,PQ12,r$_7,"),
            tag_vector);
}

}  // namespace
}  // namespace test
}  // namespace quic
