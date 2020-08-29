// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/quiche_data_writer.h"

#include <cstdint>
#include <cstring>

#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/common/quiche_data_reader.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace quiche {
namespace test {
namespace {

char* AsChars(unsigned char* data) {
  return reinterpret_cast<char*>(data);
}

struct TestParams {
  explicit TestParams(quiche::Endianness endianness) : endianness(endianness) {}

  quiche::Endianness endianness;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  return quiche::QuicheStrCat(
      (p.endianness == quiche::NETWORK_BYTE_ORDER ? "Network" : "Host"),
      "ByteOrder");
}

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  for (quiche::Endianness endianness :
       {quiche::NETWORK_BYTE_ORDER, quiche::HOST_BYTE_ORDER}) {
    params.push_back(TestParams(endianness));
  }
  return params;
}

class QuicheDataWriterTest : public QuicheTestWithParam<TestParams> {};

INSTANTIATE_TEST_SUITE_P(QuicheDataWriterTests,
                         QuicheDataWriterTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicheDataWriterTest, Write16BitUnsignedIntegers) {
  char little_endian16[] = {0x22, 0x11};
  char big_endian16[] = {0x11, 0x22};
  char buffer16[2];
  {
    uint16_t in_memory16 = 0x1122;
    QuicheDataWriter writer(2, buffer16, GetParam().endianness);
    writer.WriteUInt16(in_memory16);
    test::CompareCharArraysWithHexError(
        "uint16_t", buffer16, 2,
        GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian16
                                                            : little_endian16,
        2);

    uint16_t read_number16;
    QuicheDataReader reader(buffer16, 2, GetParam().endianness);
    reader.ReadUInt16(&read_number16);
    EXPECT_EQ(in_memory16, read_number16);
  }

  {
    uint64_t in_memory16 = 0x0000000000001122;
    QuicheDataWriter writer(2, buffer16, GetParam().endianness);
    writer.WriteBytesToUInt64(2, in_memory16);
    test::CompareCharArraysWithHexError(
        "uint16_t", buffer16, 2,
        GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian16
                                                            : little_endian16,
        2);

    uint64_t read_number16;
    QuicheDataReader reader(buffer16, 2, GetParam().endianness);
    reader.ReadBytesToUInt64(2, &read_number16);
    EXPECT_EQ(in_memory16, read_number16);
  }
}

TEST_P(QuicheDataWriterTest, Write24BitUnsignedIntegers) {
  char little_endian24[] = {0x33, 0x22, 0x11};
  char big_endian24[] = {0x11, 0x22, 0x33};
  char buffer24[3];
  uint64_t in_memory24 = 0x0000000000112233;
  QuicheDataWriter writer(3, buffer24, GetParam().endianness);
  writer.WriteBytesToUInt64(3, in_memory24);
  test::CompareCharArraysWithHexError(
      "uint24", buffer24, 3,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian24
                                                          : little_endian24,
      3);

  uint64_t read_number24;
  QuicheDataReader reader(buffer24, 3, GetParam().endianness);
  reader.ReadBytesToUInt64(3, &read_number24);
  EXPECT_EQ(in_memory24, read_number24);
}

TEST_P(QuicheDataWriterTest, Write32BitUnsignedIntegers) {
  char little_endian32[] = {0x44, 0x33, 0x22, 0x11};
  char big_endian32[] = {0x11, 0x22, 0x33, 0x44};
  char buffer32[4];
  {
    uint32_t in_memory32 = 0x11223344;
    QuicheDataWriter writer(4, buffer32, GetParam().endianness);
    writer.WriteUInt32(in_memory32);
    test::CompareCharArraysWithHexError(
        "uint32_t", buffer32, 4,
        GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian32
                                                            : little_endian32,
        4);

    uint32_t read_number32;
    QuicheDataReader reader(buffer32, 4, GetParam().endianness);
    reader.ReadUInt32(&read_number32);
    EXPECT_EQ(in_memory32, read_number32);
  }

  {
    uint64_t in_memory32 = 0x11223344;
    QuicheDataWriter writer(4, buffer32, GetParam().endianness);
    writer.WriteBytesToUInt64(4, in_memory32);
    test::CompareCharArraysWithHexError(
        "uint32_t", buffer32, 4,
        GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian32
                                                            : little_endian32,
        4);

    uint64_t read_number32;
    QuicheDataReader reader(buffer32, 4, GetParam().endianness);
    reader.ReadBytesToUInt64(4, &read_number32);
    EXPECT_EQ(in_memory32, read_number32);
  }
}

TEST_P(QuicheDataWriterTest, Write40BitUnsignedIntegers) {
  uint64_t in_memory40 = 0x0000001122334455;
  char little_endian40[] = {0x55, 0x44, 0x33, 0x22, 0x11};
  char big_endian40[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  char buffer40[5];
  QuicheDataWriter writer(5, buffer40, GetParam().endianness);
  writer.WriteBytesToUInt64(5, in_memory40);
  test::CompareCharArraysWithHexError(
      "uint40", buffer40, 5,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian40
                                                          : little_endian40,
      5);

  uint64_t read_number40;
  QuicheDataReader reader(buffer40, 5, GetParam().endianness);
  reader.ReadBytesToUInt64(5, &read_number40);
  EXPECT_EQ(in_memory40, read_number40);
}

TEST_P(QuicheDataWriterTest, Write48BitUnsignedIntegers) {
  uint64_t in_memory48 = 0x0000112233445566;
  char little_endian48[] = {0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  char big_endian48[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  char buffer48[6];
  QuicheDataWriter writer(6, buffer48, GetParam().endianness);
  writer.WriteBytesToUInt64(6, in_memory48);
  test::CompareCharArraysWithHexError(
      "uint48", buffer48, 6,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian48
                                                          : little_endian48,
      6);

  uint64_t read_number48;
  QuicheDataReader reader(buffer48, 6, GetParam().endianness);
  reader.ReadBytesToUInt64(6., &read_number48);
  EXPECT_EQ(in_memory48, read_number48);
}

TEST_P(QuicheDataWriterTest, Write56BitUnsignedIntegers) {
  uint64_t in_memory56 = 0x0011223344556677;
  char little_endian56[] = {0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  char big_endian56[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  char buffer56[7];
  QuicheDataWriter writer(7, buffer56, GetParam().endianness);
  writer.WriteBytesToUInt64(7, in_memory56);
  test::CompareCharArraysWithHexError(
      "uint56", buffer56, 7,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER ? big_endian56
                                                          : little_endian56,
      7);

  uint64_t read_number56;
  QuicheDataReader reader(buffer56, 7, GetParam().endianness);
  reader.ReadBytesToUInt64(7, &read_number56);
  EXPECT_EQ(in_memory56, read_number56);
}

TEST_P(QuicheDataWriterTest, Write64BitUnsignedIntegers) {
  uint64_t in_memory64 = 0x1122334455667788;
  unsigned char little_endian64[] = {0x88, 0x77, 0x66, 0x55,
                                     0x44, 0x33, 0x22, 0x11};
  unsigned char big_endian64[] = {0x11, 0x22, 0x33, 0x44,
                                  0x55, 0x66, 0x77, 0x88};
  char buffer64[8];
  QuicheDataWriter writer(8, buffer64, GetParam().endianness);
  writer.WriteBytesToUInt64(8, in_memory64);
  test::CompareCharArraysWithHexError(
      "uint64_t", buffer64, 8,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER
          ? AsChars(big_endian64)
          : AsChars(little_endian64),
      8);

  uint64_t read_number64;
  QuicheDataReader reader(buffer64, 8, GetParam().endianness);
  reader.ReadBytesToUInt64(8, &read_number64);
  EXPECT_EQ(in_memory64, read_number64);

  QuicheDataWriter writer2(8, buffer64, GetParam().endianness);
  writer2.WriteUInt64(in_memory64);
  test::CompareCharArraysWithHexError(
      "uint64_t", buffer64, 8,
      GetParam().endianness == quiche::NETWORK_BYTE_ORDER
          ? AsChars(big_endian64)
          : AsChars(little_endian64),
      8);
  read_number64 = 0u;
  QuicheDataReader reader2(buffer64, 8, GetParam().endianness);
  reader2.ReadUInt64(&read_number64);
  EXPECT_EQ(in_memory64, read_number64);
}

TEST_P(QuicheDataWriterTest, WriteIntegers) {
  char buf[43];
  uint8_t i8 = 0x01;
  uint16_t i16 = 0x0123;
  uint32_t i32 = 0x01234567;
  uint64_t i64 = 0x0123456789ABCDEF;
  QuicheDataWriter writer(46, buf, GetParam().endianness);
  for (size_t i = 0; i < 10; ++i) {
    switch (i) {
      case 0u:
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      case 1u:
        EXPECT_TRUE(writer.WriteUInt8(i8));
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      case 2u:
        EXPECT_TRUE(writer.WriteUInt16(i16));
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      case 3u:
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      case 4u:
        EXPECT_TRUE(writer.WriteUInt32(i32));
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      case 5u:
      case 6u:
      case 7u:
      case 8u:
        EXPECT_TRUE(writer.WriteBytesToUInt64(i, i64));
        break;
      default:
        EXPECT_FALSE(writer.WriteBytesToUInt64(i, i64));
    }
  }

  QuicheDataReader reader(buf, 46, GetParam().endianness);
  for (size_t i = 0; i < 10; ++i) {
    uint8_t read8;
    uint16_t read16;
    uint32_t read32;
    uint64_t read64;
    switch (i) {
      case 0u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0u, read64);
        break;
      case 1u:
        EXPECT_TRUE(reader.ReadUInt8(&read8));
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(i8, read8);
        EXPECT_EQ(0xEFu, read64);
        break;
      case 2u:
        EXPECT_TRUE(reader.ReadUInt16(&read16));
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(i16, read16);
        EXPECT_EQ(0xCDEFu, read64);
        break;
      case 3u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0xABCDEFu, read64);
        break;
      case 4u:
        EXPECT_TRUE(reader.ReadUInt32(&read32));
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(i32, read32);
        EXPECT_EQ(0x89ABCDEFu, read64);
        break;
      case 5u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0x6789ABCDEFu, read64);
        break;
      case 6u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0x456789ABCDEFu, read64);
        break;
      case 7u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0x23456789ABCDEFu, read64);
        break;
      case 8u:
        EXPECT_TRUE(reader.ReadBytesToUInt64(i, &read64));
        EXPECT_EQ(0x0123456789ABCDEFu, read64);
        break;
      default:
        EXPECT_FALSE(reader.ReadBytesToUInt64(i, &read64));
    }
  }
}

TEST_P(QuicheDataWriterTest, WriteBytes) {
  char bytes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  char buf[QUICHE_ARRAYSIZE(bytes)];
  QuicheDataWriter writer(QUICHE_ARRAYSIZE(buf), buf, GetParam().endianness);
  EXPECT_TRUE(writer.WriteBytes(bytes, QUICHE_ARRAYSIZE(bytes)));
  for (unsigned int i = 0; i < QUICHE_ARRAYSIZE(bytes); ++i) {
    EXPECT_EQ(bytes[i], buf[i]);
  }
}

TEST_P(QuicheDataWriterTest, Seek) {
  char buffer[3] = {};
  QuicheDataWriter writer(QUICHE_ARRAYSIZE(buffer), buffer,
                          GetParam().endianness);
  EXPECT_TRUE(writer.WriteUInt8(42));
  EXPECT_TRUE(writer.Seek(1));
  EXPECT_TRUE(writer.WriteUInt8(3));

  char expected[] = {42, 0, 3};
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(expected); ++i) {
    EXPECT_EQ(buffer[i], expected[i]);
  }
}

TEST_P(QuicheDataWriterTest, SeekTooFarFails) {
  char buffer[20];

  // Check that one can seek to the end of the writer, but not past.
  {
    QuicheDataWriter writer(QUICHE_ARRAYSIZE(buffer), buffer,
                            GetParam().endianness);
    EXPECT_TRUE(writer.Seek(20));
    EXPECT_FALSE(writer.Seek(1));
  }

  // Seeking several bytes past the end fails.
  {
    QuicheDataWriter writer(QUICHE_ARRAYSIZE(buffer), buffer,
                            GetParam().endianness);
    EXPECT_FALSE(writer.Seek(100));
  }

  // Seeking so far that arithmetic overflow could occur also fails.
  {
    QuicheDataWriter writer(QUICHE_ARRAYSIZE(buffer), buffer,
                            GetParam().endianness);
    EXPECT_TRUE(writer.Seek(10));
    EXPECT_FALSE(writer.Seek(std::numeric_limits<size_t>::max()));
  }
}

TEST_P(QuicheDataWriterTest, PayloadReads) {
  char buffer[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  char expected_first_read[4] = {1, 2, 3, 4};
  char expected_remaining[12] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  QuicheDataReader reader(buffer, sizeof(buffer));
  quiche::QuicheStringPiece previously_read_payload1 =
      reader.PreviouslyReadPayload();
  EXPECT_TRUE(previously_read_payload1.empty());
  char first_read_buffer[4] = {};
  EXPECT_TRUE(reader.ReadBytes(first_read_buffer, sizeof(first_read_buffer)));
  test::CompareCharArraysWithHexError(
      "first read", first_read_buffer, sizeof(first_read_buffer),
      expected_first_read, sizeof(expected_first_read));
  quiche::QuicheStringPiece peeked_remaining_payload =
      reader.PeekRemainingPayload();
  test::CompareCharArraysWithHexError(
      "peeked_remaining_payload", peeked_remaining_payload.data(),
      peeked_remaining_payload.length(), expected_remaining,
      sizeof(expected_remaining));
  quiche::QuicheStringPiece full_payload = reader.FullPayload();
  test::CompareCharArraysWithHexError("full_payload", full_payload.data(),
                                      full_payload.length(), buffer,
                                      sizeof(buffer));
  quiche::QuicheStringPiece previously_read_payload2 =
      reader.PreviouslyReadPayload();
  test::CompareCharArraysWithHexError(
      "previously_read_payload2", previously_read_payload2.data(),
      previously_read_payload2.length(), first_read_buffer,
      sizeof(first_read_buffer));
  quiche::QuicheStringPiece read_remaining_payload =
      reader.ReadRemainingPayload();
  test::CompareCharArraysWithHexError(
      "read_remaining_payload", read_remaining_payload.data(),
      read_remaining_payload.length(), expected_remaining,
      sizeof(expected_remaining));
  EXPECT_TRUE(reader.IsDoneReading());
  quiche::QuicheStringPiece full_payload2 = reader.FullPayload();
  test::CompareCharArraysWithHexError("full_payload2", full_payload2.data(),
                                      full_payload2.length(), buffer,
                                      sizeof(buffer));
  quiche::QuicheStringPiece previously_read_payload3 =
      reader.PreviouslyReadPayload();
  test::CompareCharArraysWithHexError(
      "previously_read_payload3", previously_read_payload3.data(),
      previously_read_payload3.length(), buffer, sizeof(buffer));
}

}  // namespace
}  // namespace test
}  // namespace quiche
