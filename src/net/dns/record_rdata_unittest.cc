// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/record_rdata.h"

#include <memory>

#include "net/dns/dns_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

using ::testing::ElementsAreArray;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SizeIs;

base::StringPiece MakeStringPiece(const uint8_t* data, unsigned size) {
  const char* data_cc = reinterpret_cast<const char*>(data);
  return base::StringPiece(data_cc, size);
}

TEST(RecordRdataTest, ParseSrvRecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t
      record[] =
          {
              0x00, 0x01, 0x00, 0x02, 0x00, 0x50, 0x03, 'w',  'w',
              'w',  0x06, 'g',  'o',  'o',  'g',  'l',  'e',  0x03,
              'c',  'o',  'm',  0x00, 0x01, 0x01, 0x01, 0x02, 0x01,
              0x03, 0x04, 'w',  'w',  'w',  '2',  0xc0, 0x0a,  // Pointer to
                                                               // "google.com"
          };

  DnsRecordParser parser(record, sizeof(record), 0);
  const unsigned first_record_len = 22;
  base::StringPiece record1_strpiece = MakeStringPiece(
      record, first_record_len);
  base::StringPiece record2_strpiece = MakeStringPiece(
      record + first_record_len, sizeof(record) - first_record_len);

  std::unique_ptr<SrvRecordRdata> record1_obj =
      SrvRecordRdata::Create(record1_strpiece, parser);
  ASSERT_TRUE(record1_obj != NULL);
  ASSERT_EQ(1, record1_obj->priority());
  ASSERT_EQ(2, record1_obj->weight());
  ASSERT_EQ(80, record1_obj->port());

  ASSERT_EQ("www.google.com", record1_obj->target());

  std::unique_ptr<SrvRecordRdata> record2_obj =
      SrvRecordRdata::Create(record2_strpiece, parser);
  ASSERT_TRUE(record2_obj != NULL);
  ASSERT_EQ(257, record2_obj->priority());
  ASSERT_EQ(258, record2_obj->weight());
  ASSERT_EQ(259, record2_obj->port());

  ASSERT_EQ("www2.google.com", record2_obj->target());

  ASSERT_TRUE(record1_obj->IsEqual(record1_obj.get()));
  ASSERT_FALSE(record1_obj->IsEqual(record2_obj.get()));
}

TEST(RecordRdataTest, ParseARecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {
      0x7F, 0x00, 0x00, 0x01  // 127.0.0.1
  };

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<ARecordRdata> record_obj =
      ARecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  ASSERT_EQ("127.0.0.1", record_obj->address().ToString());

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, ParseAAAARecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {
      0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09  // 1234:5678::9A
  };

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<AAAARecordRdata> record_obj =
      AAAARecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  ASSERT_EQ("1234:5678::9", record_obj->address().ToString());

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, ParseCnameRecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {0x03, 'w', 'w', 'w',  0x06, 'g', 'o', 'o',
                            'g',  'l', 'e', 0x03, 'c',  'o', 'm', 0x00};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<CnameRecordRdata> record_obj =
      CnameRecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  ASSERT_EQ("www.google.com", record_obj->cname());

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, ParsePtrRecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {0x03, 'w', 'w', 'w',  0x06, 'g', 'o', 'o',
                            'g',  'l', 'e', 0x03, 'c',  'o', 'm', 0x00};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<PtrRecordRdata> record_obj =
      PtrRecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  ASSERT_EQ("www.google.com", record_obj->ptrdomain());

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, ParseTxtRecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {0x03, 'w', 'w', 'w',  0x06, 'g', 'o', 'o',
                            'g',  'l', 'e', 0x03, 'c',  'o', 'm'};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<TxtRecordRdata> record_obj =
      TxtRecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  std::vector<std::string> expected;
  expected.push_back("www");
  expected.push_back("google");
  expected.push_back("com");

  ASSERT_EQ(expected, record_obj->texts());

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, ParseNsecRecord) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.

  const uint8_t record[] = {0x03, 'w',  'w',  'w',  0x06, 'g', 'o',
                            'o',  'g',  'l',  'e',  0x03, 'c', 'o',
                            'm',  0x00, 0x00, 0x02, 0x40, 0x01};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<NsecRecordRdata> record_obj =
      NsecRecordRdata::Create(record_strpiece, parser);
  ASSERT_TRUE(record_obj != NULL);

  ASSERT_EQ(16u, record_obj->bitmap_length());

  EXPECT_FALSE(record_obj->GetBit(0));
  EXPECT_TRUE(record_obj->GetBit(1));
  for (int i = 2; i < 15; i++) {
    EXPECT_FALSE(record_obj->GetBit(i));
  }
  EXPECT_TRUE(record_obj->GetBit(15));

  ASSERT_TRUE(record_obj->IsEqual(record_obj.get()));
}

TEST(RecordRdataTest, CreateNsecRecordWithEmptyBitmapReturnsNull) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.
  // This record has a bitmap that is 0 bytes long.
  const uint8_t record[] = {0x03, 'w', 'w',  'w', 0x06, 'g', 'o',  'o',  'g',
                            'l',  'e', 0x03, 'c', 'o',  'm', 0x00, 0x00, 0x00};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<NsecRecordRdata> record_obj =
      NsecRecordRdata::Create(record_strpiece, parser);
  ASSERT_FALSE(record_obj);
}

TEST(RecordRdataTest, CreateNsecRecordWithOversizedBitmapReturnsNull) {
  // These are just the rdata portions of the DNS records, rather than complete
  // records, but it works well enough for this test.
  // This record has a bitmap that is 33 bytes long. The maximum size allowed by
  // RFC 3845, Section 2.1.2, is 32 bytes.
  const uint8_t record[] = {
      0x03, 'w',  'w',  'w',  0x06, 'g',  'o',  'o',  'g',  'l',  'e',
      0x03, 'c',  'o',  'm',  0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  DnsRecordParser parser(record, sizeof(record), 0);
  base::StringPiece record_strpiece = MakeStringPiece(record, sizeof(record));

  std::unique_ptr<NsecRecordRdata> record_obj =
      NsecRecordRdata::Create(record_strpiece, parser);
  ASSERT_FALSE(record_obj);
}

TEST(RecordRdataTest, ParseOptRecord) {
  // This is just the rdata portion of an OPT record, rather than a complete
  // record.
  const uint8_t rdata[] = {
      // First OPT
      0x00, 0x01,  // OPT code
      0x00, 0x02,  // OPT data size
      0xDE, 0xAD,  // OPT data
      // Second OPT
      0x00, 0xFF,             // OPT code
      0x00, 0x04,             // OPT data size
      0xDE, 0xAD, 0xBE, 0xEF  // OPT data
  };

  DnsRecordParser parser(rdata, sizeof(rdata), 0);
  base::StringPiece rdata_strpiece = MakeStringPiece(rdata, sizeof(rdata));

  std::unique_ptr<OptRecordRdata> rdata_obj =
      OptRecordRdata::Create(rdata_strpiece, parser);
  ASSERT_THAT(rdata_obj, NotNull());
  ASSERT_THAT(rdata_obj->opts(), SizeIs(2));
  ASSERT_EQ(1, rdata_obj->opts()[0].code());
  ASSERT_EQ("\xde\xad", rdata_obj->opts()[0].data());
  ASSERT_EQ(255, rdata_obj->opts()[1].code());
  ASSERT_EQ("\xde\xad\xbe\xef", rdata_obj->opts()[1].data());
  ASSERT_TRUE(rdata_obj->IsEqual(rdata_obj.get()));
}

TEST(RecordRdataTest, ParseOptRecordWithShorterSizeThanData) {
  // This is just the rdata portion of an OPT record, rather than a complete
  // record.
  const uint8_t rdata[] = {
      0x00, 0xFF,             // OPT code
      0x00, 0x02,             // OPT data size (incorrect, should be 4)
      0xDE, 0xAD, 0xBE, 0xEF  // OPT data
  };

  DnsRecordParser parser(rdata, sizeof(rdata), 0);
  base::StringPiece rdata_strpiece = MakeStringPiece(rdata, sizeof(rdata));

  std::unique_ptr<OptRecordRdata> rdata_obj =
      OptRecordRdata::Create(rdata_strpiece, parser);
  ASSERT_THAT(rdata_obj, IsNull());
}

TEST(RecordRdataTest, ParseOptRecordWithLongerSizeThanData) {
  // This is just the rdata portion of an OPT record, rather than a complete
  // record.
  const uint8_t rdata[] = {
      0x00, 0xFF,  // OPT code
      0x00, 0x04,  // OPT data size (incorrect, should be 4)
      0xDE, 0xAD   // OPT data
  };

  DnsRecordParser parser(rdata, sizeof(rdata), 0);
  base::StringPiece rdata_strpiece = MakeStringPiece(rdata, sizeof(rdata));

  std::unique_ptr<OptRecordRdata> rdata_obj =
      OptRecordRdata::Create(rdata_strpiece, parser);
  ASSERT_THAT(rdata_obj, IsNull());
}

TEST(RecordRdataTest, AddOptToOptRecord) {
  // This is just the rdata portion of an OPT record, rather than a complete
  // record.
  const uint8_t expected_rdata[] = {
      0x00, 0xFF,             // OPT code
      0x00, 0x04,             // OPT data size
      0xDE, 0xAD, 0xBE, 0xEF  // OPT data
  };

  OptRecordRdata rdata;
  rdata.AddOpt(OptRecordRdata::Opt(255, "\xde\xad\xbe\xef"));
  EXPECT_THAT(rdata.buf(), ElementsAreArray(expected_rdata));
}

}  // namespace
}  // namespace net
