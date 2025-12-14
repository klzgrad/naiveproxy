/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/util/protozero_to_json.h"

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/protozero/test/example_proto/test_messages.pbzero.h"
#include "src/trace_processor/importers/proto/track_event.descriptor.h"
#include "src/trace_processor/test_messages.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/track_event/chrome_compositor_scheduler_state.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_STANDALONE_BUILD)
#include "protos/perfetto/metrics/chrome/all_chrome_metrics.pb.h"  // nogncheck
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"  // nogncheck
#endif

namespace perfetto {
namespace trace_processor {
namespace protozero_to_json {

namespace {

constexpr size_t kChunkSize = 42;

using ::protozero::test::protos::pbzero::EveryField;
using ::protozero::test::protos::pbzero::PackedRepeatedFields;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::StartsWith;

TEST(ProtozeroToJsonTest, CustomDescriptorPoolEmpty) {
  using perfetto::protos::pbzero::TrackEvent;
  protozero::HeapBuffered<TrackEvent> msg{kChunkSize, kChunkSize};
  auto binary_proto = msg.SerializeAsArray();
  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(kTrackEventDescriptor.data(),
                                              kTrackEventDescriptor.size());
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{}", ProtozeroToJson(pool, ".perfetto.protos.TrackEvent",
                                  binary_proto, kPretty));
  EXPECT_EQ("{}", ProtozeroToJson(pool, ".perfetto.protos.TrackEvent",
                                  binary_proto, kNone));
}

TEST(ProtozeroToJsonTest, CustomDescriptorPoolBasic) {
  using perfetto::protos::pbzero::TrackEvent;
  protozero::HeapBuffered<TrackEvent> msg{kChunkSize, kChunkSize};
  msg->set_track_uuid(4);
  msg->set_timestamp_delta_us(3);
  auto binary_proto = msg.SerializeAsArray();
  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(kTrackEventDescriptor.data(),
                                              kTrackEventDescriptor.size());
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(R"({
  "track_uuid": 4,
  "timestamp_delta_us": 3
})",
            ProtozeroToJson(pool, ".perfetto.protos.TrackEvent", binary_proto,
                            kPretty));
  EXPECT_EQ(R"({"track_uuid":4,"timestamp_delta_us":3})",
            ProtozeroToJson(pool, ".perfetto.protos.TrackEvent", binary_proto,
                            kNone));
}

TEST(ProtozeroToJsonTest, CustomDescriptorPoolNestedMsg) {
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

  EXPECT_EQ(R"({
  "track_uuid": 4,
  "cc_scheduler_state": {
    "deadline_us": 7,
    "state_machine": {
      "minor_state": {
        "commit_count": 8
      }
    },
    "observing_begin_frame_source": true
  },
  "timestamp_delta_us": 3
})",
            ProtozeroToJson(pool, ".perfetto.protos.TrackEvent", binary_proto,
                            kPretty));

  EXPECT_EQ(
      R"({"track_uuid":4,"cc_scheduler_state":{"deadline_us":7,"state_machine":{"minor_state":{"commit_count":8}},"observing_begin_frame_source":true},"timestamp_delta_us":3})",
      ProtozeroToJson(pool, ".perfetto.protos.TrackEvent", binary_proto,
                      kNone));
}

// This test depends on the CustomOptions message in descriptor.proto which
// is very tricky to point to on the non-standalone build.
#if PERFETTO_BUILDFLAG(PERFETTO_STANDALONE_BUILD)
TEST(ProtozeroToJsonTest, CustomDescriptorPoolAnnotations) {
  using perfetto::protos::TestChromeMetric;
  TestChromeMetric msg;
  msg.set_test_value(1);
  auto binary_proto = msg.SerializeAsString();
  protozero::ConstBytes binary_proto_bytes{
      reinterpret_cast<const uint8_t*>(binary_proto.data()),
      binary_proto.size()};

  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(
      kAllChromeMetricsDescriptor.data(), kAllChromeMetricsDescriptor.size());
  ASSERT_TRUE(status.ok());

  EXPECT_EQ(R"({
  "test_value": 1,
  "__annotations": {
    "test_value": {
      "__field_options": {
        "unit": "count_smallerIsBetter"
      }
    }
  }
})",
            ProtozeroToJson(pool, ".perfetto.protos.TestChromeMetric",
                            binary_proto_bytes, kPretty | kInlineAnnotations));
}
#endif

// Sets up a descriptor pool with all the messages from
// "src/protozero/test/example_proto/test_messages.proto"
class ProtozeroToJsonTestMessageTest : public testing::Test {
 protected:
  void SetUp() override {
    auto status = pool_.AddFromFileDescriptorSet(
        kTestMessagesDescriptor.data(), kTestMessagesDescriptor.size());
    ASSERT_TRUE(status.ok());
  }

  DescriptorPool pool_;
};

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntInt32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_int32(42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_int32":42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntSint32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sint32(-42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_sint32":-42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntUint32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_uint32(3000000000);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_uint32":3000000000})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntInt64) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_int64(3000000000);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_int64":3000000000})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntSint64) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sint64(INT64_C(-3000000000));

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_sint64":-3000000000})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntBool) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_bool(true);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_bool":true})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntSmallEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_small_enum(protozero::test::protos::pbzero::TO_BE);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"small_enum":"TO_BE"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntSignedEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_signed_enum(protozero::test::protos::pbzero::NEGATIVE);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"signed_enum":"NEGATIVE"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntBigEnum) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_big_enum(protozero::test::protos::pbzero::END);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"big_enum":"END"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntEnumUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendVarInt(EveryField::kSmallEnumFieldNumber, 42);
  ASSERT_EQ(EveryField::kSmallEnumFieldNumber, 51);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"small_enum":42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendVarInt(/*field_id=*/9999, /*value=*/42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"9999":42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntMismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendVarInt(EveryField::kFieldStringFieldNumber, 42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"500":42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldVarIntForPacked) {
  // Even though field_int32 has [packed = true], it still accepts a non-packed
  // representation.
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  msg->AppendVarInt(PackedRepeatedFields::kFieldInt32FieldNumber, 42);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_int32":[42]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed32Signed) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sfixed32(-42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_sfixed32":-42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed32Unsigned) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_fixed32(3000000000);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_fixed32":3000000000})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed32Float) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_float(24.125);

  EXPECT_THAT(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                              msg.SerializeAsArray(), kNone),
              StartsWith(R"({"field_float":24.125)"));
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed32Unknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendFixed<uint32_t>(/*field_id=*/9999, /*value=*/0x1);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"9999":1})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed32Mismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendFixed<uint32_t>(EveryField::kFieldStringFieldNumber, 0x1);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"500":1})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed64Signed) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_sfixed64(-42);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_sfixed64":-42})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed64Unsigned) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_fixed64(3000000000);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_fixed64":3000000000})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed64Double) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_double(24.125);

  EXPECT_THAT(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                              msg.SerializeAsArray(), kNone),
              StartsWith(R"({"field_double":24.125)"));
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed64Unknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendFixed<uint64_t>(/*field_id=*/9999, /*value=*/0x1);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"9999":1})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldFixed64Mismatch) {
  protozero::HeapBuffered<EveryField> msg;
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);
  msg->AppendFixed<uint64_t>(EveryField::kFieldStringFieldNumber, 0x1);

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"500":1})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedString) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string("Hello");

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_string":"Hello"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedBadString) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string(R"("\
)");
  std::string res = R"({"field_string":"\"\\\n"})";
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            res);
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedUnicodeString) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string(R"($£Иह€한𐍈)");
  std::string res =
      R"({"field_string":"$\u00a3\u0418\u0939\u20ac\ud55c\ud800\udf48"})";
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            res);
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedBytes) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_bytes("Hello");

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_bytes":"Hello"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedUnknown) {
  protozero::HeapBuffered<EveryField> msg;
  msg->AppendString(9999, "Hello");

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"9999":"Hello"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, RepeatedInt32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->add_repeated_int32(-42);
  msg->add_repeated_int32(2147483647);
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"repeated_int32":[-42,2147483647]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, RepeatedFixed64) {
  protozero::HeapBuffered<EveryField> msg;
  msg->add_repeated_fixed64(42);
  msg->add_repeated_fixed64(2147483647);
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"repeated_fixed64":[42,2147483647]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, RepeatedSignedFixed32) {
  protozero::HeapBuffered<EveryField> msg;
  msg->add_repeated_sfixed32(-42);
  msg->add_repeated_sfixed32(2147483647);
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"repeated_sfixed32":[-42,2147483647]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, RepeatedString) {
  protozero::HeapBuffered<EveryField> msg;
  msg->add_repeated_string("Hello");
  msg->add_repeated_string("world!");
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"repeated_string":["Hello","world!"]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedBool) {
  protozero::HeapBuffered<EveryField> msg;
  protozero::PackedVarInt buf;
  buf.Append<int32_t>(1);
  buf.Append<int32_t>(0);
  buf.Append<int32_t>(1);
  msg->AppendBytes(EveryField::kFieldBoolFieldNumber, buf.data(), buf.size());
  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"field_bool":[true,false,true]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedForNonPacked) {
  // Even though repeated_int32 doesn't have [packed = true], it still accepts a
  // packed representation.
  protozero::HeapBuffered<EveryField> msg;
  protozero::PackedVarInt buf;
  buf.Append<int32_t>(-42);
  buf.Append<int32_t>(2147483647);
  msg->AppendBytes(EveryField::kRepeatedInt32FieldNumber, buf.data(),
                   buf.size());

  EXPECT_EQ(ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                            msg.SerializeAsArray(), kNone),
            R"({"repeated_int32":[-42,2147483647]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntInt32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<int32_t>(-42);
  buf.Append<int32_t>(2147483647);
  msg->set_field_int32(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_int32":[-42,2147483647]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntInt64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<int64_t>(-42);
  buf.Append<int64_t>(3000000000);
  msg->set_field_int64(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_int64":[-42,3000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntUint32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<uint32_t>(42);
  buf.Append<uint32_t>(3000000000);
  msg->set_field_uint32(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_uint32":[42,3000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntUint64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append<uint64_t>(42);
  buf.Append<uint64_t>(3000000000000);
  msg->set_field_uint64(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_uint64":[42,3000000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed32Uint32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<uint32_t> buf;
  buf.Append(42);
  buf.Append(3000000000);
  msg->set_field_fixed32(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_fixed32":[42,3000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed32Int32) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<int32_t> buf;
  buf.Append(-42);
  buf.Append(42);
  msg->set_field_sfixed32(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_sfixed32":[-42,42]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed32Float) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<float> buf;
  buf.Append(-42);
  buf.Append(42.125);
  msg->set_field_float(buf);

  std::string output =
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kPretty);

  EXPECT_THAT(base::SplitString(output, "\n"),
              ElementsAre("{", R"(  "field_float": [)", StartsWith("    -42"),
                          StartsWith("    42.125"), R"(  ])", R"(})"));
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed64Uint64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<uint64_t> buf;
  buf.Append(42);
  buf.Append(3000000000000);
  msg->set_field_fixed64(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_fixed64":[42,3000000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed64Int64) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<int64_t> buf;
  buf.Append(-42);
  buf.Append(3000000000000);
  msg->set_field_sfixed64(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"field_sfixed64":[-42,3000000000000]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixed64Double) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedFixedSizeInt<double> buf;
  buf.Append(-42);
  buf.Append(42.125);
  msg->set_field_double(buf);

  std::string output =
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kPretty);
  EXPECT_THAT(base::SplitString(output, "\n"),
              ElementsAre("{", R"(  "field_double": [)", StartsWith("    -42"),
                          StartsWith("    42.125"), R"(  ])", R"(})"));
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedSmallEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(1);
  buf.Append(0);
  buf.Append(-1);
  msg->set_small_enum(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"small_enum":["TO_BE","NOT_TO_BE",-1]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedSignedEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(1);
  buf.Append(0);
  buf.Append(-1);
  buf.Append(-100);
  msg->set_signed_enum(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"signed_enum":["POSITIVE","NEUTRAL","NEGATIVE",-100]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedBigEnum) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  protozero::PackedVarInt buf;
  buf.Append(10);
  buf.Append(100500);
  buf.Append(-1);
  msg->set_big_enum(buf);

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kNone),
      R"({"big_enum":["BEGIN","END",-1]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixedErrShort) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  std::string buf;
  buf = "\x01";
  // buf does not contain enough data for a fixed 64
  msg->AppendBytes(PackedRepeatedFields::kFieldFixed64FieldNumber, buf.data(),
                   buf.size());

  // "protoc --decode", instead, returns an error on stderr and doesn't output
  // anything at all.
  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kPretty | kInlineErrors),
      R"({
  "field_fixed64": [],
  "__error": "Decoding failure for field 'field_fixed64'"
})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedFixedGarbage) {
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
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kInlineErrors),
      R"({"field_fixed64":[],"__error":"Decoding failure for field 'field_fixed64'"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntShort) {
  protozero::HeapBuffered<PackedRepeatedFields> msg;
  std::string buf;
  buf = "\xFF";
  // for the varint to be valid, buf should contain another byte.
  msg->AppendBytes(PackedRepeatedFields::kFieldInt32FieldNumber, buf.data(),
                   buf.size());

  // "protoc --decode", instead, returns an error on stderr and doesn't output
  // anything at all.
  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kInlineErrors),
      R"({"field_int32":[],"__error":"Decoding failure for field 'field_int32'"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, FieldLengthLimitedPackedVarIntGarbage) {
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
      ProtozeroToJson(pool_, ".protozero.test.protos.PackedRepeatedFields",
                      msg.SerializeAsArray(), kInlineErrors),
      R"({"field_int32":[42,105],"__error":"Decoding failure for field 'field_int32'"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, NestedBackToBack) {
  protozero::HeapBuffered<EveryField> msg;
  EveryField* nestedA = msg->add_field_nested();
  nestedA->set_field_string("Hello, ");
  EveryField* nestedB = msg->add_field_nested();
  nestedB->set_field_string("world!");

  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                      msg.SerializeAsArray(), kInlineErrors),
      R"({"field_nested":[{"field_string":"Hello, "},{"field_string":"world!"}]})");
}

TEST_F(ProtozeroToJsonTestMessageTest, ExtraBytes) {
  protozero::HeapBuffered<EveryField> msg;
  EveryField* nested = msg->add_field_nested();
  nested->set_field_string("hello");
  std::string garbage("\377\377");
  nested->AppendRawProtoBytes(garbage.data(), garbage.size());

  // "protoc --decode", instead:
  // * doesn't output anything.
  // * returns an error on stderr.
  EXPECT_EQ(
      ProtozeroToJson(pool_, ".protozero.test.protos.EveryField",
                      msg.SerializeAsArray(), kInlineErrors),
      R"({"field_nested":[{"field_string":"hello"}],"__error":"2 extra bytes"})");
}

TEST_F(ProtozeroToJsonTestMessageTest, NonExistingType) {
  protozero::HeapBuffered<EveryField> msg;
  msg->set_field_string("hello");
  ASSERT_EQ(EveryField::kFieldStringFieldNumber, 500);

  // "protoc --decode", instead:
  // * doesn't output anything.
  // * returns an error on stderr.
  EXPECT_EQ(ProtozeroToJson(pool_, ".non.existing.type", msg.SerializeAsArray(),
                            kNone),
            R"({"500":"hello"})");
}

}  // namespace
}  // namespace protozero_to_json
}  // namespace trace_processor
}  // namespace perfetto
