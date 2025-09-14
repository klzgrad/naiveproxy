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

#include "src/profiling/symbolizer/symbolize_database.h"

#include <map>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/trace_processor.h"

#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto {
namespace profiling {

namespace {
using trace_processor::Iterator;

constexpr const char* kQueryUnsymbolized =
    "select spm.name, spm.build_id, spf.rel_pc, spm.load_bias "
    "from stack_profile_frame spf "
    "join stack_profile_mapping spm "
    "on spf.mapping = spm.id "
    "where spm.build_id != '' and spf.symbol_set_id IS NULL";

using NameAndBuildIdPair = std::pair<std::string, std::string>;

struct UnsymbolizedMapping {
  std::string name;
  std::string build_id;
  uint64_t load_bias;
  bool operator<(const UnsymbolizedMapping& o) const {
    return std::tie(name, build_id, load_bias) <
           std::tie(o.name, o.build_id, o.load_bias);
  }
};

std::map<UnsymbolizedMapping, std::vector<uint64_t>> GetUnsymbolizedFrames(
    trace_processor::TraceProcessor* tp) {
  std::map<UnsymbolizedMapping, std::vector<uint64_t>> res;
  Iterator it = tp->ExecuteQuery(kQueryUnsymbolized);
  while (it.Next()) {
    int64_t load_bias = it.Get(3).AsLong();
    PERFETTO_CHECK(load_bias >= 0);
    trace_processor::BuildId build_id =
        trace_processor::BuildId::FromHex(it.Get(1).AsString());
    UnsymbolizedMapping unsymbolized_mapping{
        it.Get(0).AsString(), build_id.raw(), static_cast<uint64_t>(load_bias)};
    int64_t rel_pc = it.Get(2).AsLong();
    res[unsymbolized_mapping].emplace_back(rel_pc);
  }
  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return {};
  }
  return res;
}
}  // namespace

void SymbolizeDatabase(trace_processor::TraceProcessor* tp,
                       Symbolizer* symbolizer,
                       std::function<void(const std::string&)> callback) {
  PERFETTO_CHECK(symbolizer);
  auto unsymbolized = GetUnsymbolizedFrames(tp);
  for (auto it = unsymbolized.cbegin(); it != unsymbolized.cend(); ++it) {
    const auto& unsymbolized_mapping = it->first;
    const std::vector<uint64_t>& rel_pcs = it->second;
    auto res = symbolizer->Symbolize(unsymbolized_mapping.name,
                                     unsymbolized_mapping.build_id,
                                     unsymbolized_mapping.load_bias, rel_pcs);
    if (res.empty())
      continue;

    protozero::HeapBuffered<perfetto::protos::pbzero::Trace> trace;
    auto* packet = trace->add_packet();
    auto* module_symbols = packet->set_module_symbols();
    module_symbols->set_path(unsymbolized_mapping.name);
    module_symbols->set_build_id(unsymbolized_mapping.build_id);
    PERFETTO_DCHECK(res.size() == rel_pcs.size());
    for (size_t i = 0; i < res.size(); ++i) {
      auto* address_symbols = module_symbols->add_address_symbols();
      address_symbols->set_address(rel_pcs[i]);
      for (const SymbolizedFrame& frame : res[i]) {
        auto* line = address_symbols->add_lines();
        line->set_function_name(frame.function_name);
        line->set_source_file_name(frame.file_name);
        line->set_line_number(frame.line);
      }
    }
    callback(trace.SerializeAsString());
  }
}

std::vector<std::string> GetPerfettoBinaryPath() {
  const char* root = getenv("PERFETTO_BINARY_PATH");
  if (root != nullptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    const char* delimiter = ";";
#else
    const char* delimiter = ":";
#endif
    return base::SplitString(root, delimiter);
  }
  return {};
}

}  // namespace profiling
}  // namespace perfetto
