// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/plugins/storage_tables/storage_tables.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/counter_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/etm_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/flow_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/jit_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/log_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/memory_tables_py.h"   // IWYU pragma: keep
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/perf_tables_py.h"      // IWYU pragma: keep
#include "src/trace_processor/tables/profiler_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/sched_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/slice_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/state_tables_py.h"     // IWYU pragma: keep
#include "src/trace_processor/tables/trace_proto_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/tables/v8_tables_py.h"        // IWYU pragma: keep
#include "src/trace_processor/tables/winscope_tables_py.h"  // IWYU pragma: keep
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::storage_tables {
namespace {

template <typename T>
void AddDataframe(std::vector<PluginDataframe>& out,
                  T* table,
                  std::vector<std::vector<std::string>> indexes = {}) {
  out.push_back({&table->dataframe(), T::Name(), std::move(indexes)});
}

void InsertIntoBuildFlagsTable(tables::BuildFlagsTable* table,
                               StringPool* string_pool) {
  for (int i = 0; i < kPerfettoBuildFlagsCount; ++i) {
    const auto& build_flag = kPerfettoBuildFlags[i];
    tables::BuildFlagsTable::Row row;
    row.name = string_pool->InternString(build_flag.name);
    row.enabled = static_cast<uint32_t>(build_flag.value);
    table->Insert(row);
  }
}

void InsertIntoModulesTable(tables::ModulesTable* table,
                            StringPool* string_pool) {
  base::ignore_result(table, string_pool);

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  table->Insert({string_pool->InternString("etm")});
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  table->Insert({string_pool->InternString("winscope")});
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_LLVM_SYMBOLIZER)
  table->Insert({string_pool->InternString("llvm_symbolizer")});
#endif
}

class StorageTablesPlugin : public Plugin<StorageTablesPlugin> {
 public:
  ~StorageTablesPlugin() override;

  // The framework calls this once per TraceProcessorImpl construction (the
  // caller caches the result), so the build-flag / modules inserts are safe
  // to run inline without an initialization guard.
  void RegisterDataframes(std::vector<PluginDataframe>& out) override {
    auto* s = trace_context_->storage.get();
    InsertIntoBuildFlagsTable(s->mutable_build_flags_table(),
                              s->mutable_string_pool());
    InsertIntoModulesTable(s->mutable_modules_table(),
                           s->mutable_string_pool());
    AddDataframe(out, s->mutable_aggregate_profile_table());
    AddDataframe(out, s->mutable_aggregate_sample_table());
    AddDataframe(out, s->mutable_android_aflags_table());
    AddDataframe(out, s->mutable_android_cpu_per_uid_track_table());
    AddDataframe(out, s->mutable_android_dumpstate_table());
    AddDataframe(out, s->mutable_android_game_intervenion_list_table());
    AddDataframe(out, s->mutable_log_table());
    AddDataframe(out, s->mutable_build_flags_table());
    AddDataframe(out, s->mutable_modules_table());
    AddDataframe(out, s->mutable_clock_snapshot_table());
    AddDataframe(out, s->mutable_cpu_freq_table());
    AddDataframe(out, s->mutable_chrome_stack_sample_extras_table());
    AddDataframe(out, s->mutable_elf_file_table());
    AddDataframe(out, s->mutable_etm_v4_configuration_table());
    AddDataframe(out, s->mutable_etm_v4_session_table());
    AddDataframe(out, s->mutable_etm_v4_chunk_table());
    AddDataframe(out, s->mutable_experimental_missing_chrome_processes_table());
    AddDataframe(out, s->mutable_experimental_proto_content_table());
    AddDataframe(out, s->mutable_file_table());
    AddDataframe(out, s->mutable_filedescriptor_table());
    AddDataframe(out, s->mutable_gpu_context_table());
    AddDataframe(out, s->mutable_gpu_counter_group_table());
    AddDataframe(out, s->mutable_gpu_table());
    AddDataframe(out, s->mutable_machine_table());
    AddDataframe(out, s->mutable_memory_snapshot_edge_table());
    AddDataframe(out, s->mutable_memory_snapshot_table());
    AddDataframe(out, s->mutable_mmap_record_table());
    AddDataframe(out, s->mutable_package_list_table());
    AddDataframe(out, s->mutable_user_list_table());
    AddDataframe(out, s->mutable_profiler_async_context_table());
    AddDataframe(out, s->mutable_profiler_task_context_table());
    AddDataframe(out, s->mutable_profiler_execution_context_table());
    AddDataframe(out, s->mutable_profiler_session_table());
    AddDataframe(out, s->mutable_profiler_sample_table());
    AddDataframe(out, s->mutable_profiler_counter_set_table());
    AddDataframe(out, s->mutable_process_memory_snapshot_table());
    AddDataframe(out, s->mutable_profiler_smaps_table());
    AddDataframe(out, s->mutable_protolog_table());
    AddDataframe(out, s->mutable_winscope_trace_rect_table());
    AddDataframe(out, s->mutable_winscope_rect_table());
    AddDataframe(out, s->mutable_winscope_fill_region_table());
    AddDataframe(out, s->mutable_winscope_transform_table());
    AddDataframe(out, s->mutable_spe_record_table());
    AddDataframe(out, s->mutable_spurious_sched_wakeup_table());
    AddDataframe(out, s->mutable_surfaceflinger_transaction_flag_table());
    AddDataframe(out, s->mutable_trace_file_table());
    AddDataframe(out, s->mutable_trace_import_logs_table());
    AddDataframe(out, s->mutable_v8_isolate_table());
    AddDataframe(out, s->mutable_v8_js_function_table());
    AddDataframe(out, s->mutable_v8_js_script_table());
    AddDataframe(out, s->mutable_v8_wasm_script_table());
    AddDataframe(out,
                 s->mutable_window_manager_shell_transition_handlers_table());
    AddDataframe(
        out, s->mutable_window_manager_shell_transition_participants_table());
    // The jit_code_id index is required for `callstacks.stack_profile` to
    // join with `_v8_js_code` efficiently; without it, module import times
    // on V8 traces regress significantly.
    AddDataframe(out, s->mutable_v8_js_code_table(), {{"jit_code_id"}});
    AddDataframe(out, s->mutable_v8_internal_code_table());
    AddDataframe(out, s->mutable_v8_wasm_code_table());
    AddDataframe(out, s->mutable_v8_regexp_code_table());
    AddDataframe(out, s->mutable_symbol_table());
    AddDataframe(out, s->mutable_jit_code_table());
    AddDataframe(out, s->mutable_jit_frame_table());
    AddDataframe(out, s->mutable_android_key_events_table());
    AddDataframe(out, s->mutable_android_motion_events_table());
    AddDataframe(out, s->mutable_android_input_event_dispatch_table());
    AddDataframe(out, s->mutable_inputmethod_clients_table());
    AddDataframe(out, s->mutable_inputmethod_manager_service_table());
    AddDataframe(out, s->mutable_inputmethod_service_table());
    AddDataframe(out, s->mutable_surfaceflinger_layers_snapshot_table());
    AddDataframe(out, s->mutable_surfaceflinger_display_table());
    AddDataframe(out, s->mutable_surfaceflinger_layer_table());
    AddDataframe(out, s->mutable_surfaceflinger_transactions_table());
    AddDataframe(out, s->mutable_surfaceflinger_transaction_table());
    AddDataframe(out, s->mutable_viewcapture_table());
    AddDataframe(out, s->mutable_viewcapture_view_table());
    AddDataframe(out, s->mutable_windowmanager_table());
    AddDataframe(out, s->mutable_windowmanager_windowcontainer_table());
    AddDataframe(out,
                 s->mutable_window_manager_shell_transition_protos_table());
    AddDataframe(out, s->mutable_window_manager_shell_transitions_table());
    AddDataframe(out, s->mutable_memory_snapshot_node_table());
    AddDataframe(out, s->mutable_experimental_proto_path_table());
    AddDataframe(out, s->mutable_arg_table());
    AddDataframe(out, s->mutable_heap_graph_object_table());
    AddDataframe(out, s->mutable_heap_graph_primitive_table());
    AddDataframe(out, s->mutable_heap_graph_object_data_table());
    AddDataframe(out, s->mutable_heap_graph_reference_table());
    AddDataframe(out, s->mutable_heap_graph_class_table());
    AddDataframe(out, s->mutable_heap_profile_allocation_table());
    AddDataframe(out, s->mutable_heap_profile_table());
    AddDataframe(out, s->mutable_heap_graph_table());
    AddDataframe(out, s->mutable_heap_graph_thread_callsite_table());
    AddDataframe(out, s->mutable_heap_graph_java_oome_details_table());
    AddDataframe(out, s->mutable_stack_profile_mapping_table());
    AddDataframe(out, s->mutable_vulkan_memory_allocations_table());
    AddDataframe(out, s->mutable_chrome_raw_table());
    AddDataframe(out, s->mutable_ftrace_event_table());
    AddDataframe(out, s->mutable_thread_table());
    AddDataframe(out, s->mutable_process_table());
    AddDataframe(out, s->mutable_cpu_table());
    AddDataframe(out, s->mutable_interrupt_mapping_table());
    AddDataframe(out, s->mutable_sched_slice_table(), {{"utid"}});
    AddDataframe(out, s->mutable_thread_state_table());
    AddDataframe(out, s->mutable_track_table());
    AddDataframe(out, s->mutable_counter_table());
    AddDataframe(out, s->mutable_android_network_packets_table());
    AddDataframe(out, s->mutable_metadata_table());
    AddDataframe(out, s->mutable_stats_table());
    AddDataframe(out, s->mutable_trace_diagnostics_table());
    AddDataframe(out, s->mutable_slice_table(), {{"parent_id"}, {"track_id"}});
    AddDataframe(out, s->mutable_state_table());
    AddDataframe(out, s->mutable_track_event_callstacks_table());
    AddDataframe(out, s->mutable_flow_table(), {{"slice_in"}, {"slice_out"}});
    AddDataframe(out, s->mutable_stack_profile_frame_table());
    AddDataframe(out, s->mutable_stack_profile_callsite_table());
  }

  // IMPORTANT: GetBoundsMutationCount and GetTimestampBounds must enumerate
  // the same set of tables; if you touch one, touch the other.
  uint64_t GetBoundsMutationCount() override {
    const auto& s = *trace_context_->storage;
    return s.ftrace_event_table().mutations() +
           s.sched_slice_table().mutations() + s.counter_table().mutations() +
           s.slice_table().mutations() +
           s.heap_profile_allocation_table().mutations() +
           s.profiler_smaps_table().mutations() +
           s.thread_state_table().mutations() + s.log_table().mutations() +
           s.heap_graph_object_table().mutations() +
           s.heap_graph_table().mutations() + s.state_table().mutations() +
           s.profiler_sample_table().mutations();
  }

  std::pair<int64_t, int64_t> GetTimestampBounds() override {
    const auto& s = *trace_context_->storage;
    int64_t start_ns = std::numeric_limits<int64_t>::max();
    int64_t end_ns = 0;
    for (auto it = s.ftrace_event_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.sched_slice_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts() + std::max<int64_t>(it.dur(), 0), end_ns);
    }
    for (auto it = s.counter_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.slice_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts() + std::max<int64_t>(it.dur(), 0), end_ns);
    }
    for (auto it = s.heap_profile_allocation_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.profiler_smaps_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.thread_state_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts() + std::max<int64_t>(it.dur(), 0), end_ns);
    }
    for (auto it = s.log_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.heap_graph_object_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.graph_sample_ts(), start_ns);
      end_ns = std::max(it.graph_sample_ts(), end_ns);
    }
    for (auto it = s.heap_graph_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts(), end_ns);
    }
    for (auto it = s.state_table().IterateRows(); it; ++it) {
      start_ns = std::min(it.ts(), start_ns);
      end_ns = std::max(it.ts() + std::max<int64_t>(it.dur(), 0), end_ns);
    }
    // profiler_sample.ts is sorted: the first row holds the min, the last the
    // max.
    if (const auto& ps = s.profiler_sample_table(); ps.row_count() > 0) {
      start_ns = std::min(ps[0].ts(), start_ns);
      end_ns = std::max(ps[ps.row_count() - 1].ts(), end_ns);
    }
    return {start_ns, end_ns};
  }
};
StorageTablesPlugin::~StorageTablesPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<StorageTablesPlugin>();
      },
      StorageTablesPlugin::kPluginId, StorageTablesPlugin::kDepIds.data(),
      StorageTablesPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::storage_tables
