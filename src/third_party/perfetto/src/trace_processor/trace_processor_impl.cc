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
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/io.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/summarizer.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_event_parser.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_reader.h"
#include "src/trace_processor/importers/android_bugreport/android_log_event_parser.h"
#include "src/trace_processor/importers/android_bugreport/android_log_reader.h"
#include "src/trace_processor/importers/archive/decompressing_trace_reader.h"
#include "src/trace_processor/importers/archive/tar_trace_reader.h"
#include "src/trace_processor/importers/archive/zip_trace_reader.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_parser.h"
#include "src/trace_processor/importers/art_method/art_method_tokenizer.h"
#include "src/trace_processor/importers/art_method/art_method_v2_tokenizer.h"
#include "src/trace_processor/importers/collapsed_stack/collapsed_stack_trace_reader.h"
#include "src/trace_processor/importers/common/builtin_trace_importers.h"
#include "src/trace_processor/importers/common/registered_file_tracker.h"
#include "src/trace_processor/importers/common/trace_diagnostics_tracker.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_parser.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_tokenizer.h"
#include "src/trace_processor/importers/gecko/gecko_trace_tokenizer.h"
#include "src/trace_processor/importers/json/json_trace_tokenizer.h"
#include "src/trace_processor/importers/ninja/ninja_log_parser.h"
#include "src/trace_processor/importers/perf/perf_data_tokenizer.h"
#include "src/trace_processor/importers/perf/record_parser.h"
#include "src/trace_processor/importers/perf/spe_record_parser.h"
#include "src/trace_processor/importers/pprof/pprof_trace_reader.h"
#include "src/trace_processor/importers/primes/primes_trace_tokenizer.h"
#include "src/trace_processor/importers/proto/additional_modules.h"
#include "src/trace_processor/importers/proto/deobfuscation_tracker.h"
#include "src/trace_processor/importers/proto/heap_graph_tracker.h"
#include "src/trace_processor/importers/proto/track_event_module.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_tokenizer.h"
#include "src/trace_processor/importers/systrace/systrace_trace_parser.h"
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"
#include "src/trace_processor/metrics/all_webview_metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.h"
#include "src/trace_processor/metrics/sql/amalgamated_sql_metrics.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/perfetto_sql/stdlib/stdlib.h"
#include "src/trace_processor/plugins/ancestor/ancestor.h"
#include "src/trace_processor/plugins/android_framework_track_event/android_framework_track_event.h"
#include "src/trace_processor/plugins/args/args.h"
#include "src/trace_processor/plugins/art_heap_graph_functions/art_heap_graph_functions.h"
#include "src/trace_processor/plugins/art_process_metadata_importer/art_process_metadata_importer.h"
#include "src/trace_processor/plugins/base64_functions/base64_functions.h"
#include "src/trace_processor/plugins/connected_flow/connected_flow.h"
#include "src/trace_processor/plugins/core_functions/core_functions.h"
#include "src/trace_processor/plugins/counter_intervals/counter_intervals.h"
#include "src/trace_processor/plugins/counter_mipmap_operator/counter_mipmap_operator.h"
#include "src/trace_processor/plugins/create_function/create_function.h"
#include "src/trace_processor/plugins/create_intervals/create_intervals.h"
#include "src/trace_processor/plugins/create_view_function/create_view_function.h"
#include "src/trace_processor/plugins/critical_path/critical_path.h"
#include "src/trace_processor/plugins/descendant/descendant.h"
#include "src/trace_processor/plugins/developer_functions/developer_functions.h"
#include "src/trace_processor/plugins/dfs_weight_bounded/dfs_weight_bounded.h"
#include "src/trace_processor/plugins/dominator_tree/dominator_tree.h"
#include "src/trace_processor/plugins/etm_decode_chunk/etm_decode_chunk.h"
#include "src/trace_processor/plugins/etm_iterate_range/etm_iterate_range.h"
#include "src/trace_processor/plugins/experimental_annotated_stack/experimental_annotated_stack.h"
#include "src/trace_processor/plugins/experimental_flamegraph/experimental_flamegraph.h"
#include "src/trace_processor/plugins/experimental_flat_slice/experimental_flat_slice.h"
#include "src/trace_processor/plugins/experimental_slice_layout/experimental_slice_layout.h"
#include "src/trace_processor/plugins/flamegraph/flamegraph_function.h"
#include "src/trace_processor/plugins/graph_scan/graph_scan.h"
#include "src/trace_processor/plugins/graph_traversal/graph_traversal.h"
#include "src/trace_processor/plugins/import/import.h"
#include "src/trace_processor/plugins/interval_intersect/interval_intersect.h"
#include "src/trace_processor/plugins/layout_functions/layout_functions.h"
#include "src/trace_processor/plugins/math_functions/math_functions.h"
#include "src/trace_processor/plugins/metadata/metadata.h"
#include "src/trace_processor/plugins/package_lookup/package_lookup.h"
#include "src/trace_processor/plugins/perf_counter/perf_counter.h"
#include "src/trace_processor/plugins/perf_text/perf_text.h"
#include "src/trace_processor/plugins/perfetto_manifest/perfetto_manifest.h"
#include "src/trace_processor/plugins/pprof_functions/pprof_functions.h"
#include "src/trace_processor/plugins/slice_mipmap_operator/slice_mipmap_operator.h"
#include "src/trace_processor/plugins/span_join_operator/span_join_operator.h"
#include "src/trace_processor/plugins/sql_stats_table/sql_stats_table.h"
#include "src/trace_processor/plugins/stack_functions/stack_functions.h"
#include "src/trace_processor/plugins/stack_sample_importer/plugin.h"
#include "src/trace_processor/plugins/stdlib_docs/stdlib_docs.h"
#include "src/trace_processor/plugins/storage_tables/storage_tables.h"
#include "src/trace_processor/plugins/strace/strace.h"
#include "src/trace_processor/plugins/string_functions/string_functions.h"
#include "src/trace_processor/plugins/structural_tree_partition/structural_tree_partition.h"
#include "src/trace_processor/plugins/symbolize/symbolize.h"
#include "src/trace_processor/plugins/table_info/table_info.h"
#include "src/trace_processor/plugins/table_pointer_module/table_pointer_module.h"
#include "src/trace_processor/plugins/time_functions/time_functions.h"
#include "src/trace_processor/plugins/to_ftrace/to_ftrace.h"
#include "src/trace_processor/plugins/trace_export/trace_export.h"
#include "src/trace_processor/plugins/tree_functions/tree_functions.h"
#include "src/trace_processor/plugins/type_builder_functions/type_builder_functions.h"
#include "src/trace_processor/plugins/utils_functions/utils_functions.h"
#include "src/trace_processor/plugins/video_frame_importer/video_frame_importer.h"
#include "src/trace_processor/plugins/wattson/wattson.h"
#include "src/trace_processor/plugins/window_operator/window_operator.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_export.h"
#include "src/trace_processor/sqlite_iterator_impl.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/trace_processor_storage_impl.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/trace_summary/summarizer.h"
#include "src/trace_processor/trace_summary/summary.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/gzip_decompressor.h"
#include "src/trace_processor/util/protozero_to_json.h"
#include "src/trace_processor/util/protozero_to_text.h"
#include "src/trace_processor/util/sql_bundle.h"
#include "src/trace_processor/util/sql_modules.h"
#include "src/trace_processor/util/trace_type.h"
#include "src/trace_processor/util/zstd_decompressor.h"

#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/perfetto/perfetto_metatrace.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#include "src/trace_processor/importers/instruments/instruments_xml_tokenizer.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
#include "src/trace_processor/importers/common/registered_file_tracker.h"
#include "src/trace_processor/importers/etm/etm_v4_stream_demultiplexer.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_tracker.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
#include "src/trace_processor/plugins/winscope_importer/winscope_importer.h"
#include "src/trace_processor/plugins/winscope_proto_to_args_with_defaults/winscope_proto_to_args_with_defaults.h"
#include "src/trace_processor/plugins/winscope_surfaceflinger_hierarchy_paths/winscope_surfaceflinger_hierarchy_paths.h"
#include "src/trace_processor/plugins/zstd_functions/zstd_functions.h"
#endif

namespace perfetto::trace_processor {
namespace {

base::Status RegisterAllProtoBuilderFunctions(
    const DescriptorPool* pool,
    std::unordered_map<std::string, std::string>* proto_fn_name_to_path,
    PerfettoSqlConnection* connection,
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
    PerfettoSqlConnection::RegisterFunctionArgs args(fn_name.c_str());
    auto status = connection->RegisterFunction<metrics::BuildProto>(
        std::make_unique<metrics::BuildProto::UserData>(
            metrics::BuildProto::UserData{tp, pool, i}),
        args);
    if (!status.ok()) {
      PERFETTO_FATAL("Failed to register %s function: %s", fn_name.c_str(),
                     status.c_message());
    }
    proto_fn_name_to_path->emplace(fn_name, desc.full_name());
  }
  return base::OkStatus();
}

void BuildBoundsTable(PerfettoSqlConnection* engine,
                      std::pair<int64_t, int64_t> bounds) {
  auto del = engine->Execute(
      SqlSource::FromTraceProcessorImplementation("DELETE FROM _trace_bounds"));
  if (!del.ok()) {
    PERFETTO_ELOG("Error deleting from bounds table: %s",
                  del.status().c_message());
    return;
  }
  base::StackString<1024> sql("INSERT INTO _trace_bounds VALUES(%" PRId64
                              ", %" PRId64 ")",
                              bounds.first, bounds.second);
  auto ins = engine->Execute(
      SqlSource::FromTraceProcessorImplementation(sql.ToStdString()));
  if (!ins.ok()) {
    PERFETTO_ELOG("Error inserting bounds table: %s", ins.status().c_message());
  }
}

// Module bodies are string_views into |package|; the caller must keep
// |package| alive for the lifetime of the result.
base::StatusOr<sql_modules::RegisteredPackage> ToRegisteredPackage(
    const SqlPackage& package) {
  const std::string& name = package.name;
  sql_modules::RegisteredPackage new_package;
  for (const auto& [module_name, sql] : package.modules) {
    if (!sql_modules::IsPackagePrefixOf(name, module_name) ||
        name == module_name) {
      return base::ErrStatus(
          "Module name '%s' must start with package name '%s.' as prefix.",
          module_name.c_str(), name.c_str());
    }
    new_package.modules.Insert(module_name, std::string_view(sql));
  }
  return base::StatusOr<sql_modules::RegisteredPackage>(std::move(new_package));
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

void InsertIntoTraceMetricsTable(PerfettoSqlConnection* engine,
                                 const std::string& metric_name) {
  auto s = engine->Execute(SqlSource::FromTraceProcessorImplementation(
                               "INSERT INTO _trace_metrics(name) VALUES(?)"),
                           {metric_name});
  if (!s.ok()) {
    PERFETTO_ELOG("Error registering table: %s", s.c_message());
  }
}

// Aggregates GetBoundsMutationCount across all plugins.
uint64_t AggregatePluginBoundsMutationCount(
    const std::vector<std::unique_ptr<PluginBase>>& plugins) {
  uint64_t total = 0;
  for (const auto& p : plugins) {
    total += p->GetBoundsMutationCount();
  }
  return total;
}

// Aggregates GetTimestampBounds across all plugins. Returns (0, 0) if no
// plugin reported any data; pins end to start+1 if the interval collapsed.
std::pair<int64_t, int64_t> AggregatePluginTimestampBounds(
    const std::vector<std::unique_ptr<PluginBase>>& plugins) {
  int64_t start_ns = std::numeric_limits<int64_t>::max();
  int64_t end_ns = 0;
  for (const auto& p : plugins) {
    auto b = p->GetTimestampBounds();
    start_ns = std::min(start_ns, b.first);
    end_ns = std::max(end_ns, b.second);
  }
  if (start_ns == std::numeric_limits<int64_t>::max()) {
    return {0, 0};
  }
  if (start_ns == end_ns) {
    end_ns += 1;
  }
  return {start_ns, end_ns};
}

// Normalization shared by ExecuteQuery and ExecuteNextStatement. The two must
// never diverge: statement-cursor offsets (trace_processor.h) are defined
// against this normalized form and are documented to match ExecuteQuery's.
std::string NormalizeExecuteQuerySql(const std::string& sql) {
  return base::ReplaceAll(sql, "\u00A0", " ");
}

}  // namespace

TraceProcessorImpl::TraceProcessorImpl(
    const Config& cfg,
    TraceProcessor::PlatformInterface* platform)
    : TraceProcessorStorageImpl(cfg, platform), config_(cfg) {
  // TODO(lalitm): plugins should self-register via PERFETTO_TP_REGISTER_PLUGIN
  // (a global static initializer). That's currently disabled due to build-time
  // issues, so instead each plugin exposes an explicit Register* function that
  // we call here before GetPluginSet() builds its cached set. Remove these
  // explicit calls once the static-init based registration is restored.
  ancestor::RegisterPlugin();
  android_framework_track_event::RegisterPlugin();
  args::RegisterPlugin();
  art_heap_graph_functions::RegisterPlugin();
  art_process_metadata_importer::RegisterPlugin();
  base64_functions::RegisterPlugin();
  connected_flow::RegisterPlugin();
  core_functions::RegisterPlugin();
  counter_intervals::RegisterPlugin();
  counter_mipmap_operator::RegisterPlugin();
  create_function::RegisterPlugin();
  create_intervals::RegisterPlugin();
  create_view_function::RegisterPlugin();
  critical_path::RegisterPlugin();
  descendant::RegisterPlugin();
  developer_functions::RegisterPlugin();
  dfs_weight_bounded::RegisterPlugin();
  dominator_tree::RegisterPlugin();
  etm_decode_chunk::RegisterPlugin();
  etm_iterate_range::RegisterPlugin();
  experimental_annotated_stack::RegisterPlugin();
  experimental_flamegraph::RegisterPlugin();
  experimental_flat_slice::RegisterPlugin();
  experimental_slice_layout::RegisterPlugin();
  flamegraph::RegisterPlugin();
  graph_scan::RegisterPlugin();
  graph_traversal::RegisterPlugin();
  import::RegisterPlugin();
  interval_intersect::RegisterPlugin();
  layout_functions::RegisterPlugin();
  math_functions::RegisterPlugin();
  metadata::RegisterPlugin();
  package_lookup::RegisterPlugin();
  perf_counter::RegisterPlugin();
  perf_text_importer::RegisterPlugin();
  perfetto_manifest::RegisterPlugin();
  pprof_functions::RegisterPlugin();
  slice_mipmap_operator::RegisterPlugin();
  span_join_operator::RegisterPlugin();
  sql_stats_table::RegisterPlugin();
  stack_functions::RegisterPlugin();
  stack_sample_importer::RegisterPlugin();
  stdlib_docs::RegisterPlugin();
  storage_tables::RegisterPlugin();
  strace_importer::RegisterPlugin();
  string_functions::RegisterPlugin();
  trace_export::RegisterPlugin();
  structural_tree_partition::RegisterPlugin();
  symbolize::RegisterPlugin();
  table_info::RegisterPlugin();
  table_pointer_module::RegisterPlugin();
  time_functions::RegisterPlugin();
  to_ftrace::RegisterPlugin();
  tree_functions::RegisterPlugin();
  type_builder_functions::RegisterPlugin();
  utils_functions::RegisterPlugin();
  video_frame_importer::RegisterPlugin();
  wattson::RegisterPlugin();
  window_operator::RegisterPlugin();
#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  winscope_importer::RegisterPlugin();
  winscope_proto_to_args_with_defaults::RegisterPlugin();
  winscope_surfaceflinger_hierarchy_paths::RegisterPlugin();
  zstd_functions::RegisterPlugin();
#endif

  // Initialize plugins using the statically pre-computed PluginSet.
  // Dep indices are resolved once at static init time; here we just
  // instantiate, resolve dep pointers, and register importers.
  {
    const PluginSet& pset = GetPluginSet();
    plugins_.reserve(pset.entries.size());
    for (const auto& pse : pset.entries) {
      plugins_.push_back(pse.factory());
    }
    for (size_t i = 0; i < pset.entries.size(); ++i) {
      auto& p = *plugins_[i];
      p.trace_context_ = context();
      for (size_t dep_idx : pset.entries[i].dep_indices) {
        p.resolved_deps_.push_back(plugins_[dep_idx].get());
      }
    }
    for (auto& p : plugins_) {
      p->RegisterImporters(*context()->reader_registry);
    }
    for (auto& p : plugins_) {
      p->RegisterDataframes(plugin_dataframes_);
    }
    for (auto& p : plugins_) {
      p->OnDataframesRegistered(plugin_dataframes_);
    }
  }
  context()->register_additional_proto_modules =
      [this](ProtoImporterModuleContext* mctx, TraceProcessorContext* tctx) {
        RegisterAdditionalModules(mctx, tctx);
        for (auto& p : plugins_) {
          p->RegisterProtoImporterModules(mctx, tctx);
        }
        // track_module is published by RegisterAdditionalModules above.
        if (mctx->track_module) {
          auto* ext_ctx =
              mctx->track_module->mutable_extension_parser_context();
          for (auto& p : plugins_) {
            p->RegisterTrackEventExtensions(ext_ctx, tctx);
          }
        }
      };

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_ETM_IMPORTER)
  context()->perf_aux_tokenizer_registrations.push_back(
      [](perf_importer::PerfTracker* pt) {
        pt->RegisterAuxTokenizer(PERF_AUXTRACE_CS_ETM,
                                 etm::CreateEtmV4StreamDemultiplexer);
      });
#endif
  // Identity, detection, metadata and reader creation all live on the importer;
  // gzip/zip/ctrace register unconditionally and report the disabled-zlib error
  // from their CreateReader.
  auto& reg = *context()->reader_registry;
  reg.Register(CreateAndroidDumpstateImporter());
  reg.Register(CreateAndroidLogcatImporter());
  reg.Register(CreateFuchsiaImporter());
  reg.Register(CreateSystraceImporter());
  reg.Register(CreateNinjaLogImporter());
  reg.Register(CreatePprofImporter());
  reg.Register(CreateCollapsedStackImporter());
  reg.Register(CreatePerfDataImporter());
#if PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
  reg.Register(CreateInstrumentsXmlImporter());
#endif
  reg.Register(CreateGzipImporter());
  reg.Register(CreateZstdImporter());
  reg.Register(CreateCtraceImporter());
  reg.Register(CreateZipImporter());
  reg.Register(CreateJsonImporter());
  reg.Register(CreateGeckoImporter());
  reg.Register(CreateArtMethodImporter());
  reg.Register(CreateArtMethodV2Importer());
  reg.Register(CreateArtHprofImporter());
  reg.Register(CreateSimpleperfProtoImporter());
  reg.Register(CreateTarImporter());
  reg.Register(CreatePrimesImporter());

  // Force initialization of heap graph tracker.
  //
  // TODO(lalitm): remove heap graph tracker from global context and get rid
  // of this.
  context()->heap_graph_tracker = std::make_unique<HeapGraphTracker>(
      context()->storage.get(), context()->global_stats_tracker.get());

  // Initialize deobfuscation tracker.
  context()->deobfuscation_tracker =
      std::make_unique<DeobfuscationTracker>(context());

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
    base::Status status = context()->descriptor_pool_->AddFromFileDescriptorSet(
        kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size());
    PERFETTO_CHECK(status.ok());
  }

  // Compute initial trace bounds before any tables are finalized.
  cached_trace_bounds_ = AggregatePluginTimestampBounds(plugins_);

  engine_ = InitPerfettoSqlConnection({
      context(),
      context()->storage.get(),
      config_,
      registered_sql_packages_,
      sql_metrics_,
      &metrics_descriptor_pool_,
      &proto_fn_name_to_path_,
      this,
      notify_eof_called_,
      cached_trace_bounds_,
      plugins_,
      plugin_dataframes_,
  });

  sqlite_objects_post_prelude_ = engine_->SqliteRegisteredObjectCount();

  bool skip_all_sql = std::find(config_.skip_builtin_metric_paths.begin(),
                                config_.skip_builtin_metric_paths.end(),
                                "") != config_.skip_builtin_metric_paths.end();
  if (!skip_all_sql) {
    // Wrap the per-metric INSERTs in one transaction; otherwise SQLite
    // implicitly commits after each statement.
    PerfettoSqlConnection::Transaction txn(engine_.get());
    for (const auto& file_to_sql :
         SqlBundle(sql_metrics::kAmalgamatedSqlMetrics)) {
      if (base::StartsWithAny(file_to_sql.path, sanitized_extension_paths))
        continue;
      RegisterMetricImpl(file_to_sql.path, std::string(file_to_sql.sql_view()));
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
  CacheBoundsAndBuildTable();
}

base::Status TraceProcessorImpl::NotifyEndOfFile() {
  if (notify_eof_called_) {
    constexpr char kMessage[] =
        "NotifyEndOfFile should only be called once. Try calling Flush instead "
        "if trying to commit the contents of the trace to tables.";
    PERFETTO_ELOG(kMessage);
    return base::ErrStatus(kMessage);
  }
  eof_ = true;
  notify_eof_called_ = true;

  if (current_trace_name_.empty()) {
    current_trace_name_ = "Unnamed trace";
  }

  // Note: we very intentionally do not call
  // TraceProcessorStorageImpl::NotifyEndOfFile as we have very special
  // ordering requirements on how we need to push data to the sorter and
  // finalize trackers. In any case, all logic of TraceProcessorStorage
  // is confined to OnPushDataToSorter and OnEventsFullyExtracted,
  // so we can just call those directly here.

  // Stage 1: push all data to the sorter
  RETURN_IF_ERROR(TraceProcessorStorageImpl::OnPushDataToSorter());

  // Stage 2: finalize all data.
  HeapGraphTracker::Get(context())->FinalizeAllProfiles();
  TraceProcessorStorageImpl::OnEventsFullyExtracted();
  DeobfuscationTracker::Get(context())->OnEventsFullyExtracted();
  CacheBoundsAndBuildTable();

  // Run trace-config diagnostics before the parser context is destroyed (rules
  // may read metadata/clocks off the context). Rules are per-(trace, machine),
  // so loop the fork map like OnEventsFullyExtracted does.
  auto& diag_contexts =
      context()->forked_context_state->trace_and_machine_to_context;
  for (auto it = diag_contexts.GetIterator(); it; ++it) {
    if (it.value()->trace_diagnostics_tracker) {
      it.value()->trace_diagnostics_tracker->RunRules();
    }
  }

  // Stage 3: reduce memory usage by both destroying parser context *and*
  // finalizing dataframes; once finalized, attach any indexes that were
  // declared at registration time.
  TraceProcessorStorageImpl::DestroyContext();
  for (const auto& df : plugin_dataframes_) {
    df.dataframe->Finalize();
    for (const auto& idx_col_names : df.indexes) {
      std::vector<uint32_t> col_idxs;
      col_idxs.reserve(idx_col_names.size());
      const auto& col_names = df.dataframe->column_names();
      for (const auto& name : idx_col_names) {
        auto it = std::find(col_names.begin(), col_names.end(), name);
        PERFETTO_CHECK(it != col_names.end());
        col_idxs.push_back(
            static_cast<uint32_t>(std::distance(col_names.begin(), it)));
      }
      auto idx_or = df.dataframe->BuildIndex(col_idxs.data(),
                                             col_idxs.data() + col_idxs.size());
      PERFETTO_CHECK(idx_or.ok());
      *df.dataframe = df.dataframe->AddIndex(std::move(*idx_or));
    }
  }

  // Stage 4: prepare the connection for queries.
  IncludeAfterEofPrelude(engine_.get());
  sqlite_objects_post_prelude_ = engine_->SqliteRegisteredObjectCount();

  return base::OkStatus();
}

void TraceProcessorImpl::CacheBoundsAndBuildTable() {
  uint64_t mutations = AggregatePluginBoundsMutationCount(plugins_);
  if (mutations == bounds_tables_mutations_) {
    return;
  }
  bounds_tables_mutations_ = mutations;
  cached_trace_bounds_ = AggregatePluginTimestampBounds(plugins_);
  BuildBoundsTable(engine_.get(), cached_trace_bounds_);
}

// =================================================================
// |        PerfettoSQL related functionality starts here          |
// =================================================================

Iterator TraceProcessorImpl::ExecuteQuery(const std::string& sql) {
  PERFETTO_TP_TRACE(metatrace::Category::API_TIMELINE, "EXECUTE_QUERY",
                    [&](metatrace::Record* r) { r->AddArg("query", sql); });

  uint32_t sql_stats_row =
      context()->storage->mutable_sql_stats()->RecordQueryBegin(
          sql, base::GetWallTimeNs().count());
  std::string non_breaking_sql = NormalizeExecuteQuerySql(sql);
  base::StatusOr<PerfettoSqlConnection::ExecutionResult> result =
      engine_->ExecuteUntilLastStatement(
          SqlSource::FromExecuteQuery(std::move(non_breaking_sql)));
  return Iterator(std::make_unique<SqliteIteratorImpl>(this, std::move(result),
                                                       sql_stats_row));
}

std::optional<Iterator> TraceProcessorImpl::ExecuteNextStatement(
    const std::string& sql,
    uint32_t* offset) {
  PERFETTO_CHECK(offset);
  PERFETTO_TP_TRACE(metatrace::Category::API_TIMELINE, "EXECUTE_NEXT_STATEMENT",
                    [&](metatrace::Record* r) { r->AddArg("query", sql); });

  // Offsets are relative to the normalized SQL: since the normalization is
  // deterministic, an offset returned by one call stays valid when the same
  // |sql| is passed back.
  std::string non_breaking_sql = NormalizeExecuteQuerySql(sql);
  auto size = static_cast<uint32_t>(non_breaking_sql.size());
  uint32_t start = *offset;

  uint32_t sql_stats_row =
      context()->storage->mutable_sql_stats()->RecordQueryBegin(
          non_breaking_sql.substr(std::min(start, size)),
          base::GetWallTimeNs().count());
  // A soft error rather than a CHECK: over the RPC protocol the offset comes
  // from an untrusted client, which must not be able to crash the server.
  if (start > size) {
    return Iterator(std::make_unique<SqliteIteratorImpl>(
        this,
        base::ErrStatus(
            "ExecuteNextStatement: offset %u out of range (SQL size %u)", start,
            size),
        sql_stats_row));
  }
  SqlSource source = SqlSource::FromExecuteQuery(std::move(non_breaking_sql));

  uint32_t end_offset = 0;
  base::StatusOr<std::optional<PerfettoSqlConnection::ExecutionResult>> result =
      engine_->ExecuteNextStatement(source.Substr(start, size - start),
                                    &end_offset);
  if (!result.ok()) {
    return Iterator(std::make_unique<SqliteIteratorImpl>(this, result.status(),
                                                         sql_stats_row));
  }
  if (!result->has_value()) {
    // No iterator will be created to close the sql_stats entry; do it here.
    int64_t now = base::GetWallTimeNs().count();
    auto* sql_stats = context()->storage->mutable_sql_stats();
    sql_stats->RecordQueryFirstNext(sql_stats_row, now);
    sql_stats->RecordQueryEnd(sql_stats_row, now);
    *offset = size;
    return std::nullopt;
  }
  *offset = start + end_offset;
  return Iterator(std::make_unique<SqliteIteratorImpl>(
      this, std::move(**result), sql_stats_row));
}

base::Status TraceProcessorImpl::RegisterSqlPackage(SqlPackage sql_package) {
  const std::string& name = sql_package.name;

  // Same-name gate. The engine holds both stdlib and previously-registered
  // user packages, so a single lookup covers both.
  if (engine_->FindPackage(name) != nullptr && !sql_package.allow_override) {
    return base::ErrStatus(
        "Package '%s' is already registered. Choose a different name.\n"
        "If you want to replace the existing package using trace processor "
        "shell, you need to pass the --dev flag and use "
        "--override-sql-package to pass the module path.",
        name.c_str());
  }

  // Validate module names. The returned string_views point into |sql_package|
  // and stay valid through the std::move below: std::vector's move ctor
  // preserves element addresses.
  ASSIGN_OR_RETURN(auto new_package, ToRegisteredPackage(sql_package));

  auto it = std::find_if(registered_sql_packages_.begin(),
                         registered_sql_packages_.end(),
                         [&](const SqlPackage& p) { return p.name == name; });
  if (it != registered_sql_packages_.end()) {
    registered_sql_packages_.erase(it);
    engine_->ErasePackage(name);
  }
  std::string pkg_name = name;
  registered_sql_packages_.emplace_back(std::move(sql_package));
  return engine_->RegisterPackage(pkg_name, std::move(new_package));
}

// =================================================================
// |  Trace-based metrics (v2) related functionality starts here   |
// =================================================================

base::Status TraceProcessorImpl::Summarize(
    const TraceSummaryComputationSpec& computation,
    const std::vector<TraceSummarySpecBytes>& specs,
    std::vector<uint8_t>* output,
    const TraceSummaryOutputSpec& output_spec) {
  return summary::Summarize(this, *context()->descriptor_pool_, computation,
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

base::Status TraceProcessorImpl::RegisterFileContent(const std::string& path,
                                                     TraceBlob content) {
  return context_.registered_file_tracker->AddFile(path, std::move(content));
}

void TraceProcessorImpl::InterruptQuery() {
  if (!engine_->sqlite_connection()->db())
    return;
  query_interrupted_.store(true);
  sqlite3_interrupt(engine_->sqlite_connection()->db());
}

size_t TraceProcessorImpl::RestoreInitialTables() {
  // We should always have at least as many objects now as we did in the
  // constructor.
  uint64_t registered_count_before = engine_->SqliteRegisteredObjectCount();
  PERFETTO_CHECK(registered_count_before >= sqlite_objects_post_prelude_);

  // Reset the connection (and the database it owns) to its initial state.
  // Pass cached bounds to avoid recomputing them.
  engine_ = InitPerfettoSqlConnection({
      context(),
      context()->storage.get(),
      config_,
      registered_sql_packages_,
      sql_metrics_,
      &metrics_descriptor_pool_,
      &proto_fn_name_to_path_,
      this,
      notify_eof_called_,
      cached_trace_bounds_,
      plugins_,
      plugin_dataframes_,
  });

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
  return RegisterMetricImpl(path, sql);
}

base::Status TraceProcessorImpl::RegisterMetricImpl(std::string path,
                                                    std::string sql) {
  // Check if the metric with the given path already exists and if it does,
  // just update the SQL associated with it.
  auto it = std::find_if(
      sql_metrics_.begin(), sql_metrics_.end(),
      [&path](const metrics::SqlMetricFile& m) { return m.path == path; });
  if (it != sql_metrics_.end()) {
    it->sql = std::move(sql);
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

  if (IsRootMetricField(no_ext_name)) {
    metric.proto_field_name = no_ext_name;
    metric.output_table_name = no_ext_name + "_output";

    // proto_field_to_sql_metric_path_ stores |path| by value: this is the
    // one unavoidable copy on the root-metric path.
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
    InsertIntoTraceMetricsTable(engine_.get(), *metric.proto_field_name);
  }
  metric.path = std::move(path);
  metric.sql = std::move(sql);
  sql_metrics_.emplace_back(std::move(metric));
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

std::unique_ptr<PerfettoSqlConnection>
TraceProcessorImpl::InitPerfettoSqlConnection(
    const InitPerfettoSqlConnectionArgs& args) {
  auto* storage = args.storage;
  const auto& config = args.config;
  const auto& packages = args.packages;
  auto& sql_metrics = args.sql_metrics;
  const auto* metrics_descriptor_pool = args.metrics_descriptor_pool;
  auto* proto_fn_name_to_path = args.proto_fn_name_to_path;
  auto* trace_processor = args.trace_processor;
  bool notify_eof_called = args.notify_eof_called;
  auto cached_trace_bounds = args.cached_trace_bounds;
  const auto& plugins = args.plugins;
  const auto& plugin_dataframes = args.plugin_dataframes;

  auto connection = PerfettoSqlConnection::CreateConnectionToNewDatabase(
      storage->mutable_string_pool(), config.enable_extra_checks);

  PerfettoSqlConnection::Initializer init;
  init.static_tables.reserve(plugin_dataframes.size());
  for (const auto& df : plugin_dataframes) {
    init.static_tables.push_back({df.dataframe, df.name});
  }

  // Plugin contributions.
  for (auto& p : plugins) {
    p->RegisterStaticTableFunctions(connection.get(),
                                    init.static_table_functions);
    p->RegisterSqliteModules(connection.get(), init.sqlite_modules);
    p->RegisterFunctions(connection.get(), init.functions);
    p->RegisterAggregateFunctions(connection.get(), init.aggregate_functions);
    p->RegisterWindowFunctions(connection.get(), init.window_functions);
  }

  // Carve-outs that don't fit cleanly in a plugin:
  // - metrics::RunMetric needs &sql_metrics (a TraceProcessorImpl member).
  // - metrics aggregates / NullIfEmpty / UnwrapMetricProto belong to the
  //   trace_processor library, not the plugin set.
  // - Proto-builder functions are descriptor-pool-driven and registered
  //   dynamically per-connection from the live descriptor pool.
  // - Virtual table modules whose context is the live connection itself
  //   (span_join variants, mipmap operators) can only be wired here.
  init.functions.push_back(
      MakeFunctionRegistration<metrics::NullIfEmpty>(nullptr));
  init.functions.push_back(
      MakeFunctionRegistration<metrics::UnwrapMetricProto>(nullptr));
  init.functions.push_back(MakeFunctionRegistration<metrics::RunMetric>(
      std::make_unique<metrics::RunMetric::UserData>(
          metrics::RunMetric::UserData{connection.get(), &sql_metrics})));
  init.aggregate_functions.push_back(
      MakeAggregateRegistration<metrics::RepeatedField>(nullptr));

  connection->Initialize(std::move(init));

  // Proto-builder registrations are descriptor-pool-driven and don't fit
  // the data-only Initializer shape; register them directly after Initialize.
  {
    auto s = RegisterAllProtoBuilderFunctions(
        metrics_descriptor_pool, proto_fn_name_to_path, connection.get(),
        trace_processor);
    if (!s.ok()) {
      PERFETTO_FATAL("%s", s.c_message());
    }
  }

  // Stream stdlib straight from rodata into the connection. The bundle is
  // sorted by path so package entries are contiguous: flush whenever the
  // package name changes. Bodies stay as string_views into kStdlib.
  {
    auto flush = [&](const std::string& pkg,
                     sql_modules::RegisteredPackage rp) {
      auto status = connection->RegisterPackage(pkg, std::move(rp));
      if (!status.ok()) {
        PERFETTO_FATAL("%s", status.c_message());
      }
    };
    std::string current_pkg;
    std::optional<sql_modules::RegisteredPackage> rp;
    for (const auto& f : SqlBundle(stdlib::kStdlib)) {
      std::string include_key = sql_modules::GetIncludeKey(f.path);
      std::string pkg = sql_modules::GetPackageName(include_key);
      if (pkg != current_pkg) {
        if (rp.has_value()) {
          flush(current_pkg, *std::move(rp));
        }
        rp = sql_modules::RegisteredPackage();
        current_pkg = std::move(pkg);
      }
      rp->modules.Insert(std::move(include_key), f.sql_view());
    }
    if (rp.has_value()) {
      flush(current_pkg, *std::move(rp));
    }
  }

  // Re-register user-added packages.
  for (const auto& package : packages) {
    auto new_package = ToRegisteredPackage(package);
    if (!new_package.ok()) {
      PERFETTO_FATAL("%s", new_package.status().c_message());
    }
    auto status =
        connection->RegisterPackage(package.name, std::move(*new_package));
    if (!status.ok()) {
      PERFETTO_FATAL("%s", status.c_message());
    }
  }

  // Import prelude package.
  auto result = connection->Execute(SqlSource::FromTraceProcessorImplementation(
      "INCLUDE PERFETTO MODULE prelude.before_eof.*"));
  if (!result.status().ok()) {
    PERFETTO_FATAL("Failed to import prelude: %s", result.status().c_message());
  }

  if (notify_eof_called) {
    IncludeAfterEofPrelude(connection.get());
  }

  // Same batching as the constructor: wrap the per-metric INSERTs in one
  // transaction.
  {
    PerfettoSqlConnection::Transaction txn(connection.get());
    for (const auto& metric : sql_metrics) {
      if (metric.proto_field_name) {
        InsertIntoTraceMetricsTable(connection.get(), *metric.proto_field_name);
      }
    }
  }
  BuildBoundsTable(connection.get(), cached_trace_bounds);
  return connection;
}

void TraceProcessorImpl::IncludeAfterEofPrelude(
    PerfettoSqlConnection* connection) {
  auto result = connection->Execute(SqlSource::FromTraceProcessorImplementation(
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

// =================================================================
// |                        Summarizer                              |
// =================================================================

base::Status TraceProcessorImpl::CreateSummarizer(
    std::unique_ptr<Summarizer>* out) {
  // Lazily initialize the descriptor pool for textproto generation.
  auto opt_idx = metrics_descriptor_pool_.FindDescriptorIdx(
      ".perfetto.protos.TraceSummarySpec");
  if (!opt_idx) {
    metrics_descriptor_pool_.AddFromFileDescriptorSet(
        kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size());
  }

  // Auto-generate a unique id for table namespacing. The id is embedded in
  // SQL table names (e.g. "_exp_mat_{id}_{seq}") to prevent collisions
  // between multiple summarizer instances.
  std::string id = std::to_string(next_summarizer_id_++);
  *out = std::make_unique<summary::SummarizerImpl>(
      this, &metrics_descriptor_pool_, std::move(id));
  return base::OkStatus();
}

base::Status TraceProcessorImpl::Export(ExportFormat format,
                                        ExportOutput* output) {
  if (!output) {
    return base::ErrStatus("Export output is null");
  }
  if (format == ExportFormat::kSqlite) {
    std::optional<std::string> path = output->GetFilePath();
    if (!path) {
      return base::ErrStatus("SQLite export requires a file path");
    }
    // SQLite export is an explicit API call (not reachable from SQL), so it is
    // not gated behind Config::enable_sql_file_access. Embedders without a
    // filesystem, such as the RPC server and Wasm, fail here cleanly.
    // The filesystem is borrowed at construction and must outlive this
    // instance.
    io::FileSystem* file_system = context()->file_system;
    if (!file_system) {
      return base::ErrStatus("SQLite export requires a file system");
    }
    return ExportSqliteDatabase(this, file_system, *path);
  }
  return trace_export::WriteExport(
      plugin_dataframes_, context()->storage->string_pool(), format, output);
}

}  // namespace perfetto::trace_processor
