// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/json/json_reader.h"
#include "base/optional.h"
#include "base/strings/string_piece_forward.h"
#include "base/time/time.h"
#include "net/base/backoff_entry.h"
#include "net/base/backoff_entry_serializer.h"
#include "net/base/backoff_entry_serializer_fuzzer_input.pb.h"
#include "testing/libfuzzer/proto/json_proto_converter.h"
#include "testing/libfuzzer/proto/lpm_interface.h"

namespace net {

namespace {
struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOG_ERROR); }
};

class ProtoTranslator {
 public:
  explicit ProtoTranslator(const fuzz_proto::FuzzerInput& input)
      : input_(input) {}

  BackoffEntry::Policy policy() const {
    return PolicyFromProto(input_.policy());
  }
  base::Time parse_time() const { return TimeFromProto(input_.parse_time()); }
  base::Time serialize_time() const {
    return TimeFromProto(input_.serialize_time());
  }

  base::Optional<base::Value> serialized_entry() const {
    json_proto::JsonProtoConverter converter;
    std::string json_array = converter.Convert(input_.serialized_entry());
    base::Optional<base::Value> value = base::JSONReader::Read(json_array);
    return value;
  }

 private:
  const fuzz_proto::FuzzerInput& input_;

  static BackoffEntry::Policy PolicyFromProto(
      const fuzz_proto::BackoffEntryPolicy& policy) {
    return BackoffEntry::Policy{
        .num_errors_to_ignore = policy.num_errors_to_ignore(),
        .initial_delay_ms = policy.initial_delay_ms(),
        .multiply_factor = policy.multiply_factor(),
        .jitter_factor = policy.jitter_factor(),
        .maximum_backoff_ms = policy.maximum_backoff_ms(),
        .entry_lifetime_ms = policy.entry_lifetime_ms(),
        .always_use_initial_delay = policy.always_use_initial_delay(),
    };
  }

  static base::Time TimeFromProto(uint64_t raw_time) {
    return base::Time() + base::TimeDelta::FromMicroseconds(raw_time);
  }
};

// Tests the "deserialize-reserialize" property. Deserializes a BackoffEntry
// from JSON, reserializes it, and checks that the JSON values match.
void TestDeserialize(const ProtoTranslator& translator) {
  // Attempt to convert the json_proto.ArrayValue to a base::Value.
  base::Optional<base::Value> value = translator.serialized_entry();
  if (!value)
    return;
  DCHECK(value->is_list());

  BackoffEntry::Policy policy = translator.policy();

  // Attempt to deserialize a BackoffEntry.
  std::unique_ptr<BackoffEntry> entry =
      BackoffEntrySerializer::DeserializeFromValue(*value, &policy, nullptr,
                                                   translator.parse_time());
  if (!entry)
    return;

  // Serializing |entry| it should recreate the original JSON input!
  std::unique_ptr<base::Value> reserialized =
      BackoffEntrySerializer::SerializeToValue(*entry,
                                               translator.serialize_time());
  CHECK(reserialized);
  CHECK_EQ(*reserialized, *value);
}

// Tests the "serialize-deserialize" property. Serializes an arbitrary
// BackoffEntry to JSON, deserializes to another BackoffEntry, and checks
// equality of the two entries. Our notion of equality is *very weak* and needs
// improvement.
void TestSerialize(const ProtoTranslator& translator) {
  BackoffEntry::Policy policy = translator.policy();

  // Serialize the BackoffEntry.
  BackoffEntry native_entry(&policy);
  std::unique_ptr<base::Value> serialized =
      BackoffEntrySerializer::SerializeToValue(native_entry,
                                               translator.serialize_time());
  CHECK(serialized);

  // Deserialize it.
  std::unique_ptr<BackoffEntry> deserialized_entry =
      BackoffEntrySerializer::DeserializeFromValue(
          *serialized, &policy, nullptr, translator.parse_time());
  // Even though SerializeToValue was successful, we're not guaranteed to have a
  // |deserialized_entry|. One reason deserialization may fail is if the parsed
  // |absolute_release_time_us| is below zero.
  if (!deserialized_entry)
    return;

  // TODO(dmcardle) Develop a stronger equality check for BackoffEntry.

  // Note that while |BackoffEntry::GetReleaseTime| looks like an accessor, it
  // returns a |value that is computed based on a random double, so it's not
  // suitable for CHECK_EQ here. See |BackoffEntry::CalculateReleaseTime|.

  CHECK_EQ(native_entry.failure_count(), deserialized_entry->failure_count());
}
}  // namespace

DEFINE_PROTO_FUZZER(const fuzz_proto::FuzzerInput& input) {
  static Environment env;

  // Print the entire |input| protobuf if asked.
  if (getenv("LPM_DUMP_NATIVE_INPUT")) {
    std::cout << "input: " << input.DebugString();
  }

  ProtoTranslator translator(input);
  TestSerialize(translator);
  TestDeserialize(translator);
}

}  // namespace net
