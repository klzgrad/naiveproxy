/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/protozero/test/example_proto/test_messages.pbzero.h"
#include "src/trace_processor/importers/proto/track_event.descriptor.h"
#include "src/trace_processor/test_messages.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/protozero_to_text.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/track_event/chrome_compositor_scheduler_state.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto::trace_processor::protozero_to_text {

namespace {

constexpr size_t kChunkSize = 42;

using ::protozero::test::protos::pbzero::EveryField;
using ::protozero::test::protos::pbzero::PackedRepeatedFields;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::StartsWith;

TEST(ProtozeroToTextTest, CustomDescriptorPoolBasic) {
  using perfetto::protos::pbzero::TrackEvent;
  protozero::HeapBuffered<TrackEvent> msg{kChunkSize, kChunkSize};
  msg->set_track_uuid(4);
  msg->set_timestamp_delta_us(3);
  auto binary_proto = msg.SerializeAsArray();
  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(kTrackEventDescriptor.data(),
                                              kTrackEventDescriptor.size());
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("track_uuid: 4\ntimestamp_delta_us: 3",
            ProtozeroToText(pool, ".perfetto.protos.TrackEvent", binary_proto,
                            kIncludeNewLines));
  EXPECT_EQ("track_uuid: 4 timestamp_delta_us: 3",
            ProtozeroToText(pool, ".perfetto.protos.TrackEvent", binary_proto,
                            kSkipNewLines));
}

TEST(ProtozeroToTextTest, CustomDescriptorPoolNestedMsg) {
  using perfetto::protos::pbzero::TrackEvent;
  protozero::HeapBuffered<TrackEvent> msg{kChunkSize, kChunkSize};
  msg->set_track_uuid(4);
  auto* state = msg->set_cc_scheduler_state();
  state->set_deadline_us(7);
  auto* machine = state->set_state_machine();
  auto* minor_state = machine->set_minor_state();
  minor_state->set_commit_count(8);
  state->set_observing_begin_frame_source(true);
  msg->set_timestamp_delta_us(3);
  auto binary_proto = msg.SerializeAsArray();

  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(kTrackEventDescriptor.data(),
                                              kTrackEventDescriptor.size());
  ASSERT_TRUE(status.ok());

  EXPECT_EQ(
      R"(track_uuid: 4
cc_scheduler_state {
  deadline_us: 7
  state_machine {
    minor_state {
      commit_count: 8
    }
  }
  observing_begin_frame_source: true
}
timestamp_delta_us: 3)",
      ProtozeroToText(pool, ".perfetto.protos.TrackEvent", binary_proto,
                      kIncludeNewLines));

  EXPECT_EQ(
      "track_uuid: 4 cc_scheduler_state { deadline_us: 7 state_machine { "
      "minor_state { commit_count: 8 } } observing_begin_frame_source: true } "
      "timestamp_delta_us: 3",
      ProtozeroToText(pool, ".perfetto.protos.TrackEvent", binary_proto,
                      kSkipNewLines));
}

// Sets up a descriptor pool with all the messages from
// "src/protozero/test/example_proto/test_messages.proto"
class ProtozeroToTextTestMessageTest : public testing::Test {
 protected:
  void SetUp() override {
    auto status = pool_.AddFromFileDescriptorSet(
        kTestMessagesDescriptor.data(), kTestMessagesDescriptor.size());
    ASSERT_TRUE(status.ok());
  }

  DescriptorPool pool_;
};

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntInt32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_int32(42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_int32: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntSint32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sint32(-42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_sint32: -42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntUint32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_uint32(3000000000);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_uint32: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntInt64) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_int64(3000000000);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_int64: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntSint64) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sint64(INT64_C(-3000000000));

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_sint64: -3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntBool) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_bool(true);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_bool: true");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntSmallEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_small_enum(protozero::test::protos::pbzero::TO_BE);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "small_enum: TO_BE");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntSignedEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_signed_enum(protozero::test::protos::pbzero::NEGATIVE);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "signed_enum: NEGATIVE");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntBigEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_big_enum(protozero::test::protos::pbzero::END);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "big_enum: END");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntEnumUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendVarInt(EveryField::kSmallEnumFieldNumber, 42);
  ASSERT_EQ(EveryField::kSmallEnumFieldNumber, 51);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "51: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendVarInt(/*field_id=*/9999, /*value=*/42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "9999: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntMismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendVarInt(EveryField::kFieldStringFieldNumber, 42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "500: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldVarIntForPacked) {
  // Even though field_int32 has [packed = true], it still accepts a non-packed
  // representation.
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  msg->AppendVarInt(PackedRepeatedFields::kFieldInt32FieldNumber, 42);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_int32: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed32Signed) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sfixed32(-42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_sfixed32: -42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed32Unsigned) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_fixed32(3000000000);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_fixed32: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed32Float) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_float(24.125);

  EXPECT_THAT(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                              msg.SerializeAsArray(), kIncludeNewLines),
              StartsWith("field_float: 24.125"));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed32Unknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendFixed<uint32_t>(/*field_id=*/9999, /*value=*/0x1);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "9999: 0x00000001");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed32Mismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendFixed<uint32_t>(EveryField::kFieldStringFieldNumber, 0x1);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "500: 0x00000001");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed64Signed) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sfixed64(-42);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_sfixed64: -42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed64Unsigned) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_fixed64(3000000000);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "field_fixed64: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed64Double) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_double(24.125);

  EXPECT_THAT(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                              msg.SerializeAsArray(), kIncludeNewLines),
              StartsWith("field_double: 24.125"));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed64Unknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendFixed<uint64_t>(/*field_id=*/9999, /*value=*/0x1);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "9999: 0x0000000000000001");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldFixed64Mismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendFixed<uint64_t>(EveryField::kFieldStringFieldNumber, 0x1);

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "500: 0x0000000000000001");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedString) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string("Hello");

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            R"(field_string: "Hello")");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedBytes) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_bytes("Hello");

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            R"(field_bytes: "Hello")");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendString(9999, "Hello");

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            R"(9999: "Hello")");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedMismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldBoolFieldNumber, 13);
  msg->AppendString(EveryField::kFieldBoolFieldNumber, "Hello");

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "# Packed type 8 not supported. Printing raw string.\n"
            R"(13: "Hello")");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedForNonPacked) {
  // Even though repeated_int32 doesn't have [packed = true], it still accepts a
  // packed representation.
  protozero::HeapBuffered<EveryField> msg;
  protozero::PackedVarInt buf;
  buf.Append<int32_t>(-42);
  buf.Append<int32_t>(2147483647);
  msg->AppendBytes(EveryField::kRepeatedInt32FieldNumber, buf.data(),
                   buf.size());

  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            "repeated_int32: -42\nrepeated_int32: 2147483647");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntInt32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<int32_t>(-42);
  buf.Append<int32_t>(2147483647);
  msg->set_field_int32(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_int32: -42\nfield_int32: 2147483647");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntInt64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<int64_t>(-42);
  buf.Append<int64_t>(3000000000);
  msg->set_field_int64(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_int64: -42\nfield_int64: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntUint32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<uint32_t>(42);
  buf.Append<uint32_t>(3000000000);
  msg->set_field_uint32(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_uint32: 42\nfield_uint32: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntUint64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<uint64_t>(42);
  buf.Append<uint64_t>(3000000000000);
  msg->set_field_uint64(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_uint64: 42\nfield_uint64: 3000000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed32Uint32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<uint32_t> buf;
  buf.Append(42);
  buf.Append(3000000000);
  msg->set_field_fixed32(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_fixed32: 42\nfield_fixed32: 3000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed32Int32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<int32_t> buf;
  buf.Append(-42);
  buf.Append(42);
  msg->set_field_sfixed32(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_sfixed32: -42\nfield_sfixed32: 42");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed32Float) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<float> buf;
  buf.Append(-42);
  buf.Append(42.125);
  msg->set_field_float(buf);

  std::string output =
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines);

  EXPECT_THAT(base::SplitString(output, "\n"),
              ElementsAre(StartsWith("field_float: -42"),
                          StartsWith("field_float: 42.125")));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed64Uint64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<uint64_t> buf;
  buf.Append(42);
  buf.Append(3000000000000);
  msg->set_field_fixed64(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_fixed64: 42\nfield_fixed64: 3000000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed64Int64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<int64_t> buf;
  buf.Append(-42);
  buf.Append(3000000000000);
  msg->set_field_sfixed64(buf);

  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_sfixed64: -42\nfield_sfixed64: 3000000000000");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixed64Double) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<double> buf;
  buf.Append(-42);
  buf.Append(42.125);
  msg->set_field_double(buf);

  EXPECT_THAT(
      base::SplitString(
          ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                          msg.SerializeAsArray(), kIncludeNewLines),
          "\n"),
      ElementsAre(StartsWith("field_double: -42"),
                  StartsWith("field_double: 42.125")));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedSmallEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(1);
  buf.Append(0);
  buf.Append(-1);
  msg->set_small_enum(buf);

  EXPECT_THAT(
      base::SplitString(
          ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                          msg.SerializeAsArray(), kIncludeNewLines),
          "\n"),
      ElementsAre(Eq("small_enum: TO_BE"), Eq("small_enum: NOT_TO_BE"),
                  Eq("51: -1")));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedSignedEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(1);
  buf.Append(0);
  buf.Append(-1);
  buf.Append(-100);
  msg->set_signed_enum(buf);

  EXPECT_THAT(
      base::SplitString(
          ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                          msg.SerializeAsArray(), kIncludeNewLines),
          "\n"),
      ElementsAre(Eq("signed_enum: POSITIVE"), Eq("signed_enum: NEUTRAL"),
                  Eq("signed_enum: NEGATIVE"), Eq("52: -100")));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedBigEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(10);
  buf.Append(100500);
  buf.Append(-1);
  msg->set_big_enum(buf);

  EXPECT_THAT(
      base::SplitString(
          ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                          msg.SerializeAsArray(), kIncludeNewLines),
          "\n"),
      ElementsAre(Eq("big_enum: BEGIN"), Eq("big_enum: END"), Eq("53: -1")));
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixedErrShort) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  std::string buf;
  buf = "\x01";
  // buf does not contain enough data for a fixed 64
  msg->AppendBytes(PackedRepeatedFields::kFieldFixed64FieldNumber, buf.data(),
                   buf.size());

  // "protoc --decode", instead, returns an error on stderr and doesn't output
  // anything at all.
  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "# Packed decoding failure for field field_fixed64\n");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedFixedGarbage) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<uint64_t> buf;
  buf.Append(42);
  buf.Append(3000000000000);
  std::string buf_and_garbage(reinterpret_cast<const char*>(buf.data()),
                              buf.size());
  buf_and_garbage += "\x01";
  // buf contains extra garbage
  msg->AppendBytes(PackedRepeatedFields::kFieldFixed64FieldNumber,
                   buf_and_garbage.data(), buf_and_garbage.size());

  // "protoc --decode", instead, returns an error on stderr and doesn't output
  // anything at all.
  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "# Packed decoding failure for field field_fixed64\n");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntShort) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  std::string buf;
  buf = "\xFF";
  // for the varint to be valid, buf should contain another byte.
  msg->AppendBytes(PackedRepeatedFields::kFieldInt32FieldNumber, buf.data(),
                   buf.size());

  // "protoc --decode", instead, returns an error on stderr and doesn't output
  // anything at all.
  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "# Packed decoding failure for field field_int32\n");
}

TEST_F(ProtozeroToTextTestMessageTest, FieldLengthLimitedPackedVarIntGarbage) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(42);
  buf.Append(105);
  std::string buf_and_garbage(reinterpret_cast<const char*>(buf.data()),
                              buf.size());
  buf_and_garbage += "\xFF";
  // buf contains extra garbage
  msg->AppendBytes(PackedRepeatedFields::kFieldInt32FieldNumber,
                   buf_and_garbage.data(), buf_and_garbage.size());

  // "protoc --decode", instead:
  // * doesn't output anything.
  // * returns an error on stderr.
  EXPECT_EQ(
      ProtozeroToText(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kIncludeNewLines),
      "field_int32: 42\n"
      "field_int32: 105\n"
      "# Packed decoding failure for field field_int32\n");
}

TEST_F(ProtozeroToTextTestMessageTest, ExtraBytes) {
  protozero::HeapBuffered<EveryField> msg;
  EveryField* nested = msg->add_field_nested();
  nested->set_field_string("hello");
  std::string garbage("\377\377");
  nested->AppendRawProtoBytes(garbage.data(), garbage.size());

  // "protoc --decode", instead:
  // * doesn't output anything.
  // * returns an error on stderr.
  EXPECT_EQ(ProtozeroToText(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kIncludeNewLines),
            R"(field_nested {
  field_string: "hello"
  # Extra bytes: "\377\377"

})");
}

TEST_F(ProtozeroToTextTestMessageTest, NonExistingType) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string("hello");
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);

  // "protoc --decode", instead:
  // * doesn't output anything.
  // * returns an error on stderr.
  EXPECT_EQ(ProtozeroToText(pool_, ".non.existing.type", msg.SerializeAsArray(),
                            kIncludeNewLines),
            R"(500: "hello")");
}

}  // namespace
}  // namespace perfetto::trace_processor::protozero_to_text
