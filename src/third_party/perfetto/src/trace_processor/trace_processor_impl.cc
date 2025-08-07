/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/trace_processor_impl.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/thread_utils.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/db/table.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_event_parser_impl.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_reader.h"
#include "src/trace_processor/importers/android_bugreport/android_log_event_parser_impl.h"
#include "src/trace_processor/importers/android_bugreport/android_log_reader.h"
#include "src/trace_processor/importers/archive/gzip_trace_parser.h"
#include "src/trace_processor/importers/archive/tar_trace_reader.h"
#include "src/trace_processor/importers/archive/zip_trace_reader.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_parser.h"
#include "src/trace_processor/importers/art_method/art_method_parser_impl.h"
#include "src/trace_processor/importers/art_method/art_method_tokenizer.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/common/trace_parser.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_parser.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_tokenizer.h"
#include "src/trace_processor/importers/gecko/gecko_trace_parser_impl.h"
#include "src/trace_processor/importers/gecko/gecko_trace_tokenizer.h"
#include "src/trace_processor/importers/json/json_trace_parser_impl.h"
#include "src/trace_processor/importers/json/json_trace_tokenizer.h"
#include "src/trace_processor/importers/json/json_utils.h"
#include "src/trace_processor/importers/ninja/ninja_log_parser.h"
#include "src/trace_processor/importers/perf/perf_data_tokenizer.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_tracker.h"
#include "src/trace_processor/importers/perf/record_parser.h"
#include "src/trace_processor/importers/perf/spe_record_parser.h"
#include "src/trace_processor/importers/perf_text/perf_text_trace_parser_impl.h"
#include "src/trace_processor/importers/perf_text/perf_text_trace_tokenizer.h"
#include "src/trace_processor/importers/proto/additional_modules.h"
#include "src/trace_processor/importers/systrace/systrace_trace_parser.h"
#include "src/trace_processor/iterator_impl.h"
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"
#include "src/trace_processor/metrics/all_webview_metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.h"
#include "src/trace_processor/metrics/sql/amalgamated_sql_metrics.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_shared_storage.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/engine/table_pointer_module.h"
#include "src/trace_processor/perfetto_sql/generator/structured_query_generator.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/base64.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/clock_functions.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/counter_intervals.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/create_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/create_view_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/dominator_tree.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/graph_scan.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/graph_traversal.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/import.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/interval_intersect.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/layout_functions.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/math.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/pprof_functions.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/replace_numbers_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sqlite3_str_split.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/stack_functions.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/structural_tree_partition.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/to_ftrace.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/type_builders.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/utils.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/window_functions.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/counter_mipmap_operator.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/slice_mipmap_operator.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/span_join_operator.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/window_operator.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/ancestor.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/connected_flow.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/dataframe_query_plan_decoder.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/descendant.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/dfs_weight_bounded.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_annotated_stack.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_flamegraph.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_flat_slice.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_slice_layout.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/table_info.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/winscope_proto_to_args_with_defaults.h"
#include "src/trace_processor/perfetto_sql/stdlib/stdlib.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sql_stats_table.h"
#include "src/trace_processor/sqlite/stats_table.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/trace_processor_storage_impl.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/trace_summary/summary.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/gzip_utils.h"
#include "src/trace_processor/util/protozero_to_json.h"
#include "src/trace_processor/util/protozero_to_text.h"
#include "src/trace_processor/util/regex.h"
#include "src/trace_processor/util/sql_modules.h"
#include "src/trace_processor/util/trace_type.h"

#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/perfetto/perfetto_metatrace.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#include "src/trace_processor/importers/instruments/instruments_xml_tokenizer.h"
#include "src/trace_processor/importers/instruments/row_parser.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
#include "src/trace_processor/importers/etm/etm_tracker.h"
#include "src/trace_processor/importers/etm/etm_v4_stream_demultiplexer.h"
#include "src/trace_processor/importers/etm/file_tracker.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/etm_decode_trace_vtable.h"
#include "src/trace_processor/perfetto_sql/intrinsics/operators/etm_iterate_range_vtable.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/winscope_proto_to_args_with_defaults.h"
#endif

namespace perfetto::trace_processor {
namespace {

template <typename SqlFunction, typename Ptr = typename SqlFunction::Context*>
void RegisterFunction(PerfettoSqlEngine* engine,
                      const char* name,
                      int argc,
                      Ptr context = nullptr,
                      bool deterministic = true) {
  auto status = engine->RegisterStaticFunction<SqlFunction>(
      name, argc, std::move(context), deterministic);
  if (!status.ok())
    PERFETTO_ELOG("%s", status.c_message());
}

base::Status RegisterAllProtoBuilderFunctions(
    const DescriptorPool* pool,
    std::unordered_map<std::string, std::string>* proto_fn_name_to_path,
    PerfettoSqlEngine* engine,
    TraceProcessor* tp) {
  for (uint32_t i = 0; i < pool->descriptors().size(); ++i) {
    // Convert the full name (e.g. .perfetto.protos.TraceMetrics.SubMetric)
    // into a function name of the form (TraceMetrics_SubMetric).
    const auto& desc = pool->descriptors()[i];
    auto fn_name = desc.full_name().substr(desc.package_name().size() + 1);
    std::replace(fn_name.begin(), fn_name.end(), '.', '_');
    auto registered_fn = proto_fn_name_to_path->find(fn_name);
    if (registered_fn != proto_fn_name_to_path->end() &&
        registered_fn->second != desc.full_name()) {
      return base::ErrStatus(
          "Attempt to create new metric function '%s' for different descriptor "
          "'%s' that conflicts with '%s'",
          fn_name.c_str(), desc.full_name().c_str(),
          registered_fn->second.c_str());
    }
    RegisterFunction<metrics::BuildProto>(
        engine, fn_name.c_str(), -1,
        std::make_unique<metrics::BuildProto::Context>(
            metrics::BuildProto::Context{tp, pool, i}));
    proto_fn_name_to_path->emplace(fn_name, desc.full_name());
  }
  return base::OkStatus();
}

void BuildBoundsTable(sqlite3* db, std::pair<int64_t, int64_t> bounds) {
  char* error = nullptr;
  sqlite3_exec(db, "DELETE FROM _trace_bounds", nullptr, nullptr, &error);
  if (error) {
    PERFETTO_ELOG("Error deleting from bounds table: %s", error);
    sqlite3_free(error);
    return;
  }

  base::StackString<1024> sql("INSERT INTO _trace_bounds VALUES(%" PRId64
                              ", %" PRId64 ")",
                              bounds.first, bounds.second);
  sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (error) {
    PERFETTO_ELOG("Error inserting bounds table: %s", error);
    sqlite3_free(error);
  }
}

template <typename T>
void AddUnfinalizedStaticTable(
    std::vector<PerfettoSqlEngine::UnfinalizedStaticTable>& tables,
    T* table_instance) {
  tables.push_back({
      &table_instance->dataframe(),
      T::Name(),
  });
}

base::StatusOr<sql_modules::RegisteredPackage> ToRegisteredPackage(
    const SqlPackage& package) {
  const std::string& name = package.name;
  sql_modules::RegisteredPackage new_package;
  for (auto const& module_name_and_sql : package.modules) {
    if (sql_modules::GetPackageName(module_name_and_sql.first) != name) {
      return base::ErrStatus(
          "Module name doesn't match the package name. First part of module "
          "name should be package name. Import key: '%s', package name: '%s'.",
          module_name_and_sql.first.c_str(), name.c_str());
    }
    new_package.modules.Insert(module_name_and_sql.first,
                               {module_name_and_sql.second, false});
  }
  return std::move(new_package);
}

class ValueAtMaxTs : public SqliteAggregateFunction<ValueAtMaxTs> {
 public:
  static constexpr char kName[] = "VALUE_AT_MAX_TS";
  static constexpr int kArgCount = 2;
  struct Context {
    bool initialized;
    int value_type;

    int64_t max_ts;
    int64_t int_value_at_max_ts;
    double double_value_at_max_ts;
  };

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    sqlite3_value* ts = argv[0];
    sqlite3_value* value = argv[1];

    // Note that sqlite3_aggregate_context zeros the memory for us so all the
    // variables of the struct should be zero.
    auto* fn_ctx = reinterpret_cast<Context*>(
        sqlite3_aggregate_context(ctx, sizeof(Context)));

    // For performance reasons, we only do the check for the type of ts and
    // value on the first call of the function.
    if (PERFETTO_UNLIKELY(!fn_ctx->initialized)) {
      if (sqlite3_value_type(ts) != SQLITE_INTEGER) {
        return sqlite::result::Error(
            ctx, "VALUE_AT_MAX_TS: ts passed was not an integer");
      }

      fn_ctx->value_type = sqlite3_value_type(value);
      if (fn_ctx->value_type != SQLITE_INTEGER &&
          fn_ctx->value_type != SQLITE_FLOAT) {
        return sqlite::result::Error(
            ctx, "VALUE_AT_MAX_TS: value passed was not an integer or float");
      }

      fn_ctx->max_ts = std::numeric_limits<int64_t>::min();
      fn_ctx->initialized = true;
    }

    // On dcheck builds however, we check every passed ts and value.
#if PERFETTO_DCHECK_IS_ON()
    if (sqlite3_value_type(ts) != SQLITE_INTEGER) {
      return sqlite::result::Error(
          ctx, "VALUE_AT_MAX_TS: ts passed was not an integer");
    }
    if (sqlite3_value_type(value) != fn_ctx->value_type) {
      return sqlite::result::Error(
          ctx, "VALUE_AT_MAX_TS: value type is inconsistent");
    }
#endif

    int64_t ts_int = sqlite3_value_int64(ts);
    if (PERFETTO_LIKELY(fn_ctx->max_ts <= ts_int)) {
      fn_ctx->max_ts = ts_int;

      if (fn_ctx->value_type == SQLITE_INTEGER) {
        fn_ctx->int_value_at_max_ts = sqlite3_value_int64(value);
      } else {
        fn_ctx->double_value_at_max_ts = sqlite3_value_double(value);
      }
    }
  }

  static void Final(sqlite3_context* ctx) {
    auto* fn_ctx = static_cast<Context*>(sqlite3_aggregate_context(ctx, 0));
    if (!fn_ctx) {
      sqlite::result::Null(ctx);
      return;
    }
    if (fn_ctx->value_type == SQLITE_INTEGER) {
      sqlite::result::Long(ctx, fn_ctx->int_value_at_max_ts);
    } else {
      sqlite::result::Double(ctx, fn_ctx->double_value_at_max_ts);
    }
  }
};

void RegisterValueAtMaxTsFunction(PerfettoSqlEngine& engine) {
  base::Status status =
      engine.RegisterSqliteAggregateFunction<ValueAtMaxTs>(nullptr);
  if (!status.ok()) {
    PERFETTO_ELOG("Error initializing VALUE_AT_MAX_TS");
  }
}

std::vector<std::string> SanitizeMetricMountPaths(
    const std::vector<std::string>& mount_paths) {
  std::vector<std::string> sanitized;
  for (const auto& path : mount_paths) {
    if (path.empty())
      continue;
    sanitized.push_back(path);
    if (path.back() != '/')
      sanitized.back().append("/");
  }
  return sanitized;
}

void InsertIntoTraceMetricsTable(sqlite3* db, const std::string& metric_name) {
  char* insert_sql = sqlite3_mprintf(
      "INSERT INTO _trace_metrics(name) VALUES('%q')", metric_name.c_str());
  char* insert_error = nullptr;
  sqlite3_exec(db, insert_sql, nullptr, nullptr, &insert_error);
  sqlite3_free(insert_sql);
  if (insert_error) {
    PERFETTO_ELOG("Error registering table: %s", insert_error);
    sqlite3_free(insert_error);
  }
}

sql_modules::NameToPackage GetStdlibPackages() {
  sql_modules::NameToPackage packages;
  for (const auto& file_to_sql : stdlib::kFileToSql) {
    std::string module_name = sql_modules::GetIncludeKey(file_to_sql.path);
    std::string package_name = sql_modules::GetPackageName(module_name);
    packages.Insert(package_name, {})
        .first->push_back({module_name, file_to_sql.sql});
  }
  return packages;
}

std::pair<int64_t, int64_t> GetTraceTimestampBoundsNs(
    const TraceStorage& storage) {
  int64_t start_ns = std::numeric_limits<int64_t>::max();
  int64_t end_ns = std::numeric_limits<int64_t>::min();
  for (auto it = storage.ftrace_event_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.sched_slice_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts() + it.dur(), end_ns);
  }
  for (auto it = storage.counter_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.slice_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts() + it.dur(), end_ns);
  }
  for (auto it = storage.heap_profile_allocation_table().IterateRows(); it;
       ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.thread_state_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts() + it.dur(), end_ns);
  }
  for (auto it = storage.android_log_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.heap_graph_object_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.graph_sample_ts(), start_ns);
    end_ns = std::max(it.graph_sample_ts(), end_ns);
  }
  for (auto it = storage.perf_sample_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.instruments_sample_table().IterateRows(); it; ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  for (auto it = storage.cpu_profile_stack_sample_table().IterateRows(); it;
       ++it) {
    start_ns = std::min(it.ts(), start_ns);
    end_ns = std::max(it.ts(), end_ns);
  }
  if (start_ns == std::numeric_limits<int64_t>::max()) {
    return std::make_pair(0, 0);
  }
  if (start_ns == end_ns) {
    end_ns += 1;
  }
  return std::make_pair(start_ns, end_ns);
}

}  // namespace

TraceProcessorImpl::TraceProcessorImpl(const Config& cfg)
    : TraceProcessorStorageImpl(cfg), config_(cfg) {
  context_.reader_registry->RegisterTraceReader<AndroidDumpstateReader>(
      kAndroidDumpstateTraceType);
  context_.android_dumpstate_event_parser =
      std::make_unique<AndroidDumpstateEventParserImpl>(&context_);

  context_.reader_registry->RegisterTraceReader<AndroidLogReader>(
      kAndroidLogcatTraceType);
  context_.android_log_event_parser =
      std::make_unique<AndroidLogEventParserImpl>(&context_);

  context_.reader_registry->RegisterTraceReader<FuchsiaTraceTokenizer>(
      kFuchsiaTraceType);
  context_.fuchsia_record_parser =
      std::make_unique<FuchsiaTraceParser>(&context_);

  context_.reader_registry->RegisterTraceReader<SystraceTraceParser>(
      kSystraceTraceType);
  context_.reader_registry->RegisterTraceReader<NinjaLogParser>(
      kNinjaLogTraceType);

  context_.reader_registry
      ->RegisterTraceReader<perf_importer::PerfDataTokenizer>(
          kPerfDataTraceType);
  context_.perf_record_parser =
      std::make_unique<perf_importer::RecordParser>(&context_);
  context_.spe_record_parser =
      std::make_unique<perf_importer::SpeRecordParserImpl>(&context_);

#if PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
  context_.reader_registry
      ->RegisterTraceReader<instruments_importer::InstrumentsXmlTokenizer>(
          kInstrumentsXmlTraceType);
  context_.instruments_row_parser =
      std::make_unique<instruments_importer::RowParser>(&context_);
#endif

  if constexpr (util::IsGzipSupported()) {
    context_.reader_registry->RegisterTraceReader<GzipTraceParser>(
        kGzipTraceType);
    context_.reader_registry->RegisterTraceReader<GzipTraceParser>(
        kCtraceTraceType);
    context_.reader_registry->RegisterTraceReader<ZipTraceReader>(kZipFile);
  }

  if constexpr (json::IsJsonSupported()) {
    context_.reader_registry->RegisterTraceReader<JsonTraceTokenizer>(
        kJsonTraceType);
    context_.json_trace_parser =
        std::make_unique<JsonTraceParserImpl>(&context_);

    context_.reader_registry
        ->RegisterTraceReader<gecko_importer::GeckoTraceTokenizer>(
            kGeckoTraceType);
    context_.gecko_trace_parser =
        std::make_unique<gecko_importer::GeckoTraceParserImpl>(&context_);
  }

  context_.reader_registry->RegisterTraceReader<art_method::ArtMethodTokenizer>(
      kArtMethodTraceType);
  context_.art_method_parser =
      std::make_unique<art_method::ArtMethodParserImpl>(&context_);

  context_.reader_registry->RegisterTraceReader<art_hprof::ArtHprofParser>(
      kArtHprofTraceType);

  context_.reader_registry
      ->RegisterTraceReader<perf_text_importer::PerfTextTraceTokenizer>(
          kPerfTextTraceType);
  context_.perf_text_parser =
      std::make_unique<perf_text_importer::PerfTextTraceParserImpl>(&context_);

  context_.reader_registry->RegisterTraceReader<TarTraceReader>(kTarTraceType);

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  perf_importer::PerfTracker::GetOrCreate(&context_)->RegisterAuxTokenizer(
      PERF_AUXTRACE_CS_ETM, etm::CreateEtmV4StreamDemultiplexer);
#endif

  const std::vector<std::string> sanitized_extension_paths =
      SanitizeMetricMountPaths(config_.skip_builtin_metric_paths);
  std::vector<std::string> skip_prefixes;
  skip_prefixes.reserve(sanitized_extension_paths.size());
  for (const auto& path : sanitized_extension_paths) {
    skip_prefixes.push_back(kMetricProtoRoot + path);
  }

  // Add metrics to descriptor pool
  metrics_descriptor_pool_.AddFromFileDescriptorSet(
      kMetricsDescriptor.data(), kMetricsDescriptor.size(), skip_prefixes);
  metrics_descriptor_pool_.AddFromFileDescriptorSet(
      kAllChromeMetricsDescriptor.data(), kAllChromeMetricsDescriptor.size(),
      skip_prefixes);
  metrics_descriptor_pool_.AddFromFileDescriptorSet(
      kAllWebviewMetricsDescriptor.data(), kAllWebviewMetricsDescriptor.size(),
      skip_prefixes);

  // Add the summary descriptor to the summary pool.
  {
    base::Status status = context_.descriptor_pool_->AddFromFileDescriptorSet(
        kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size());
    PERFETTO_CHECK(status.ok());
  }
  RegisterAdditionalModules(&context_);

  // Register stdlib packages.
  auto packages = GetStdlibPackages();
  for (auto package = packages.GetIterator(); package; ++package) {
    registered_sql_packages_.emplace_back<SqlPackage>(
        {/*name=*/package.key(),
         /*modules=*/package.value(),
         /*allow_override=*/false});
  }

  engine_ = InitPerfettoSqlEngine(
      &context_, context_.storage.get(), config_, &dataframe_shared_storage_,
      registered_sql_packages_, sql_metrics_, &metrics_descriptor_pool_,
      &proto_fn_name_to_path_, this, notify_eof_called_);

  sqlite_objects_post_prelude_ = engine_->SqliteRegisteredObjectCount();

  bool skip_all_sql = std::find(config_.skip_builtin_metric_paths.begin(),
                                config_.skip_builtin_metric_paths.end(),
                                "") != config_.skip_builtin_metric_paths.end();
  if (!skip_all_sql) {
    for (const auto& file_to_sql : sql_metrics::kFileToSql) {
      if (base::StartsWithAny(file_to_sql.path, sanitized_extension_paths))
        continue;
      RegisterMetric(file_to_sql.path, file_to_sql.sql);
    }
  }
}

TraceProcessorImpl::~TraceProcessorImpl() = default;

// =================================================================
// |        TraceProcessorStorage implementation starts here       |
// =================================================================

base::Status TraceProcessorImpl::Parse(TraceBlobView blob) {
  bytes_parsed_ += blob.size();
  return TraceProcessorStorageImpl::Parse(std::move(blob));
}

void TraceProcessorImpl::Flush() {
  TraceProcessorStorageImpl::Flush();
  BuildBoundsTable(engine_->sqlite_engine()->db(),
                   GetTraceTimestampBoundsNs(*context_.storage));
}

base::Status TraceProcessorImpl::NotifyEndOfFile() {
  if (notify_eof_called_) {
    const char kMessage[] =
        "NotifyEndOfFile should only be called once. Try calling Flush instead "
        "if trying to commit the contents of the trace to tables.";
    PERFETTO_ELOG(kMessage);
    return base::ErrStatus(kMessage);
  }
  notify_eof_called_ = true;

  if (current_trace_name_.empty())
    current_trace_name_ = "Unnamed trace";

  // Last opportunity to flush all pending data.
  Flush();

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  if (context_.etm_tracker) {
    RETURN_IF_ERROR(etm::EtmTracker::GetOrCreate(&context_)->Finalize());
  }
#endif

  RETURN_IF_ERROR(TraceProcessorStorageImpl::NotifyEndOfFile());
  if (context_.perf_tracker) {
    perf_importer::PerfTracker::GetOrCreate(&context_)->NotifyEndOfFile();
  }

  // Rebuild the bounds table once everything has been completed: we do this
  // so that if any data was added to tables in
  // TraceProcessorStorageImpl::NotifyEndOfFile, this will be counted in
  // trace bounds: this is important for parsers like ninja which wait until
  // the end to flush all their data.
  BuildBoundsTable(engine_->sqlite_engine()->db(),
                   GetTraceTimestampBoundsNs(*context_.storage));

  TraceProcessorStorageImpl::DestroyContext();
  context_.storage->ShrinkToFitTables();

  engine_->FinalizeAndShareAllStaticTables();
  IncludeAfterEofPrelude(engine_.get());
  sqlite_objects_post_prelude_ = engine_->SqliteRegisteredObjectCount();

  return base::OkStatus();
}

// =================================================================
// |        PerfettoSQL related functionality starts here          |
// =================================================================

Iterator TraceProcessorImpl::ExecuteQuery(const std::string& sql) {
  PERFETTO_TP_TRACE(metatrace::Category::API_TIMELINE, "EXECUTE_QUERY",
                    [&](metatrace::Record* r) { r->AddArg("query", sql); });

  uint32_t sql_stats_row =
      context_.storage->mutable_sql_stats()->RecordQueryBegin(
          sql, base::GetWallTimeNs().count());
  std::string non_breaking_sql = base::ReplaceAll(sql, "\u00A0", " ");
  base::StatusOr<PerfettoSqlEngine::ExecutionResult> result =
      engine_->ExecuteUntilLastStatement(
          SqlSource::FromExecuteQuery(std::move(non_breaking_sql)));
  std::unique_ptr<IteratorImpl> impl(
      new IteratorImpl(this, std::move(result), sql_stats_row));
  return Iterator(std::move(impl));
}

base::Status TraceProcessorImpl::RegisterSqlPackage(SqlPackage sql_package) {
  std::string name = sql_package.name;
  if (engine_->FindPackage(name) && !sql_package.allow_override) {
    return base::ErrStatus(
        "Package '%s' is already registered. Choose a different name.\n"
        "If you want to replace the existing package using trace processor "
        "shell, you need to pass the --dev flag and use "
        "--override-sql-package "
        "to pass the module path.",
        name.c_str());
  }
  ASSIGN_OR_RETURN(auto new_package, ToRegisteredPackage(sql_package));
  registered_sql_packages_.emplace_back(std::move(sql_package));
  engine_->RegisterPackage(name, std::move(new_package));
  return base::OkStatus();
}

base::Status TraceProcessorImpl::RegisterSqlModule(SqlModule module) {
  SqlPackage package;
  package.name = std::move(module.name);
  package.modules = std::move(module.files);
  package.allow_override = module.allow_module_override;
  return RegisterSqlPackage(package);
}

// =================================================================
// |  Trace-based metrics (v2) related functionality starts here   |
// =================================================================

base::Status TraceProcessorImpl::Summarize(
    const TraceSummaryComputationSpec& computation,
    const std::vector<TraceSummarySpecBytes>& specs,
    std::vector<uint8_t>* output,
    const TraceSummaryOutputSpec& output_spec) {
  return summary::Summarize(this, *context_.descriptor_pool_, computation,
                            specs, output, output_spec);
}

// =================================================================
// |        Metatracing related functionality starts here          |
// =================================================================

void TraceProcessorImpl::EnableMetatrace(MetatraceConfig config) {
  metatrace::Enable(config);
}

// =================================================================
// |                      Experimental                             |
// =================================================================

base::Status TraceProcessorImpl::AnalyzeStructuredQueries(
    const std::vector<StructuredQueryBytes>& sqs,
    std::vector<AnalyzedStructuredQuery>* output) {
  auto opt_idx = metrics_descriptor_pool_.FindDescriptorIdx(
      ".perfetto.protos.TraceSummarySpec");
  if (!opt_idx) {
    metrics_descriptor_pool_.AddFromFileDescriptorSet(
        kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size());
  }
  perfetto_sql::generator::StructuredQueryGenerator sqg;
  for (const auto& sq : sqs) {
    AnalyzedStructuredQuery analyzed_sq;
    ASSIGN_OR_RETURN(analyzed_sq.sql, sqg.Generate(sq.ptr, sq.size));
    analyzed_sq.textproto =
        perfetto::trace_processor::protozero_to_text::ProtozeroToText(
            metrics_descriptor_pool_,
            ".perfetto.protos.PerfettoSqlStructuredQuery",
            protozero::ConstBytes{sq.ptr, sq.size},
            perfetto::trace_processor::protozero_to_text::kIncludeNewLines);
    analyzed_sq.modules = sqg.ComputeReferencedModules();
    analyzed_sq.preambles = sqg.ComputePreambles();
    sqg.AddQuery(sq.ptr, sq.size);
    output->push_back(analyzed_sq);
  }
  return base::OkStatus();
}

namespace {

class StringInterner {
 public:
  StringInterner(protos::pbzero::PerfettoMetatrace& event,
                 base::FlatHashMap<std::string, uint64_t>& interned_strings)
      : event_(event), interned_strings_(interned_strings) {}

  ~StringInterner() {
    for (const auto& interned_string : new_interned_strings_) {
      auto* interned_string_proto = event_.add_interned_strings();
      interned_string_proto->set_iid(interned_string.first);
      interned_string_proto->set_value(interned_string.second);
    }
  }

  uint64_t InternString(const std::string& str) {
    uint64_t new_iid = interned_strings_.size();
    auto insert_result = interned_strings_.Insert(str, new_iid);
    if (insert_result.second) {
      new_interned_strings_.emplace_back(new_iid, str);
    }
    return *insert_result.first;
  }

 private:
  protos::pbzero::PerfettoMetatrace& event_;
  base::FlatHashMap<std::string, uint64_t>& interned_strings_;

  base::SmallVector<std::pair<uint64_t, std::string>, 16> new_interned_strings_;
};

}  // namespace

base::Status TraceProcessorImpl::DisableAndReadMetatrace(
    std::vector<uint8_t>* trace_proto) {
  protozero::HeapBuffered<protos::pbzero::Trace> trace;

  auto* clock_snapshot = trace->add_packet()->set_clock_snapshot();
  for (const auto& [clock_id, ts] : base::CaptureClockSnapshots()) {
    auto* clock = clock_snapshot->add_clocks();
    clock->set_clock_id(clock_id);
    clock->set_timestamp(ts);
  }

  auto tid = static_cast<uint32_t>(base::GetThreadId());
  base::FlatHashMap<std::string, uint64_t> interned_strings;
  metatrace::DisableAndReadBuffer(
      [&trace, &interned_strings, tid](metatrace::Record* record) {
        auto* packet = trace->add_packet();
        packet->set_timestamp(record->timestamp_ns);
        auto* evt = packet->set_perfetto_metatrace();

        StringInterner interner(*evt, interned_strings);

        evt->set_event_name_iid(interner.InternString(record->event_name));
        evt->set_event_duration_ns(record->duration_ns);
        evt->set_thread_id(tid);

        if (record->args_buffer_size == 0)
          return;

        base::StringSplitter s(
            record->args_buffer, record->args_buffer_size, '\0',
            base::StringSplitter::EmptyTokenMode::ALLOW_EMPTY_TOKENS);
        for (; s.Next();) {
          auto* arg_proto = evt->add_args();
          arg_proto->set_key_iid(interner.InternString(s.cur_token()));

          bool has_next = s.Next();
          PERFETTO_CHECK(has_next);
          arg_proto->set_value_iid(interner.InternString(s.cur_token()));
        }
      });
  *trace_proto = trace.SerializeAsArray();
  return base::OkStatus();
}

// =================================================================
// |              Advanced functionality starts here               |
// =================================================================

std::string TraceProcessorImpl::GetCurrentTraceName() {
  if (current_trace_name_.empty())
    return "";
  auto size = " (" + std::to_string(bytes_parsed_ / 1024 / 1024) + " MB)";
  return current_trace_name_ + size;
}

void TraceProcessorImpl::SetCurrentTraceName(const std::string& name) {
  current_trace_name_ = name;
}

base::Status TraceProcessorImpl::RegisterFileContent(
    [[maybe_unused]] const std::string& path,
    [[maybe_unused]] TraceBlobView content) {
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  return etm::FileTracker::GetOrCreate(&context_)->AddFile(path,
                                                           std::move(content));
#else
  return base::OkStatus();
#endif
}

void TraceProcessorImpl::InterruptQuery() {
  if (!engine_->sqlite_engine()->db())
    return;
  query_interrupted_.store(true);
  sqlite3_interrupt(engine_->sqlite_engine()->db());
}

size_t TraceProcessorImpl::RestoreInitialTables() {
  // We should always have at least as many objects now as we did in the
  // constructor.
  uint64_t registered_count_before = engine_->SqliteRegisteredObjectCount();
  PERFETTO_CHECK(registered_count_before >= sqlite_objects_post_prelude_);

  // Reset the engine to its initial state.
  engine_ = InitPerfettoSqlEngine(
      &context_, context_.storage.get(), config_, &dataframe_shared_storage_,
      registered_sql_packages_, sql_metrics_, &metrics_descriptor_pool_,
      &proto_fn_name_to_path_, this, notify_eof_called_);

  // The registered count should now be the same as it was in the constructor.
  uint64_t registered_count_after = engine_->SqliteRegisteredObjectCount();
  PERFETTO_CHECK(registered_count_after == sqlite_objects_post_prelude_);
  return static_cast<size_t>(registered_count_before - registered_count_after);
}

// =================================================================
// |  Trace-based metrics (v1) related functionality starts here   |
// =================================================================

base::Status TraceProcessorImpl::RegisterMetric(const std::string& path,
                                                const std::string& sql) {
  // Check if the metric with the given path already exists and if it does,
  // just update the SQL associated with it.
  auto it = std::find_if(
      sql_metrics_.begin(), sql_metrics_.end(),
      [&path](const metrics::SqlMetricFile& m) { return m.path == path; });
  if (it != sql_metrics_.end()) {
    it->sql = sql;
    return base::OkStatus();
  }

  auto sep_idx = path.rfind('/');
  std::string basename =
      sep_idx == std::string::npos ? path : path.substr(sep_idx + 1);

  auto sql_idx = basename.rfind(".sql");
  if (sql_idx == std::string::npos) {
    return base::ErrStatus("Unable to find .sql extension for metric");
  }
  auto no_ext_name = basename.substr(0, sql_idx);

  metrics::SqlMetricFile metric;
  metric.path = path;
  metric.sql = sql;

  if (IsRootMetricField(no_ext_name)) {
    metric.proto_field_name = no_ext_name;
    metric.output_table_name = no_ext_name + "_output";

    auto field_it_and_inserted =
        proto_field_to_sql_metric_path_.emplace(*metric.proto_field_name, path);
    if (!field_it_and_inserted.second) {
      // We already had a metric with this field name in the map. However, if
      // this was the case, we should have found the metric in
      // |path_to_sql_metric_file_| above if we are simply overriding the
      // metric. Return an error since this means we have two different SQL
      // files which are trying to output the same metric.
      const auto& prev_path = field_it_and_inserted.first->second;
      PERFETTO_DCHECK(prev_path != path);
      return base::ErrStatus(
          "RegisterMetric Error: Metric paths %s (which is already "
          "registered) "
          "and %s are both trying to output the proto field %s",
          prev_path.c_str(), path.c_str(), metric.proto_field_name->c_str());
    }
  }

  if (metric.proto_field_name) {
    InsertIntoTraceMetricsTable(engine_->sqlite_engine()->db(),
                                *metric.proto_field_name);
  }
  sql_metrics_.emplace_back(metric);
  return base::OkStatus();
}

base::Status TraceProcessorImpl::ExtendMetricsProto(const uint8_t* data,
                                                    size_t size) {
  return ExtendMetricsProto(data, size, /*skip_prefixes*/ {});
}

base::Status TraceProcessorImpl::ExtendMetricsProto(
    const uint8_t* data,
    size_t size,
    const std::vector<std::string>& skip_prefixes) {
  RETURN_IF_ERROR(metrics_descriptor_pool_.AddFromFileDescriptorSet(
      data, size, skip_prefixes));
  RETURN_IF_ERROR(RegisterAllProtoBuilderFunctions(
      &metrics_descriptor_pool_, &proto_fn_name_to_path_, engine_.get(), this));
  return base::OkStatus();
}

base::Status TraceProcessorImpl::ComputeMetric(
    const std::vector<std::string>& metric_names,
    std::vector<uint8_t>* metrics_proto) {
  auto opt_idx = metrics_descriptor_pool_.FindDescriptorIdx(
      ".perfetto.protos.TraceMetrics");
  if (!opt_idx.has_value())
    return base::Status("Root metrics proto descriptor not found");

  const auto& root_descriptor =
      metrics_descriptor_pool_.descriptors()[opt_idx.value()];
  return metrics::ComputeMetrics(engine_.get(), metric_names, sql_metrics_,
                                 metrics_descriptor_pool_, root_descriptor,
                                 metrics_proto);
}

base::Status TraceProcessorImpl::ComputeMetricText(
    const std::vector<std::string>& metric_names,
    TraceProcessor::MetricResultFormat format,
    std::string* metrics_string) {
  std::vector<uint8_t> metrics_proto;
  base::Status status = ComputeMetric(metric_names, &metrics_proto);
  if (!status.ok())
    return status;
  switch (format) {
    case TraceProcessor::MetricResultFormat::kProtoText:
      *metrics_string = protozero_to_text::ProtozeroToText(
          metrics_descriptor_pool_, ".perfetto.protos.TraceMetrics",
          protozero::ConstBytes{metrics_proto.data(), metrics_proto.size()},
          protozero_to_text::kIncludeNewLines);
      break;
    case TraceProcessor::MetricResultFormat::kJson:
      *metrics_string = protozero_to_json::ProtozeroToJson(
          metrics_descriptor_pool_, ".perfetto.protos.TraceMetrics",
          protozero::ConstBytes{metrics_proto.data(), metrics_proto.size()},
          protozero_to_json::kPretty | protozero_to_json::kInlineErrors |
              protozero_to_json::kInlineAnnotations);
      break;
  }
  return status;
}

std::vector<uint8_t> TraceProcessorImpl::GetMetricDescriptors() {
  return metrics_descriptor_pool_.SerializeAsDescriptorSet();
}

std::vector<PerfettoSqlEngine::LegacyStaticTable>
TraceProcessorImpl::GetLegacyStaticTables(TraceStorage*) {
  std::vector<PerfettoSqlEngine::LegacyStaticTable> tables;
  return tables;
}

std::vector<PerfettoSqlEngine::UnfinalizedStaticTable>
TraceProcessorImpl::GetUnfinalizedStaticTables(TraceStorage* storage) {
  std::vector<PerfettoSqlEngine::UnfinalizedStaticTable> tables;
  AddUnfinalizedStaticTable(tables, storage->mutable_android_dumpstate_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_android_game_intervenion_list_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_android_log_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_clock_snapshot_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_cpu_freq_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_cpu_profile_stack_sample_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_elf_file_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_etm_v4_configuration_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_etm_v4_session_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_etm_v4_trace_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_experimental_missing_chrome_processes_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_experimental_proto_content_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_file_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_filedescriptor_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_gpu_counter_group_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_instruments_sample_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_machine_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_memory_snapshot_edge_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_memory_snapshot_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_mmap_record_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_package_list_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_perf_session_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_process_memory_snapshot_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_profiler_smaps_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_protolog_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_winscope_trace_rect_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_winscope_rect_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_winscope_fill_region_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_winscope_transform_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_spe_record_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_spurious_sched_wakeup_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_surfaceflinger_transaction_flag_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_trace_file_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_isolate_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_js_function_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_js_script_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_wasm_script_table());
  AddUnfinalizedStaticTable(
      tables,
      storage->mutable_window_manager_shell_transition_handlers_table());
  AddUnfinalizedStaticTable(
      tables,
      storage->mutable_window_manager_shell_transition_participants_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_js_code_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_internal_code_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_wasm_code_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_v8_regexp_code_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_symbol_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_jit_code_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_jit_frame_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_android_key_events_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_android_motion_events_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_android_input_event_dispatch_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_inputmethod_clients_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_inputmethod_manager_service_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_inputmethod_service_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_surfaceflinger_layers_snapshot_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_surfaceflinger_display_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_surfaceflinger_layer_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_surfaceflinger_transactions_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_surfaceflinger_transaction_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_viewcapture_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_viewcapture_view_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_windowmanager_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_window_manager_shell_transition_protos_table());
  AddUnfinalizedStaticTable(
      tables, storage->mutable_window_manager_shell_transitions_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_memory_snapshot_node_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_experimental_proto_path_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_arg_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_heap_graph_object_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_heap_graph_reference_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_heap_graph_class_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_heap_profile_allocation_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_perf_sample_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_stack_profile_mapping_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_vulkan_memory_allocations_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_chrome_raw_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_ftrace_event_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_thread_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_process_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_cpu_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_sched_slice_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_thread_state_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_track_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_counter_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_android_network_packets_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_metadata_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_slice_table());
  AddUnfinalizedStaticTable(tables, storage->mutable_flow_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_stack_profile_frame_table());
  AddUnfinalizedStaticTable(tables,
                            storage->mutable_stack_profile_callsite_table());
  return tables;
}

std::vector<std::unique_ptr<StaticTableFunction>>
TraceProcessorImpl::CreateStaticTableFunctions(TraceProcessorContext* context,
                                               TraceStorage* storage,
                                               const Config& config,
                                               PerfettoSqlEngine* engine) {
  std::vector<std::unique_ptr<StaticTableFunction>> fns;
  fns.emplace_back(std::make_unique<ExperimentalFlamegraph>(context));
  fns.emplace_back(std::make_unique<ExperimentalSliceLayout>(
      storage->mutable_string_pool(), &storage->slice_table()));
  fns.emplace_back(
      std::make_unique<TableInfo>(storage->mutable_string_pool(), engine));
  fns.emplace_back(std::make_unique<Ancestor>(Ancestor::Type::kSlice, storage));
  fns.emplace_back(std::make_unique<Ancestor>(
      Ancestor::Type::kStackProfileCallsite, storage));
  fns.emplace_back(
      std::make_unique<Ancestor>(Ancestor::Type::kSliceByStack, storage));
  fns.emplace_back(
      std::make_unique<Descendant>(Descendant::Type::kSlice, storage));
  fns.emplace_back(
      std::make_unique<Descendant>(Descendant::Type::kSliceByStack, storage));
  fns.emplace_back(std::make_unique<ConnectedFlow>(
      ConnectedFlow::Mode::kDirectlyConnectedFlow, storage));
  fns.emplace_back(std::make_unique<ConnectedFlow>(
      ConnectedFlow::Mode::kPrecedingFlow, storage));
  fns.emplace_back(std::make_unique<ConnectedFlow>(
      ConnectedFlow::Mode::kFollowingFlow, storage));
  fns.emplace_back(std::make_unique<ExperimentalAnnotatedStack>(context));
  fns.emplace_back(std::make_unique<ExperimentalFlatSlice>(context));
  fns.emplace_back(
      std::make_unique<DfsWeightBounded>(storage->mutable_string_pool()));

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  fns.emplace_back(std::make_unique<WinscopeProtoToArgsWithDefaults>(
      storage->mutable_string_pool(), engine, context));
#endif

  if (config.enable_dev_features) {
    fns.emplace_back(std::make_unique<DataframeQueryPlanDecoder>(
        storage->mutable_string_pool()));
  }
  return fns;
}

std::unique_ptr<PerfettoSqlEngine> TraceProcessorImpl::InitPerfettoSqlEngine(
    TraceProcessorContext* context,
    TraceStorage* storage,
    const Config& config,
    DataframeSharedStorage* dataframe_shared_storage,
    const std::vector<SqlPackage>& packages,
    std::vector<metrics::SqlMetricFile>& sql_metrics,
    const DescriptorPool* metrics_descriptor_pool,
    std::unordered_map<std::string, std::string>* proto_fn_name_to_path,
    TraceProcessor* trace_processor,
    bool notify_eof_called) {
  auto engine = std::make_unique<PerfettoSqlEngine>(
      storage->mutable_string_pool(), dataframe_shared_storage,
      config.enable_extra_checks);

  auto legacy_tables = GetLegacyStaticTables(storage);
  auto functions =
      CreateStaticTableFunctions(context, storage, config, engine.get());

  std::vector<PerfettoSqlEngine::UnfinalizedStaticTable> unfinalized =
      GetUnfinalizedStaticTables(storage);
  std::vector<PerfettoSqlEngine::FinalizedStaticTable> finalized;
  if (notify_eof_called) {
    // If EOF has already been called, all the unfinalized static tables
    // should have finalized handles in the shared storage. Look those up.
    for (auto& table : unfinalized) {
      auto handle = dataframe_shared_storage->Find(
          DataframeSharedStorage::MakeKeyForStaticTable(table.name));
      if (!handle) {
        PERFETTO_FATAL("Static table '%s' not found in shared storage.",
                       table.name.c_str());
      }
      finalized.emplace_back<PerfettoSqlEngine::FinalizedStaticTable>({
          std::move(*handle),
          std::move(table.name),
      });
    }
    // Clear the unfinalized tables as all of them have finalized counterparts.
    unfinalized.clear();
  }
  engine->InitializeStaticTablesAndFunctions(
      legacy_tables, unfinalized, std::move(finalized), std::move(functions));

  sqlite3* db = engine->sqlite_engine()->db();
  sqlite3_str_split_init(db);

  // Register SQL functions only used in local development instances.
  if (config.enable_dev_features) {
    RegisterFunction<WriteFile>(engine.get(), "WRITE_FILE", 2);
  }
  RegisterFunction<Glob>(engine.get(), "glob", 2);
  RegisterFunction<Hash>(engine.get(), "HASH", -1);
  RegisterFunction<Base64Encode>(engine.get(), "BASE64_ENCODE", 1);
  RegisterFunction<Demangle>(engine.get(), "DEMANGLE", 1);
  RegisterFunction<SourceGeq>(engine.get(), "SOURCE_GEQ", -1);
  RegisterFunction<TablePtrBind>(engine.get(), "__intrinsic_table_ptr_bind",
                                 -1);
  RegisterFunction<ExportJson>(engine.get(), "EXPORT_JSON", 1, storage, false);
  RegisterFunction<ExtractArg>(engine.get(), "EXTRACT_ARG", 2, storage);
  RegisterFunction<AbsTimeStr>(engine.get(), "ABS_TIME_STR", 1,
                               context->clock_converter.get());
  RegisterFunction<Reverse>(engine.get(), "REVERSE", 1);
  RegisterFunction<ToMonotonic>(engine.get(), "TO_MONOTONIC", 1,
                                context->clock_converter.get());
  RegisterFunction<ToRealtime>(engine.get(), "TO_REALTIME", 1,
                               context->clock_converter.get());
  RegisterFunction<ToTimecode>(engine.get(), "TO_TIMECODE", 1);
  RegisterFunction<CreateFunction>(engine.get(), "CREATE_FUNCTION", 3,
                                   engine.get());
  RegisterFunction<CreateViewFunction>(engine.get(), "CREATE_VIEW_FUNCTION", 3,
                                       engine.get());
  RegisterFunction<ExperimentalMemoize>(engine.get(), "EXPERIMENTAL_MEMOIZE", 1,
                                        engine.get());
  RegisterFunction<Import>(
      engine.get(), "IMPORT", 1,
      std::make_unique<Import::Context>(Import::Context{engine.get()}));
  RegisterFunction<ToFtrace>(engine.get(), "TO_FTRACE", 1,
                             std::make_unique<ToFtrace::Context>(context));

  if constexpr (regex::IsRegexSupported()) {
    RegisterFunction<Regex>(engine.get(), "regexp", 2);
  }
  // Old style function registration.
  // TODO(lalitm): migrate this over to using RegisterFunction once aggregate
  // functions are supported.
  RegisterValueAtMaxTsFunction(*engine);
  {
    base::Status status = RegisterLastNonNullFunction(*engine);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterStackFunctions(engine.get(), context);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterStripHexFunction(engine.get(), context);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = PprofFunctions::Register(*engine, context);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterLayoutFunctions(*engine);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterMathFunctions(*engine);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterBase64Functions(*engine);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterTypeBuilderFunctions(*engine);
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status =
        RegisterGraphScanFunctions(*engine, storage->mutable_string_pool());
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = RegisterGraphTraversalFunctions(
        *engine, *storage->mutable_string_pool());
    if (!status.ok())
      PERFETTO_FATAL("%s", status.c_message());
  }
  {
    base::Status status = perfetto_sql::RegisterIntervalIntersectFunctions(
        *engine, storage->mutable_string_pool());
  }
  {
    base::Status status = perfetto_sql::RegisterCounterIntervalsFunctions(
        *engine, storage->mutable_string_pool());
  }

  // Operator tables.
  engine->RegisterVirtualTableModule<SpanJoinOperatorModule>(
      "span_join",
      std::make_unique<SpanJoinOperatorModule::Context>(engine.get()));
  engine->RegisterVirtualTableModule<SpanJoinOperatorModule>(
      "span_left_join",
      std::make_unique<SpanJoinOperatorModule::Context>(engine.get()));
  engine->RegisterVirtualTableModule<SpanJoinOperatorModule>(
      "span_outer_join",
      std::make_unique<SpanJoinOperatorModule::Context>(engine.get()));
  engine->RegisterVirtualTableModule<WindowOperatorModule>("__intrinsic_window",
                                                           nullptr);
  engine->RegisterVirtualTableModule<CounterMipmapOperator>(
      "__intrinsic_counter_mipmap",
      std::make_unique<CounterMipmapOperator::Context>(engine.get()));
  engine->RegisterVirtualTableModule<SliceMipmapOperator>(
      "__intrinsic_slice_mipmap",
      std::make_unique<SliceMipmapOperator::Context>(engine.get()));
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  engine->RegisterVirtualTableModule<etm::EtmDecodeTraceVtable>(
      "__intrinsic_etm_decode_trace", storage);
  engine->RegisterVirtualTableModule<etm::EtmIterateRangeVtable>(
      "__intrinsic_etm_iterate_instruction_range", storage);
#endif

  // Register metrics functions.
  {
    base::Status status =
        engine->RegisterSqliteAggregateFunction<metrics::RepeatedField>(
            nullptr);
    if (!status.ok())
      PERFETTO_ELOG("%s", status.c_message());
  }

  RegisterFunction<metrics::NullIfEmpty>(engine.get(), "NULL_IF_EMPTY", 1);
  RegisterFunction<metrics::UnwrapMetricProto>(engine.get(),
                                               "UNWRAP_METRIC_PROTO", 2);
  RegisterFunction<metrics::RunMetric>(
      engine.get(), "RUN_METRIC", -1,
      std::make_unique<metrics::RunMetric::Context>(metrics::RunMetric::Context{
          engine.get(),
          &sql_metrics,
      }));

  // Legacy tables.
  engine->RegisterVirtualTableModule<SqlStatsModule>("sqlstats", storage);
  engine->RegisterVirtualTableModule<StatsModule>("stats", storage);
  engine->RegisterVirtualTableModule<TablePointerModule>(
      "__intrinsic_table_ptr", nullptr);

  // Value table aggregate functions.
  engine->RegisterSqliteAggregateFunction<DominatorTree>(
      storage->mutable_string_pool());
  engine->RegisterSqliteAggregateFunction<StructuralTreePartition>(
      storage->mutable_string_pool());

  // Metrics.
  {
    auto status = RegisterAllProtoBuilderFunctions(
        metrics_descriptor_pool, proto_fn_name_to_path, engine.get(),
        trace_processor);
    if (!status.ok()) {
      PERFETTO_FATAL("%s", status.c_message());
    }
  }

  // Reregister manually added stdlib packages.
  for (const auto& package : packages) {
    auto new_package = ToRegisteredPackage(package);
    if (!new_package.ok()) {
      PERFETTO_FATAL("%s", new_package.status().c_message());
    }
    engine->RegisterPackage(package.name, std::move(*new_package));
  }

  // Import prelude package.
  auto result = engine->Execute(SqlSource::FromTraceProcessorImplementation(
      "INCLUDE PERFETTO MODULE prelude.before_eof.*"));
  if (!result.status().ok()) {
    PERFETTO_FATAL("Failed to import prelude: %s", result.status().c_message());
  }

  if (notify_eof_called) {
    IncludeAfterEofPrelude(engine.get());
  }

  for (const auto& metric : sql_metrics) {
    if (metric.proto_field_name) {
      InsertIntoTraceMetricsTable(db, *metric.proto_field_name);
    }
  }

  // Fill trace bounds table.
  BuildBoundsTable(db, GetTraceTimestampBoundsNs(*storage));
  return engine;
}

void TraceProcessorImpl::IncludeAfterEofPrelude(PerfettoSqlEngine* engine) {
  auto result = engine->Execute(SqlSource::FromTraceProcessorImplementation(
      "INCLUDE PERFETTO MODULE prelude.after_eof.*"));
  if (!result.status().ok()) {
    PERFETTO_FATAL("Failed to import prelude: %s", result.status().c_message());
  }
}

bool TraceProcessorImpl::IsRootMetricField(const std::string& metric_name) {
  std::optional<uint32_t> desc_idx = metrics_descriptor_pool_.FindDescriptorIdx(
      ".perfetto.protos.TraceMetrics");
  if (!desc_idx.has_value())
    return false;
  const auto* field_idx =
      metrics_descriptor_pool_.descriptors()[*desc_idx].FindFieldByName(
          metric_name);
  return field_idx != nullptr;
}

}  // namespace perfetto::trace_processor
