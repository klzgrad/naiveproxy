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

#include <algorithm>
#include <vector>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_processor/importers/proto/trace.descriptor.h"
#include "src/trace_processor/util/proto_profiler.h"

#include "protos/third_party/pprof/profile.pbzero.h"

namespace perfetto {
namespace protoprofile {
namespace {

class PprofProfileComputer {
 public:
  std::string Compute(const uint8_t* ptr,
                      size_t size,
                      const std::string& message_type,
                      trace_processor::DescriptorPool* pool);

 private:
  int InternString(const std::string& str);
  int InternLocation(const std::string& str);

  // Interned strings:
  std::vector<std::string> strings_;
  base::FlatHashMap<std::string, int> string_to_id_;

  // Interned 'locations', each location is a single frame of the stack.
  base::FlatHashMap<std::string, int> locations_;
};

int PprofProfileComputer::InternString(const std::string& s) {
  auto val = string_to_id_.Find(s);
  if (val) {
    return *val;
  }
  strings_.push_back(s);
  int id = static_cast<int>(strings_.size() - 1);
  string_to_id_[s] = id;
  return id;
}

int PprofProfileComputer::InternLocation(const std::string& s) {
  auto val = locations_.Find(s);
  if (val) {
    return *val;
  }
  int id = static_cast<int>(locations_.size()) + 1;
  locations_[s] = id;
  return id;
}

std::string PprofProfileComputer::Compute(
    const uint8_t* ptr,
    size_t size,
    const std::string& message_type,
    trace_processor::DescriptorPool* pool) {
  PERFETTO_CHECK(InternString("") == 0);

  trace_processor::util::SizeProfileComputer computer(pool, message_type);
  computer.Reset(ptr, size);

  using PathToSamplesMap = std::unordered_map<
      trace_processor::util::SizeProfileComputer::FieldPath,
      std::vector<size_t>,
      trace_processor::util::SizeProfileComputer::FieldPathHasher>;
  PathToSamplesMap field_path_to_samples;
  for (auto sample = computer.GetNext(); sample; sample = computer.GetNext()) {
    field_path_to_samples[computer.GetPath()].push_back(*sample);
  }

  protozero::HeapBuffered<third_party::perftools::profiles::pbzero::Profile>
      profile;

  auto* sample_type = profile->add_sample_type();
  sample_type->set_type(InternString("protos"));
  sample_type->set_unit(InternString("count"));

  sample_type = profile->add_sample_type();
  sample_type->set_type(InternString("max_size"));
  sample_type->set_unit(InternString("bytes"));

  sample_type = profile->add_sample_type();
  sample_type->set_type(InternString("min_size"));
  sample_type->set_unit(InternString("bytes"));

  sample_type = profile->add_sample_type();
  sample_type->set_type(InternString("median"));
  sample_type->set_unit(InternString("bytes"));

  sample_type = profile->add_sample_type();
  sample_type->set_type(InternString("total_size"));
  sample_type->set_unit(InternString("bytes"));

  // For each unique field path we've seen write out the stats:
  for (auto& entry : field_path_to_samples) {
    std::vector<std::string> field_path;
    for (const auto& field : entry.first) {
      if (field.has_field_name())
        field_path.push_back(field.field_name());
      field_path.push_back(field.type_name());
    }
    std::vector<size_t>& samples = entry.second;

    protozero::PackedVarInt location_ids;
    auto* sample = profile->add_sample();
    for (auto loc_it = field_path.rbegin(); loc_it != field_path.rend();
         ++loc_it) {
      location_ids.Append(InternLocation(*loc_it));
    }
    sample->set_location_id(location_ids);

    std::sort(samples.begin(), samples.end());
    size_t count = samples.size();
    size_t total_size = 0;
    size_t max_size = samples[count - 1];
    size_t min_size = samples[0];
    size_t median_size = samples[count / 2];
    for (size_t i = 0; i < count; ++i)
      total_size += samples[i];
    // These have to be in the same order as the sample types above:
    protozero::PackedVarInt values;
    values.Append(static_cast<int64_t>(count));
    values.Append(static_cast<int64_t>(max_size));
    values.Append(static_cast<int64_t>(min_size));
    values.Append(static_cast<int64_t>(median_size));
    values.Append(static_cast<int64_t>(total_size));
    sample->set_value(values);
  }

  // The proto profile has a two step mapping where samples are associated with
  // locations which in turn are associated to functions. We don't currently
  // distinguish them so we make a 1:1 mapping between the locations and the
  // functions:
  for (auto it = locations_.GetIterator(); it; ++it) {
    auto* location = profile->add_location();
    location->set_id(static_cast<uint64_t>(it.value()));

    auto* line = location->add_line();
    line->set_function_id(static_cast<uint64_t>(it.value()));

    auto* function = profile->add_function();
    function->set_id(static_cast<uint64_t>(it.value()));
    function->set_name(InternString(it.key()));
  }
  // Finally the string table. We intern more strings above, so this has to be
  // last.
  for (size_t i = 0; i < strings_.size(); i++) {
    profile->add_string_table(strings_[i]);
  }
  return profile.SerializeAsString();
}

int PrintUsage(int, const char** argv) {
  fprintf(stderr, "Usage: %s INPUT_PATH OUTPUT_PATH\n", argv[0]);
  return 1;
}

int Main(int argc, const char** argv) {
  if (argc != 3)
    return PrintUsage(argc, argv);

  const char* input_path = argv[1];
  const char* output_path = argv[2];

  base::ScopedFile proto_fd = base::OpenFile(input_path, O_RDONLY);
  if (!proto_fd) {
    PERFETTO_ELOG("Could not open input path (%s)", input_path);
    return 1;
  }

  std::string s;
  base::ReadFileDescriptor(proto_fd.get(), &s);

  trace_processor::DescriptorPool pool;
  base::Status status = pool.AddFromFileDescriptorSet(kTraceDescriptor.data(),
                                                      kTraceDescriptor.size());
  if (!status.ok()) {
    PERFETTO_ELOG("Could not add Trace proto descriptor: %s",
                  status.c_message());
    return 1;
  }

  const uint8_t* start = reinterpret_cast<const uint8_t*>(s.data());
  size_t size = s.size();

  base::ScopedFile output_fd =
      base::OpenFile(output_path, O_WRONLY | O_TRUNC | O_CREAT, 0600);
  if (!output_fd) {
    PERFETTO_ELOG("Could not open output path (%s)", output_path);
    return 1;
  }
  PprofProfileComputer computer;
  std::string out =
      computer.Compute(start, size, ".perfetto.protos.Trace", &pool);
  base::WriteAll(output_fd.get(), out.data(), out.size());
  base::FlushFile(output_fd.get());

  return 0;
}

}  // namespace
}  // namespace protoprofile
}  // namespace perfetto

int main(int argc, const char** argv) {
  return perfetto::protoprofile::Main(argc, argv);
}
