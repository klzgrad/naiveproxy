/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/util/proto_to_args_parser.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/util/descriptors.h"

#include "src/protozero/test/example_proto/test_messages.pbzero.h"
#include "src/trace_processor/test_messages.descriptor.h"

namespace perfetto::trace_processor::util {
namespace {

using ::protozero::test::protos::pbzero::EveryField;

// A Delegate that mirrors the cost ArgsParser pays on the proto hot path: it
// receives already-interned key ids (the parser owns key interning + its memo)
// and folds them, plus the value, into a checksum so the work is not optimized
// away. The args are not materialized into a table: this isolates the proto
// reflection + key-handling cost that proto-key memoization targets.
class BenchmarkDelegate : public ProtoToArgsParser::Delegate {
 public:
  explicit BenchmarkDelegate(StringPool* pool) : pool_(pool) {}

  Id InternString(base::StringView s) override {
    return pool_->InternString(s);
  }
  void AddInteger(Id fk, Id k, int64_t value) override {
    Sink(fk, k, static_cast<uint64_t>(value));
  }
  void AddUnsignedInteger(Id fk, Id k, uint64_t value) override {
    Sink(fk, k, value);
  }
  void AddString(Id fk, Id k, const protozero::ConstChars& value) override {
    Sink(fk, k, value.size);
  }
  void AddString(Id fk, Id k, const std::string& value) override {
    Sink(fk, k, value.size());
  }
  void AddDouble(Id fk, Id k, double value) override {
    Sink(fk, k, static_cast<uint64_t>(value));
  }
  void AddPointer(Id fk, Id k, uint64_t value) override { Sink(fk, k, value); }
  void AddBoolean(Id fk, Id k, bool value) override {
    Sink(fk, k, value ? 1u : 0u);
  }
  bool AddJson(Id fk, Id k, const protozero::ConstChars& value) override {
    Sink(fk, k, value.size);
    return true;
  }
  void AddNull(Id fk, Id k) override { Sink(fk, k, 0); }

  size_t GetArrayEntryIndex(const std::string& array_key) override {
    return array_indexes_[pool_->InternString(base::StringView(array_key))];
  }
  size_t IncrementArrayEntryIndex(const std::string& array_key) override {
    return ++array_indexes_[pool_->InternString(base::StringView(array_key))];
  }

  PacketSequenceStateGeneration* seq_state() override { return nullptr; }

  uint64_t sink() const { return sink_; }

 protected:
  InternedMessageView* GetInternedMessageView(uint32_t, uint64_t) override {
    return nullptr;
  }

 private:
  void Sink(Id fk, Id k, uint64_t value) {
    sink_ += fk.raw_id() + k.raw_id() + value;
  }

  StringPool* pool_;
  std::unordered_map<StringPool::Id, size_t> array_indexes_;
  uint64_t sink_ = 0;
};

// Fills one EveryField message with every scalar type, plus `nested` recursive
// children and `repeated` packed/unpacked entries, so the generated arg set
// exercises scalar leaves, nested-message key prefixes and array-index keys.
void FillEveryField(EveryField* msg, int nested, int repeated) {
  msg->set_field_int32(-1);
  msg->set_field_int64(-2);
  msg->set_field_uint32(3);
  msg->set_field_uint64(4);
  msg->set_field_sint32(-5);
  msg->set_field_sint64(-6);
  msg->set_field_fixed32(7);
  msg->set_field_fixed64(8);
  msg->set_field_sfixed32(-9);
  msg->set_field_sfixed64(-10);
  msg->set_field_float(11.5f);
  msg->set_field_double(12.5);
  msg->set_field_bool(true);
  msg->set_field_string("the quick brown fox");
  for (int i = 0; i < repeated; ++i) {
    msg->add_repeated_int32(i);
    msg->add_repeated_string("repeated_value");
  }
  for (int i = 0; i < nested; ++i) {
    FillEveryField(msg->add_field_nested(), nested - 1, repeated);
  }
}

std::string BuildMessage(int nested, int repeated) {
  protozero::HeapBuffered<EveryField> msg;
  FillEveryField(msg.get(), nested, repeated);
  return msg.SerializeAsString();
}

void BM_ProtoToArgsParser(benchmark::State& state) {
  DescriptorPool pool;
  auto status = pool.AddFromFileDescriptorSet(kTestMessagesDescriptor.data(),
                                              kTestMessagesDescriptor.size());
  PERFETTO_CHECK(status.ok());

  std::string bytes = BuildMessage(static_cast<int>(state.range(0)),
                                   static_cast<int>(state.range(1)));
  protozero::ConstBytes cb{reinterpret_cast<const uint8_t*>(bytes.data()),
                           bytes.size()};

  // One parser + one delegate reused across iterations: this is the production
  // steady state, where the same message type is parsed once per packet and
  // any per-field key memoization has already warmed up.
  StringPool string_pool;
  ProtoToArgsParser parser(pool, string_pool);
  BenchmarkDelegate delegate(&string_pool);
  for (auto _ : state) {
    auto s = parser.ParseMessage(cb, ".protozero.test.protos.EveryField",
                                 nullptr, delegate);
    benchmark::DoNotOptimize(s);
  }
  benchmark::DoNotOptimize(delegate.sink());
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_ProtoToArgsParser)
    ->Args({0, 0})   // flat scalars only
    ->Args({0, 16})  // scalars + repeated (array keys)
    ->Args({3, 4})   // nested recursion + arrays (track-event-like)
    ->Args({5, 8});  // deep + wide

}  // namespace
}  // namespace perfetto::trace_processor::util
