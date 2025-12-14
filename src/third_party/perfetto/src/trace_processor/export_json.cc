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

#include "perfetto/ext/trace_processor/export_json.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/export_json.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/trace_processor_storage_impl.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/args_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
#include <json/config.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#endif

namespace perfetto::trace_processor::json {

namespace {

class FileWriter : public OutputWriter {
 public:
  explicit FileWriter(FILE* file) : file_(file) {}
  ~FileWriter() override { fflush(file_); }

  base::Status AppendString(const std::string& s) override {
    size_t written =
        fwrite(s.data(), sizeof(std::string::value_type), s.size(), file_);
    if (written != s.size())
      return base::ErrStatus("Error writing to file: %d", ferror(file_));
    return base::OkStatus();
  }

 private:
  FILE* file_;
};

template <typename T, typename Compare>
uint32_t LowerBoundIndex(uint32_t first,
                         uint32_t last,
                         const T& value,
                         Compare comp) {
  uint32_t count = last - first;
  uint32_t step;
  while (count > 0) {
    step = count / 2;
    uint32_t current = first + step;
    if (comp(current, value)) {
      first = current + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first;
}

#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
using IndexMap = perfetto::trace_processor::TraceStorage::Stats::IndexMap;

const char kLegacyEventArgsKey[] = "legacy_event";
const char kLegacyEventPassthroughUtidKey[] = "passthrough_utid";
const char kLegacyEventCategoryKey[] = "category";
const char kLegacyEventNameKey[] = "name";
const char kLegacyEventPhaseKey[] = "phase";
const char kLegacyEventDurationNsKey[] = "duration_ns";
const char kLegacyEventThreadTimestampNsKey[] = "thread_timestamp_ns";
const char kLegacyEventThreadDurationNsKey[] = "thread_duration_ns";
const char kLegacyEventThreadInstructionCountKey[] = "thread_instruction_count";
const char kLegacyEventThreadInstructionDeltaKey[] = "thread_instruction_delta";
const char kLegacyEventUseAsyncTtsKey[] = "use_async_tts";
const char kLegacyEventUnscopedIdKey[] = "unscoped_id";
const char kLegacyEventGlobalIdKey[] = "global_id";
const char kLegacyEventLocalIdKey[] = "local_id";
const char kLegacyEventIdScopeKey[] = "id_scope";
const char kStrippedArgument[] = "__stripped__";

const char* GetNonNullString(const TraceStorage* storage,
                             std::optional<StringId> id) {
  return id == std::nullopt || *id == kNullStringId
             ? ""
             : storage->GetString(*id).c_str();
}

class JsonExporter {
 public:
  JsonExporter(const TraceStorage* storage,
               OutputWriter* output,
               ArgumentFilterPredicate argument_filter,
               MetadataFilterPredicate metadata_filter,
               LabelFilterPredicate label_filter)
      : storage_(storage),
        args_builder_(storage_),
        writer_(output,
                std::move(argument_filter),
                std::move(metadata_filter),
                std::move(label_filter)) {}

  base::Status Export() {
    RETURN_IF_ERROR(MapUniquePidsAndTids());
    RETURN_IF_ERROR(ExportThreadNames());
    RETURN_IF_ERROR(ExportProcessNames());
    RETURN_IF_ERROR(ExportProcessUptimes());
    RETURN_IF_ERROR(ExportSlices());
    RETURN_IF_ERROR(ExportFlows());
    RETURN_IF_ERROR(ExportRawEvents());
    RETURN_IF_ERROR(ExportMetadata());
    RETURN_IF_ERROR(ExportStats());
    RETURN_IF_ERROR(ExportMemorySnapshots());
    return base::OkStatus();
  }

 private:
  class TraceFormatWriter {
   public:
    TraceFormatWriter(OutputWriter* output,
                      ArgumentFilterPredicate argument_filter,
                      MetadataFilterPredicate metadata_filter,
                      LabelFilterPredicate label_filter)
        : output_(output),
          argument_filter_(std::move(argument_filter)),
          metadata_filter_(std::move(metadata_filter)),
          label_filter_(std::move(label_filter)),
          first_event_(true) {
      Json::StreamWriterBuilder b;
      b.settings_["indentation"] = "";
      writer_.reset(b.newStreamWriter());
      WriteHeader();
    }

    ~TraceFormatWriter() { WriteFooter(); }

    void WriteCommonEvent(const Json::Value& event) {
      if (label_filter_ && !label_filter_("traceEvents"))
        return;

      DoWriteEvent(event);
    }

    void AddAsyncBeginEvent(const Json::Value& event) {
      if (label_filter_ && !label_filter_("traceEvents"))
        return;

      async_begin_events_.push_back(event);
    }

    void AddAsyncInstantEvent(const Json::Value& event) {
      if (label_filter_ && !label_filter_("traceEvents"))
        return;

      async_instant_events_.push_back(event);
    }

    void AddAsyncEndEvent(const Json::Value& event) {
      if (label_filter_ && !label_filter_("traceEvents"))
        return;

      async_end_events_.push_back(event);
    }

    void SortAndEmitAsyncEvents() {
      // Catapult doesn't handle out-of-order begin/end events well, especially
      // when their timestamps are the same, but their order is incorrect. Since
      // we process events sorted by begin timestamp, |async_begin_events_| and
      // |async_instant_events_| are already sorted. We now only have to sort
      // |async_end_events_| and merge-sort all events into a single sequence.

      // Sort |async_end_events_|. Note that we should order by ascending
      // timestamp, but in reverse-stable order. This way, a child slices's end
      // is emitted before its parent's end event, even if both end events have
      // the same timestamp. To accomplish this, we perform a stable sort in
      // descending order and later iterate via reverse iterators.
      struct {
        bool operator()(const Json::Value& a, const Json::Value& b) const {
          return a["ts"].asInt64() > b["ts"].asInt64();
        }
      } CompareEvents;
      std::stable_sort(async_end_events_.begin(), async_end_events_.end(),
                       CompareEvents);

      // Merge sort by timestamp. If events share the same timestamp, prefer
      // instant events, then end events, so that old slices close before new
      // ones are opened, but instant events remain in their deepest nesting
      // level.
      auto instant_event_it = async_instant_events_.begin();
      auto end_event_it = async_end_events_.rbegin();
      auto begin_event_it = async_begin_events_.begin();

      auto has_instant_event = instant_event_it != async_instant_events_.end();
      auto has_end_event = end_event_it != async_end_events_.rend();
      auto has_begin_event = begin_event_it != async_begin_events_.end();

      auto emit_next_instant = [&instant_event_it, &has_instant_event, this]() {
        DoWriteEvent(*instant_event_it);
        instant_event_it++;
        has_instant_event = instant_event_it != async_instant_events_.end();
      };
      auto emit_next_end = [&end_event_it, &has_end_event, this]() {
        DoWriteEvent(*end_event_it);
        end_event_it++;
        has_end_event = end_event_it != async_end_events_.rend();
      };
      auto emit_next_begin = [&begin_event_it, &has_begin_event, this]() {
        DoWriteEvent(*begin_event_it);
        begin_event_it++;
        has_begin_event = begin_event_it != async_begin_events_.end();
      };

      auto emit_next_instant_or_end = [&instant_event_it, &end_event_it,
                                       &emit_next_instant, &emit_next_end]() {
        if ((*instant_event_it)["ts"].asInt64() <=
            (*end_event_it)["ts"].asInt64()) {
          emit_next_instant();
        } else {
          emit_next_end();
        }
      };
      auto emit_next_instant_or_begin = [&instant_event_it, &begin_event_it,
                                         &emit_next_instant,
                                         &emit_next_begin]() {
        if ((*instant_event_it)["ts"].asInt64() <=
            (*begin_event_it)["ts"].asInt64()) {
          emit_next_instant();
        } else {
          emit_next_begin();
        }
      };
      auto emit_next_end_or_begin = [&end_event_it, &begin_event_it,
                                     &emit_next_end, &emit_next_begin]() {
        if ((*end_event_it)["ts"].asInt64() <=
            (*begin_event_it)["ts"].asInt64()) {
          emit_next_end();
        } else {
          emit_next_begin();
        }
      };

      // While we still have events in all iterators, consider each.
      while (has_instant_event && has_end_event && has_begin_event) {
        if ((*instant_event_it)["ts"].asInt64() <=
            (*end_event_it)["ts"].asInt64()) {
          emit_next_instant_or_begin();
        } else {
          emit_next_end_or_begin();
        }
      }

      // Only instant and end events left.
      while (has_instant_event && has_end_event) {
        emit_next_instant_or_end();
      }

      // Only instant and begin events left.
      while (has_instant_event && has_begin_event) {
        emit_next_instant_or_begin();
      }

      // Only end and begin events left.
      while (has_end_event && has_begin_event) {
        emit_next_end_or_begin();
      }

      // Remaining instant events.
      while (has_instant_event) {
        emit_next_instant();
      }

      // Remaining end events.
      while (has_end_event) {
        emit_next_end();
      }

      // Remaining begin events.
      while (has_begin_event) {
        emit_next_begin();
      }
    }

    void WriteMetadataEvent(const char* metadata_type,
                            const char* metadata_arg_name,
                            const char* metadata_arg_value,
                            int64_t pid,
                            int64_t tid) {
      if (label_filter_ && !label_filter_("traceEvents"))
        return;

      std::ostringstream ss;
      if (!first_event_)
        ss << ",\n";

      Json::Value value;
      value["ph"] = "M";
      value["cat"] = "__metadata";
      value["ts"] = 0;
      value["name"] = metadata_type;
      value["pid"] = Json::Int(pid);
      value["tid"] = Json::Int(tid);

      Json::Value args;
      args[metadata_arg_name] = metadata_arg_value;
      value["args"] = args;

      writer_->write(value, &ss);
      output_->AppendString(ss.str());
      first_event_ = false;
    }

    void MergeMetadata(const Json::Value& value) {
      for (const auto& member : value.getMemberNames()) {
        metadata_[member] = value[member];
      }
    }

    void WriteTraceConfigString(const char* value) {
      metadata_["trace-config"] = value;
    }

    void AppendTelemetryMetadataString(const char* key, const char* value) {
      metadata_["telemetry"][key].append(value);
    }

    void AppendTelemetryMetadataInt(const char* key, int64_t value) {
      metadata_["telemetry"][key].append(Json::Int64(value));
    }

    void AppendTelemetryMetadataBool(const char* key, bool value) {
      metadata_["telemetry"][key].append(value);
    }

    void SetTelemetryMetadataTimestamp(const char* key, int64_t value) {
      metadata_["telemetry"][key] = static_cast<double>(value) / 1000.0;
    }

    void SetStats(const char* key, int64_t value) {
      metadata_["trace_processor_stats"][key] = Json::Int64(value);
    }

    void SetStats(const char* key, const IndexMap& indexed_values) {
      constexpr const char* kBufferStatsPrefix = "traced_buf_";

      // Stats for the same buffer should be grouped together in the JSON.
      if (strncmp(kBufferStatsPrefix, key, strlen(kBufferStatsPrefix)) == 0) {
        for (const auto& value : indexed_values) {
          metadata_["trace_processor_stats"]["traced_buf"][value.first]
                   [key + strlen(kBufferStatsPrefix)] =
                       Json::Int64(value.second);
        }
        return;
      }

      // Other indexed value stats are exported as array under their key.
      for (const auto& value : indexed_values) {
        metadata_["trace_processor_stats"][key][value.first] =
            Json::Int64(value.second);
      }
    }

    void AddSystemTraceData(const std::string& data) {
      system_trace_data_ += data;
    }

    void AddUserTraceData(const std::string& data) {
      if (user_trace_data_.empty())
        user_trace_data_ = "[";
      user_trace_data_ += data;
    }

   private:
    void WriteHeader() {
      if (!label_filter_)
        output_->AppendString("{\"traceEvents\":[\n");
    }

    void WriteFooter() {
      SortAndEmitAsyncEvents();

      // Filter metadata entries.
      if (metadata_filter_) {
        for (const auto& member : metadata_.getMemberNames()) {
          if (!metadata_filter_(member.c_str()))
            metadata_[member] = kStrippedArgument;
        }
      }

      if ((!label_filter_ || label_filter_("traceEvents")) &&
          !user_trace_data_.empty()) {
        user_trace_data_ += "]";

        Json::CharReaderBuilder builder;
        auto reader =
            std::unique_ptr<Json::CharReader>(builder.newCharReader());
        Json::Value result;
        if (reader->parse(user_trace_data_.data(),
                          user_trace_data_.data() + user_trace_data_.length(),
                          &result, nullptr)) {
          for (const auto& event : result) {
            WriteCommonEvent(event);
          }
        } else {
          PERFETTO_DLOG(
              "can't parse legacy user json trace export, skipping. data: %s",
              user_trace_data_.c_str());
        }
      }

      std::ostringstream ss;
      if (!label_filter_)
        ss << "]";

      if ((!label_filter_ || label_filter_("systemTraceEvents")) &&
          !system_trace_data_.empty()) {
        ss << ",\"systemTraceEvents\":\n";
        writer_->write(Json::Value(system_trace_data_), &ss);
      }

      if ((!label_filter_ || label_filter_("metadata")) && !metadata_.empty()) {
        ss << ",\"metadata\":\n";
        writer_->write(metadata_, &ss);
      }

      if (!label_filter_)
        ss << "}";

      output_->AppendString(ss.str());
    }

    void DoWriteEvent(const Json::Value& event) {
      std::ostringstream ss;
      if (!first_event_)
        ss << ",\n";

      ArgumentNameFilterPredicate argument_name_filter;
      bool strip_args =
          argument_filter_ &&
          !argument_filter_(event["cat"].asCString(), event["name"].asCString(),
                            &argument_name_filter);
      if ((strip_args || argument_name_filter) && event.isMember("args")) {
        Json::Value event_copy = event;
        if (strip_args) {
          event_copy["args"] = kStrippedArgument;
        } else {
          auto& args = event_copy["args"];
          for (const auto& member : event["args"].getMemberNames()) {
            if (!argument_name_filter(member.c_str()))
              args[member] = kStrippedArgument;
          }
        }
        writer_->write(event_copy, &ss);
      } else {
        writer_->write(event, &ss);
      }
      first_event_ = false;

      output_->AppendString(ss.str());
    }

    OutputWriter* output_;
    ArgumentFilterPredicate argument_filter_;
    MetadataFilterPredicate metadata_filter_;
    LabelFilterPredicate label_filter_;

    std::unique_ptr<Json::StreamWriter> writer_;
    bool first_event_;
    Json::Value metadata_;
    std::string system_trace_data_;
    std::string user_trace_data_;
    std::vector<Json::Value> async_begin_events_;
    std::vector<Json::Value> async_instant_events_;
    std::vector<Json::Value> async_end_events_;
  };

  class ArgsBuilder {
   public:
    explicit ArgsBuilder(const TraceStorage* storage)
        : storage_(storage),
          empty_value_(Json::objectValue),
          nan_value_(Json::StaticString("NaN")),
          inf_value_(Json::StaticString("Infinity")),
          neg_inf_value_(Json::StaticString("-Infinity")) {
      const auto& arg_table = storage_->arg_table();
      ArgSet arg_set;
      uint32_t cur_args_set_id = std::numeric_limits<uint32_t>::max();
      for (auto it = arg_table.IterateRows(); it; ++it) {
        ArgSetId set_id = it.arg_set_id();
        if (set_id != cur_args_set_id) {
          args_sets_[cur_args_set_id] = ArgNodeToJson(arg_set.root());
          arg_set = ArgSet();
          cur_args_set_id = set_id;
        }
        arg_set.AppendArg(storage->GetString(it.key()),
                          storage_->GetArgValue(it.row_number().row_number()));
      }
      if (cur_args_set_id != std::numeric_limits<uint32_t>::max()) {
        args_sets_[cur_args_set_id] = ArgNodeToJson(arg_set.root());
      }
      PostprocessArgs();
    }

    const Json::Value& GetArgs(std::optional<ArgSetId> set_id) const {
      return set_id ? *args_sets_.Find(*set_id) : empty_value_;
    }

    std::optional<int64_t> GetLegacyTraceSourceId(ArgSetId set_id) const {
      auto* ptr = legacy_trace_ids_.Find(set_id);
      return ptr ? std::make_optional(*ptr) : std::nullopt;
    }

   private:
    Json::Value VariadicToJson(Variadic variadic) {
      switch (variadic.type) {
        case Variadic::kInt:
          return Json::Int64(variadic.int_value);
        case Variadic::kUint:
          return Json::UInt64(variadic.uint_value);
        case Variadic::kString:
          return GetNonNullString(storage_, variadic.string_value);
        case Variadic::kReal:
          if (std::isnan(variadic.real_value)) {
            return nan_value_;
          } else if (std::isinf(variadic.real_value) &&
                     variadic.real_value > 0) {
            return inf_value_;
          } else if (std::isinf(variadic.real_value) &&
                     variadic.real_value < 0) {
            return neg_inf_value_;
          } else {
            return variadic.real_value;
          }
        case Variadic::kPointer:
          return base::Uint64ToHexString(variadic.pointer_value);
        case Variadic::kBool:
          return variadic.bool_value;
        case Variadic::kNull:
          return base::Uint64ToHexString(0);
        case Variadic::kJson:
          Json::CharReaderBuilder b;
          auto reader = std::unique_ptr<Json::CharReader>(b.newCharReader());

          Json::Value result;
          std::string v = GetNonNullString(storage_, variadic.json_value);
          reader->parse(v.data(), v.data() + v.length(), &result, nullptr);
          return result;
      }
      PERFETTO_FATAL("Not reached");  // For gcc.
    }

    Json::Value ArgNodeToJson(const ArgNode& node) {
      switch (node.GetType()) {
        case ArgNode::Type::kPrimitive:
          return VariadicToJson(node.GetPrimitiveValue());
        case ArgNode::Type::kArray: {
          Json::Value result(Json::arrayValue);
          for (const auto& child : node.GetArray()) {
            result.append(ArgNodeToJson(child));
          }
          return result;
        }
        case ArgNode::Type::kDict: {
          Json::Value result(Json::objectValue);
          for (const auto& [key, value] : node.GetDict()) {
            result[key] = ArgNodeToJson(value);
          }
          return result;
        }
      }
      PERFETTO_FATAL("Not reached");  // For gcc.
    }

    void PostprocessArgs() {
      for (auto it = args_sets_.GetIterator(); it; ++it) {
        auto& args = it.value();
        // Move all fields from "debug" key to upper level.
        if (args.isMember("debug")) {
          Json::Value debug = args["debug"];
          args.removeMember("debug");
          for (const auto& member : debug.getMemberNames()) {
            args[member] = debug[member];
          }
        }

        if (args.isMember("legacy_trace_source_id")) {
          const Json::Value& legacy_trace_source_id =
              args["legacy_trace_source_id"];
          if (legacy_trace_source_id.isInt64()) {
            legacy_trace_ids_[it.key()] = legacy_trace_source_id.asInt64();
            args.removeMember("legacy_trace_source_id");
          }
        }

        // Rename source fields.
        if (args.isMember("task")) {
          if (args["task"].isMember("posted_from")) {
            Json::Value posted_from = args["task"]["posted_from"];
            args["task"].removeMember("posted_from");
            if (posted_from.isMember("function_name")) {
              args["src_func"] = posted_from["function_name"];
              args["src_file"] = posted_from["file_name"];
              args["src_line"] = posted_from["line_number"];
            } else if (posted_from.isMember("file_name")) {
              args["src"] = posted_from["file_name"];
            }
          }
          if (args["task"].empty())
            args.removeMember("task");
        }
        if (args.isMember("source")) {
          Json::Value source = args["source"];
          if (source.isObject() && source.isMember("function_name")) {
            args["function_name"] = source["function_name"];
            args["file_name"] = source["file_name"];
            args["line_number"] = source["line_number"];
            args.removeMember("source");
          }
        }
      }
    }

    const TraceStorage* storage_;
    base::FlatHashMap<ArgSetId, Json::Value> args_sets_;
    base::FlatHashMap<ArgSetId, int64_t> legacy_trace_ids_;
    const Json::Value empty_value_;
    const Json::Value nan_value_;
    const Json::Value inf_value_;
    const Json::Value neg_inf_value_;
  };

  base::Status MapUniquePidsAndTids() {
    const auto& process_table = storage_->process_table();
    for (auto it = process_table.IterateRows(); it; ++it) {
      UniquePid upid = it.id();
      int64_t exported_pid = it.pid();
      auto it_and_inserted =
          exported_pids_to_upids_.emplace(exported_pid, upid);
      if (!it_and_inserted.second) {
        exported_pid = NextExportedPidOrTidForDuplicates();
        it_and_inserted = exported_pids_to_upids_.emplace(exported_pid, upid);
      }
      upids_to_exported_pids_.emplace(upid, exported_pid);
    }

    const auto& thread_table = storage_->thread_table();
    for (auto it = thread_table.IterateRows(); it; ++it) {
      UniqueTid utid = it.id();

      int64_t exported_pid = 0;
      std::optional<UniquePid> upid = it.upid();
      if (upid) {
        auto exported_pid_it = upids_to_exported_pids_.find(*upid);
        PERFETTO_DCHECK(exported_pid_it != upids_to_exported_pids_.end());
        exported_pid = exported_pid_it->second;
      }

      int64_t exported_tid = it.tid();
      auto it_and_inserted = exported_pids_and_tids_to_utids_.emplace(
          std::make_pair(exported_pid, exported_tid), utid);
      if (!it_and_inserted.second) {
        exported_tid = NextExportedPidOrTidForDuplicates();
        it_and_inserted = exported_pids_and_tids_to_utids_.emplace(
            std::make_pair(exported_pid, exported_tid), utid);
      }
      utids_to_exported_pids_and_tids_.emplace(
          utid, std::make_pair(exported_pid, exported_tid));
    }
    return base::OkStatus();
  }

  base::Status ExportThreadNames() {
    const auto& thread_table = storage_->thread_table();
    for (auto it = thread_table.IterateRows(); it; ++it) {
      auto opt_name = it.name();
      if (opt_name.has_value()) {
        UniqueTid utid = it.id();
        const char* thread_name = GetNonNullString(storage_, opt_name);
        auto pid_and_tid = UtidToPidAndTid(utid);
        writer_.WriteMetadataEvent("thread_name", "name", thread_name,
                                   pid_and_tid.first, pid_and_tid.second);
      }
    }
    return base::OkStatus();
  }

  base::Status ExportProcessNames() {
    const auto& process_table = storage_->process_table();
    for (auto it = process_table.IterateRows(); it; ++it) {
      auto opt_name = it.name();
      if (opt_name.has_value()) {
        UniquePid upid = it.id();
        const char* process_name = GetNonNullString(storage_, opt_name);
        writer_.WriteMetadataEvent("process_name", "name", process_name,
                                   UpidToPid(upid), /*tid=*/0);
      }
    }
    return base::OkStatus();
  }

  // For each process it writes an approximate uptime, based on the process'
  // start time and the last slice in the entire trace. This same last slice is
  // used with all processes, so the process could have ended earlier.
  base::Status ExportProcessUptimes() {
    int64_t last_timestamp_ns = FindLastSliceTimestamp();
    if (last_timestamp_ns <= 0)
      return base::OkStatus();

    const auto& process_table = storage_->process_table();
    for (auto it = process_table.IterateRows(); it; ++it) {
      std::optional<int64_t> start_timestamp_ns = it.start_ts();
      if (!start_timestamp_ns.has_value()) {
        continue;
      }

      UniquePid upid = it.id();
      int64_t process_uptime_seconds =
          (last_timestamp_ns - start_timestamp_ns.value()) /
          (1000l * 1000 * 1000);
      writer_.WriteMetadataEvent("process_uptime_seconds", "uptime",
                                 std::to_string(process_uptime_seconds).c_str(),
                                 UpidToPid(upid), /*tid=*/0);
    }

    return base::OkStatus();
  }

  // Returns the last slice's end timestamp for the entire trace. If no slices
  // are found 0 is returned.
  int64_t FindLastSliceTimestamp() {
    int64_t last_ts = 0;
    for (auto it = storage_->slice_table().IterateRows(); it; ++it) {
      last_ts = std::max(last_ts, it.ts() + it.dur());
    }
    return last_ts;
  }

  base::Status ExportSlices() {
    const auto& slices = storage_->slice_table();
    for (auto it = slices.IterateRows(); it; ++it) {
      // Skip slices with empty category - these are ftrace/system slices that
      // were also imported into the raw table and will be exported from there
      // by trace_to_text.
      // TODO(b/153609716): Add a src column or do_not_export flag instead.
      if (!it.category())
        continue;
      auto cat = storage_->GetString(*it.category());
      if (cat.c_str() == nullptr || cat == "binder")
        continue;

      Json::Value event;
      event["ts"] = Json::Int64(it.ts() / 1000);
      event["cat"] = GetNonNullString(storage_, it.category());
      event["name"] = GetNonNullString(storage_, it.name());
      event["pid"] = 0;
      event["tid"] = 0;

      std::optional<UniqueTid> legacy_utid;
      std::string legacy_phase;

      event["args"] = args_builder_.GetArgs(it.arg_set_id());  // Makes a copy.
      if (event["args"].isMember(kLegacyEventArgsKey)) {
        const auto& legacy_args = event["args"][kLegacyEventArgsKey];

        if (legacy_args.isMember(kLegacyEventPassthroughUtidKey)) {
          legacy_utid = legacy_args[kLegacyEventPassthroughUtidKey].asUInt();
        }
        if (legacy_args.isMember(kLegacyEventPhaseKey)) {
          legacy_phase = legacy_args[kLegacyEventPhaseKey].asString();
        }

        event["args"].removeMember(kLegacyEventArgsKey);
      }

      std::optional<int64_t> legacy_trace_source_id;
      if (it.arg_set_id()) {
        legacy_trace_source_id =
            args_builder_.GetLegacyTraceSourceId(*it.arg_set_id());
      }

      // To prevent duplicate export of slices, only export slices on descriptor
      // or chrome tracks (i.e. TrackEvent slices). Slices on other tracks may
      // also be present as raw events and handled by trace_to_text. Only add
      // more track types here if they are not already covered by trace_to_text.
      TrackId track_id = it.track_id();

      const auto& track_table = storage_->track_table();

      auto track_row_ref = *track_table.FindById(track_id);
      auto track_args_id = track_row_ref.source_arg_set_id();
      const Json::Value* track_args = nullptr;
      bool legacy_chrome_track = false;
      bool is_child_track = false;
      if (track_args_id) {
        track_args = &args_builder_.GetArgs(*track_args_id);
        legacy_chrome_track = (*track_args)["source"].asString() == "chrome";
        is_child_track = track_args->isMember("is_root_in_scope") &&
                         !(*track_args)["is_root_in_scope"].asBool();
      }

      const auto& virtual_track_slices = storage_->virtual_track_slices();

      int64_t duration_ns = it.dur();
      std::optional<int64_t> thread_ts_ns;
      std::optional<int64_t> thread_duration_ns;
      std::optional<int64_t> thread_instruction_count;
      std::optional<int64_t> thread_instruction_delta;

      if (it.thread_dur()) {
        thread_ts_ns = it.thread_ts();
        thread_duration_ns = it.thread_dur();
        thread_instruction_count = it.thread_instruction_count();
        thread_instruction_delta = it.thread_instruction_delta();
      } else {
        SliceId id = it.id();
        std::optional<uint32_t> vtrack_slice_row =
            virtual_track_slices.FindRowForSliceId(id);
        if (vtrack_slice_row) {
          thread_ts_ns =
              virtual_track_slices.thread_timestamp_ns()[*vtrack_slice_row];
          thread_duration_ns =
              virtual_track_slices.thread_duration_ns()[*vtrack_slice_row];
          thread_instruction_count =
              virtual_track_slices
                  .thread_instruction_counts()[*vtrack_slice_row];
          thread_instruction_delta =
              virtual_track_slices
                  .thread_instruction_deltas()[*vtrack_slice_row];
        }
      }

      if (track_row_ref.utid() && !is_child_track) {
        // Synchronous (thread) slice or instant event.
        auto pid_and_tid = UtidToPidAndTid(*track_row_ref.utid());
        event["pid"] = Json::Int(pid_and_tid.first);
        event["tid"] = Json::Int(pid_and_tid.second);

        if (duration_ns == 0) {
          if (legacy_phase.empty()) {
            // Use "I" instead of "i" phase for backwards-compat with old
            // consumers.
            event["ph"] = "I";
          } else {
            event["ph"] = legacy_phase;
          }
          if (thread_ts_ns && thread_ts_ns > 0) {
            event["tts"] = Json::Int64(*thread_ts_ns / 1000);
          }
          if (thread_instruction_count && *thread_instruction_count > 0) {
            event["ticount"] = Json::Int64(*thread_instruction_count);
          }
          event["s"] = "t";
        } else {
          if (duration_ns > 0) {
            event["ph"] = "X";
            event["dur"] = Json::Int64(duration_ns / 1000);
          } else {
            // If the slice didn't finish, the duration may be negative. Only
            // write a begin event without end event in this case.
            event["ph"] = "B";
          }
          if (thread_ts_ns && *thread_ts_ns > 0) {
            event["tts"] = Json::Int64(*thread_ts_ns / 1000);
            // Only write thread duration for completed events.
            if (duration_ns > 0 && thread_duration_ns)
              event["tdur"] = Json::Int64(*thread_duration_ns / 1000);
          }
          if (thread_instruction_count && *thread_instruction_count > 0) {
            event["ticount"] = Json::Int64(*thread_instruction_count);
            // Only write thread instruction delta for completed events.
            if (duration_ns > 0 && thread_instruction_delta)
              event["tidelta"] = Json::Int64(*thread_instruction_delta);
          }
        }
        writer_.WriteCommonEvent(event);
      } else if (is_child_track ||
                 (legacy_chrome_track && legacy_trace_source_id)) {
        // Async event slice.
        if (legacy_chrome_track) {
          // Legacy async tracks are always process-associated and have args.
          PERFETTO_DCHECK(track_args);
          PERFETTO_DCHECK(track_args->isMember("upid"));
          int64_t exported_pid = UpidToPid((*track_args)["upid"].asUInt());
          event["pid"] = Json::Int(exported_pid);
          event["tid"] =
              Json::Int(legacy_utid ? UtidToPidAndTid(*legacy_utid).second
                                    : exported_pid);

          // Preserve original event IDs for legacy tracks. This is so that e.g.
          // memory dump IDs show up correctly in the JSON trace.
          PERFETTO_DCHECK(legacy_trace_source_id);
          PERFETTO_DCHECK(track_args->isMember("trace_id_is_process_scoped"));
          PERFETTO_DCHECK(track_args->isMember("source_scope"));
          auto trace_id = static_cast<uint64_t>(*legacy_trace_source_id);
          std::string source_scope = (*track_args)["source_scope"].asString();
          if (!source_scope.empty())
            event["scope"] = source_scope;
          bool trace_id_is_process_scoped =
              (*track_args)["trace_id_is_process_scoped"].asBool();
          if (trace_id_is_process_scoped) {
            event["id2"]["local"] = base::Uint64ToHexString(trace_id);
          } else {
            // Some legacy importers don't understand "id2" fields, so we use
            // the "usually" global "id" field instead. This works as long as
            // the event phase is not in {'N', 'D', 'O', '(', ')'}, see
            // "LOCAL_ID_PHASES" in catapult.
            event["id"] = base::Uint64ToHexString(trace_id);
          }
        } else {
          if (track_row_ref.utid()) {
            auto pid_and_tid = UtidToPidAndTid(*track_row_ref.utid());
            event["pid"] = Json::Int(pid_and_tid.first);
            event["tid"] = Json::Int(pid_and_tid.second);
            event["id2"]["local"] = base::Uint64ToHexString(track_id.value);
          } else if (track_row_ref.upid()) {
            int64_t exported_pid = UpidToPid(*track_row_ref.upid());
            event["pid"] = Json::Int(exported_pid);
            event["tid"] =
                Json::Int(legacy_utid ? UtidToPidAndTid(*legacy_utid).second
                                      : exported_pid);
            event["id2"]["local"] = base::Uint64ToHexString(track_id.value);
          } else {
            if (legacy_utid) {
              auto pid_and_tid = UtidToPidAndTid(*legacy_utid);
              event["pid"] = Json::Int(pid_and_tid.first);
              event["tid"] = Json::Int(pid_and_tid.second);
            }

            // Some legacy importers don't understand "id2" fields, so we use
            // the "usually" global "id" field instead. This works as long as
            // the event phase is not in {'N', 'D', 'O', '(', ')'}, see
            // "LOCAL_ID_PHASES" in catapult.
            event["id"] = base::Uint64ToHexString(track_id.value);
          }
        }

        if (thread_ts_ns && *thread_ts_ns > 0) {
          event["tts"] = Json::Int64(*thread_ts_ns / 1000);
          event["use_async_tts"] = Json::Int(1);
        }
        if (thread_instruction_count && *thread_instruction_count > 0) {
          event["ticount"] = Json::Int64(*thread_instruction_count);
          event["use_async_tts"] = Json::Int(1);
        }

        if (duration_ns == 0) {
          if (legacy_phase.empty()) {
            // Instant async event.
            event["ph"] = "n";
            writer_.AddAsyncInstantEvent(event);
          } else {
            // Async step events.
            event["ph"] = legacy_phase;
            writer_.AddAsyncBeginEvent(event);
          }
        } else {  // Async start and end.
          event["ph"] = legacy_phase.empty() ? "b" : legacy_phase;
          writer_.AddAsyncBeginEvent(event);
          // If the slice didn't finish, the duration may be negative. Don't
          // write the end event in this case.
          if (duration_ns > 0) {
            event["ph"] = legacy_phase.empty() ? "e" : "F";
            event["ts"] = Json::Int64((it.ts() + duration_ns) / 1000);
            if (thread_ts_ns && thread_duration_ns && *thread_ts_ns > 0) {
              event["tts"] =
                  Json::Int64((*thread_ts_ns + *thread_duration_ns) / 1000);
            }
            if (thread_instruction_count && thread_instruction_delta &&
                *thread_instruction_count > 0) {
              event["ticount"] = Json::Int64(
                  (*thread_instruction_count + *thread_instruction_delta));
            }
            event["args"].clear();
            writer_.AddAsyncEndEvent(event);
          }
        }
      } else {
        // Global or process-scoped instant event.
        PERFETTO_DCHECK(legacy_chrome_track || !is_child_track);
        if (duration_ns != 0) {
          // We don't support exporting slices on the default global or process
          // track to JSON (JSON only supports instant events on these tracks).
          PERFETTO_DLOG(
              "skipping non-instant slice on global or process track");
        } else {
          if (legacy_phase.empty()) {
            // Use "I" instead of "i" phase for backwards-compat with old
            // consumers.
            event["ph"] = "I";
          } else {
            event["ph"] = legacy_phase;
          }

          if (track_row_ref.upid()) {
            int64_t exported_pid = UpidToPid(*track_row_ref.upid());
            event["pid"] = Json::Int(exported_pid);
            event["tid"] =
                Json::Int(legacy_utid ? UtidToPidAndTid(*legacy_utid).second
                                      : exported_pid);
            event["s"] = "p";
          } else {
            event["s"] = "g";
          }
          writer_.WriteCommonEvent(event);
        }
      }
    }
    return base::OkStatus();
  }

  std::optional<Json::Value> CreateFlowEventV1(uint32_t flow_id,
                                               SliceId slice_id,
                                               const std::string& name,
                                               const std::string& cat,
                                               Json::Value args,
                                               bool flow_begin) {
    const auto& slices = storage_->slice_table();

    auto opt_slice_rr = slices.FindById(slice_id);
    if (!opt_slice_rr)
      return std::nullopt;
    auto slice_rr = opt_slice_rr.value();

    TrackId track_id = slice_rr.track_id();
    auto rr = storage_->track_table().FindById(track_id);

    // catapult only supports flow events attached to thread-track slices
    if (!rr || !rr->utid()) {
      return std::nullopt;
    }

    UniqueTid utid = *rr->utid();
    auto pid_and_tid = UtidToPidAndTid(utid);
    Json::Value event;
    event["id"] = flow_id;
    event["pid"] = Json::Int(pid_and_tid.first);
    event["tid"] = Json::Int(pid_and_tid.second);
    event["cat"] = cat;
    event["name"] = name;
    event["ph"] = (flow_begin ? "s" : "f");
    event["ts"] = Json::Int64(slice_rr.ts() / 1000);
    if (!flow_begin) {
      event["bp"] = "e";
    }
    event["args"] = std::move(args);
    return std::move(event);
  }

  base::Status ExportFlows() {
    const auto& flow_table = storage_->flow_table();
    const auto& slice_table = storage_->slice_table();
    for (auto it = flow_table.IterateRows(); it; ++it) {
      SliceId slice_out = it.slice_out();
      SliceId slice_in = it.slice_in();
      std::optional<uint32_t> arg_set_id = it.arg_set_id();

      std::string cat;
      std::string name;
      auto args = args_builder_.GetArgs(arg_set_id);
      if (arg_set_id != std::nullopt) {
        cat = args["cat"].asString();
        name = args["name"].asString();
        // Don't export these args since they are only used for this export and
        // weren't part of the original event.
        args.removeMember("name");
        args.removeMember("cat");
      } else {
        auto rr = slice_table.FindById(slice_out);
        PERFETTO_DCHECK(rr.has_value());
        cat = GetNonNullString(storage_, rr->category());
        name = GetNonNullString(storage_, rr->name());
      }

      uint32_t i = it.row_number().row_number();
      auto out_event = CreateFlowEventV1(i, slice_out, name, cat, args,
                                         /* flow_begin = */ true);
      auto in_event = CreateFlowEventV1(i, slice_in, name, cat, std::move(args),
                                        /* flow_begin = */ false);

      if (out_event && in_event) {
        writer_.WriteCommonEvent(out_event.value());
        writer_.WriteCommonEvent(in_event.value());
      }
    }
    return base::OkStatus();
  }

  Json::Value ConvertLegacyRawEventToJson(
      const tables::ChromeRawTable::ConstIterator& it) {
    Json::Value event;
    event["ts"] = Json::Int64(it.ts() / 1000);

    UniqueTid utid = static_cast<UniqueTid>(it.utid());
    auto pid_and_tid = UtidToPidAndTid(utid);
    event["pid"] = Json::Int(pid_and_tid.first);
    event["tid"] = Json::Int(pid_and_tid.second);

    // Raw legacy events store all other params in the arg set. Make a copy of
    // the converted args here, parse, and then remove the legacy params.
    event["args"] = args_builder_.GetArgs(it.arg_set_id());
    const Json::Value& legacy_args = event["args"][kLegacyEventArgsKey];

    PERFETTO_DCHECK(legacy_args.isMember(kLegacyEventCategoryKey));
    event["cat"] = legacy_args[kLegacyEventCategoryKey];

    PERFETTO_DCHECK(legacy_args.isMember(kLegacyEventNameKey));
    event["name"] = legacy_args[kLegacyEventNameKey];

    PERFETTO_DCHECK(legacy_args.isMember(kLegacyEventPhaseKey));
    event["ph"] = legacy_args[kLegacyEventPhaseKey];

    // Object snapshot events are supposed to have a mandatory "snapshot" arg,
    // which may be removed in trace processor if it is empty.
    if (legacy_args[kLegacyEventPhaseKey] == "O" &&
        !event["args"].isMember("snapshot")) {
      event["args"]["snapshot"] = Json::Value(Json::objectValue);
    }

    if (legacy_args.isMember(kLegacyEventDurationNsKey))
      event["dur"] = legacy_args[kLegacyEventDurationNsKey].asInt64() / 1000;

    if (legacy_args.isMember(kLegacyEventThreadTimestampNsKey)) {
      event["tts"] =
          legacy_args[kLegacyEventThreadTimestampNsKey].asInt64() / 1000;
    }

    if (legacy_args.isMember(kLegacyEventThreadDurationNsKey)) {
      event["tdur"] =
          legacy_args[kLegacyEventThreadDurationNsKey].asInt64() / 1000;
    }

    if (legacy_args.isMember(kLegacyEventThreadInstructionCountKey))
      event["ticount"] = legacy_args[kLegacyEventThreadInstructionCountKey];

    if (legacy_args.isMember(kLegacyEventThreadInstructionDeltaKey))
      event["tidelta"] = legacy_args[kLegacyEventThreadInstructionDeltaKey];

    if (legacy_args.isMember(kLegacyEventUseAsyncTtsKey))
      event["use_async_tts"] = legacy_args[kLegacyEventUseAsyncTtsKey];

    if (legacy_args.isMember(kLegacyEventUnscopedIdKey)) {
      event["id"] = base::Uint64ToHexString(
          legacy_args[kLegacyEventUnscopedIdKey].asUInt64());
    }

    if (legacy_args.isMember(kLegacyEventGlobalIdKey)) {
      event["id2"]["global"] = base::Uint64ToHexString(
          legacy_args[kLegacyEventGlobalIdKey].asUInt64());
    }

    if (legacy_args.isMember(kLegacyEventLocalIdKey)) {
      event["id2"]["local"] = base::Uint64ToHexString(
          legacy_args[kLegacyEventLocalIdKey].asUInt64());
    }

    if (legacy_args.isMember(kLegacyEventIdScopeKey))
      event["scope"] = legacy_args[kLegacyEventIdScopeKey];

    event["args"].removeMember(kLegacyEventArgsKey);

    return event;
  }

  base::Status ExportRawEvents() {
    std::optional<StringId> raw_legacy_event_key_id =
        storage_->string_pool().GetId("track_event.legacy_event");
    std::optional<StringId> raw_legacy_system_trace_event_id =
        storage_->string_pool().GetId("chrome_event.legacy_system_trace");
    std::optional<StringId> raw_legacy_user_trace_event_id =
        storage_->string_pool().GetId("chrome_event.legacy_user_trace");
    std::optional<StringId> raw_chrome_metadata_event_id =
        storage_->string_pool().GetId("chrome_event.metadata");

    const auto& events = storage_->chrome_raw_table();
    for (auto it = events.IterateRows(); it; ++it) {
      if (raw_legacy_event_key_id && it.name() == *raw_legacy_event_key_id) {
        Json::Value event = ConvertLegacyRawEventToJson(it);
        writer_.WriteCommonEvent(event);
      } else if (raw_legacy_system_trace_event_id &&
                 it.name() == *raw_legacy_system_trace_event_id) {
        Json::Value args = args_builder_.GetArgs(it.arg_set_id());
        PERFETTO_DCHECK(args.isMember("data"));
        writer_.AddSystemTraceData(args["data"].asString());
      } else if (raw_legacy_user_trace_event_id &&
                 it.name() == *raw_legacy_user_trace_event_id) {
        Json::Value args = args_builder_.GetArgs(it.arg_set_id());
        PERFETTO_DCHECK(args.isMember("data"));
        writer_.AddUserTraceData(args["data"].asString());
      } else if (raw_chrome_metadata_event_id &&
                 it.name() == *raw_chrome_metadata_event_id) {
        Json::Value args = args_builder_.GetArgs(it.arg_set_id());
        writer_.MergeMetadata(args);
      }
    }
    return base::OkStatus();
  }

  base::Status ExportMetadata() {
    const auto& trace_metadata = storage_->metadata_table();

    // Create a mapping from key string ids to keys.
    std::unordered_map<StringId, metadata::KeyId> key_map;
    for (uint32_t i = 0; i < metadata::kNumKeys; ++i) {
      auto id = *storage_->string_pool().GetId(metadata::kNames[i]);
      key_map[id] = static_cast<metadata::KeyId>(i);
    }

    for (auto it = trace_metadata.IterateRows(); it; ++it) {
      auto key_it = key_map.find(it.name());
      // Skip exporting dynamic entries; the cr-xxx entries that come from
      // the ChromeMetadata proto message are already exported from the raw
      // table.
      if (key_it == key_map.end())
        continue;

      // Cast away from enum type, as otherwise -Wswitch-enum will demand an
      // exhaustive list of cases, even if there's a default case.
      metadata::KeyId key = key_it->second;
      switch (static_cast<size_t>(key)) {
        case metadata::trace_config_pbtxt:
          writer_.WriteTraceConfigString(
              storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        case metadata::benchmark_description:
          writer_.AppendTelemetryMetadataString(
              "benchmarkDescriptions",
              storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        case metadata::benchmark_name:
          writer_.AppendTelemetryMetadataString(
              "benchmarks",
              storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        case metadata::benchmark_start_time_us:
          writer_.SetTelemetryMetadataTimestamp("benchmarkStart",
                                                *it.int_value());
          break;

        case metadata::benchmark_had_failures:
          writer_.AppendTelemetryMetadataBool("hadFailures", *it.int_value());
          break;

        case metadata::benchmark_label:
          writer_.AppendTelemetryMetadataString(
              "labels", storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        case metadata::benchmark_story_name:
          writer_.AppendTelemetryMetadataString(
              "stories", storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        case metadata::benchmark_story_run_index:
          writer_.AppendTelemetryMetadataInt("storysetRepeats",
                                             *it.int_value());
          break;

        case metadata::benchmark_story_run_time_us:
          writer_.SetTelemetryMetadataTimestamp("traceStart", *it.int_value());
          break;

        case metadata::benchmark_story_tags:  // repeated
          writer_.AppendTelemetryMetadataString(
              "storyTags",
              storage_->string_pool().Get(*it.str_value()).c_str());
          break;

        default:
          PERFETTO_DLOG("Ignoring metadata key %zu", static_cast<size_t>(key));
          break;
      }
    }
    return base::OkStatus();
  }

  base::Status ExportStats() {
    const auto& stats = storage_->stats();

    for (size_t idx = 0; idx < stats::kNumKeys; idx++) {
      if (stats::kTypes[idx] == stats::kSingle) {
        writer_.SetStats(stats::kNames[idx], stats[idx].value);
      } else {
        PERFETTO_DCHECK(stats::kTypes[idx] == stats::kIndexed);
        writer_.SetStats(stats::kNames[idx], stats[idx].indexed_values);
      }
    }

    return base::OkStatus();
  }

  base::Status ExportMemorySnapshots() {
    const auto& memory_snapshots = storage_->memory_snapshot_table();
    std::optional<StringId> private_footprint_id =
        storage_->string_pool().GetId("chrome.private_footprint_kb");
    std::optional<StringId> peak_resident_set_id =
        storage_->string_pool().GetId("chrome.peak_resident_set_kb");

    std::string_view chrome_process_stats =
        tracks::kChromeProcessStatsBlueprint.type;
    std::optional<StringId> process_stats = storage_->string_pool().GetId(
        {chrome_process_stats.data(), chrome_process_stats.size()});

    for (auto sit = memory_snapshots.IterateRows(); sit; ++sit) {
      Json::Value event_base;

      event_base["ph"] = "v";
      event_base["cat"] = "disabled-by-default-memory-infra";
      auto snapshot_id = sit.id();
      event_base["id"] = base::Uint64ToHexString(snapshot_id.value);
      int64_t snapshot_ts = sit.timestamp();
      event_base["ts"] = Json::Int64(snapshot_ts / 1000);
      // TODO(crbug:1116359): Add dump type to the snapshot proto
      // to properly fill event_base["name"]
      event_base["name"] = "periodic_interval";
      event_base["args"]["dumps"]["level_of_detail"] =
          GetNonNullString(storage_, sit.detail_level());

      // Export OS dump events for processes with relevant data.
      const auto& process_table = storage_->process_table();
      const auto& track_table = storage_->track_table();
      for (auto pit = process_table.IterateRows(); pit; ++pit) {
        Json::Value event = FillInProcessEventDetails(event_base, pit.pid());
        Json::Value& totals = event["args"]["dumps"]["process_totals"];

        for (auto it = track_table.IterateRows(); it; ++it) {
          if (it.type() != process_stats) {
            continue;
          }
          if (it.upid() != pit.id()) {
            continue;
          }
          TrackId track_id = it.id();
          if (private_footprint_id && (it.name() == private_footprint_id)) {
            totals["private_footprint_bytes"] = base::Uint64ToHexStringNoPrefix(
                GetCounterValue(track_id, snapshot_ts));
          } else if (peak_resident_set_id &&
                     (it.name() == peak_resident_set_id)) {
            totals["peak_resident_set_size"] = base::Uint64ToHexStringNoPrefix(
                GetCounterValue(track_id, snapshot_ts));
          }
        }

        auto process_args_id = pit.arg_set_id();
        if (process_args_id) {
          const Json::Value* process_args =
              &args_builder_.GetArgs(process_args_id);
          if (process_args->isMember("is_peak_rss_resettable")) {
            totals["is_peak_rss_resettable"] =
                (*process_args)["is_peak_rss_resettable"];
          }
        }

        const auto& smaps_table = storage_->profiler_smaps_table();
        // Do not create vm_regions without memory maps, since catapult expects
        // to have rows.
        Json::Value* smaps =
            smaps_table.row_count() > 0
                ? &event["args"]["dumps"]["process_mmaps"]["vm_regions"]
                : nullptr;
        for (auto it = smaps_table.IterateRows(); it; ++it) {
          if (it.upid() != pit.id())
            continue;
          if (it.ts() != snapshot_ts)
            continue;
          Json::Value region;
          region["mf"] = GetNonNullString(storage_, it.file_name());
          region["pf"] = Json::Int64(it.protection_flags());
          region["sa"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.start_address()));
          region["sz"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.size_kb()) * 1024);
          region["ts"] = Json::Int64(it.module_timestamp());
          region["id"] = GetNonNullString(storage_, it.module_debugid());
          region["df"] = GetNonNullString(storage_, it.module_debug_path());
          region["bs"]["pc"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.private_clean_resident_kb()) * 1024);
          region["bs"]["pd"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.private_dirty_kb()) * 1024);
          region["bs"]["pss"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.proportional_resident_kb()) * 1024);
          region["bs"]["sc"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.shared_clean_resident_kb()) * 1024);
          region["bs"]["sd"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.shared_dirty_resident_kb()) * 1024);
          region["bs"]["sw"] = base::Uint64ToHexStringNoPrefix(
              static_cast<uint64_t>(it.swap_kb()) * 1024);
          smaps->append(region);
        }

        if (!totals.empty() || (smaps && !smaps->empty()))
          writer_.WriteCommonEvent(event);
      }

      // Export chrome dump events for process snapshots in current memory
      // snapshot.
      const auto& process_snapshots = storage_->process_memory_snapshot_table();

      for (auto psit = process_snapshots.IterateRows(); psit; ++psit) {
        if (psit.snapshot_id() != snapshot_id)
          continue;

        auto process_snapshot_id = psit.id();
        int64_t pid = UpidToPid(psit.upid());

        // Shared memory nodes are imported into a fake process with pid 0.
        // Catapult expects them to be associated with one of the real processes
        // of the snapshot, so we choose the first one we can find and replace
        // the pid.
        if (pid == 0) {
          for (auto iit = process_snapshots.IterateRows(); iit; ++iit) {
            if (iit.snapshot_id() != snapshot_id)
              continue;
            int64_t new_pid = UpidToPid(iit.upid());
            if (new_pid != 0) {
              pid = new_pid;
              break;
            }
          }
        }

        Json::Value event = FillInProcessEventDetails(event_base, pid);

        const auto& sn = storage_->memory_snapshot_node_table();

        for (auto it = sn.IterateRows(); it; ++it) {
          if (it.process_snapshot_id() != process_snapshot_id) {
            continue;
          }
          const char* path = GetNonNullString(storage_, it.path());
          event["args"]["dumps"]["allocators"][path]["guid"] =
              base::Uint64ToHexStringNoPrefix(
                  static_cast<uint64_t>(it.id().value));
          if (it.size()) {
            AddAttributeToMemoryNode(&event, path, "size", it.size(), "bytes");
          }
          if (it.effective_size()) {
            AddAttributeToMemoryNode(&event, path, "effective_size",
                                     it.effective_size(), "bytes");
          }

          auto node_args_id = it.arg_set_id();
          if (!node_args_id)
            continue;
          const Json::Value* node_args =
              &args_builder_.GetArgs(node_args_id.value());
          for (const auto& arg_name : node_args->getMemberNames()) {
            const Json::Value& arg_value = (*node_args)[arg_name]["value"];
            if (arg_value.empty())
              continue;
            if (arg_value.isString()) {
              AddAttributeToMemoryNode(&event, path, arg_name,
                                       arg_value.asString());
            } else if (arg_value.isInt64()) {
              Json::Value unit = (*node_args)[arg_name]["unit"];
              if (unit.empty())
                unit = "unknown";
              AddAttributeToMemoryNode(&event, path, arg_name,
                                       arg_value.asInt64(), unit.asString());
            }
          }
        }

        const auto& snapshot_edges = storage_->memory_snapshot_edge_table();
        for (auto it = snapshot_edges.IterateRows(); it; ++it) {
          SnapshotNodeId source_node_id = it.source_node_id();
          auto source_node_rr = *sn.FindById(source_node_id);

          if (source_node_rr.process_snapshot_id() != process_snapshot_id) {
            continue;
          }
          Json::Value edge;
          edge["source"] =
              base::Uint64ToHexStringNoPrefix(it.source_node_id().value);
          edge["target"] =
              base::Uint64ToHexStringNoPrefix(it.target_node_id().value);
          edge["importance"] = Json::Int(it.importance());
          edge["type"] = "ownership";
          event["args"]["dumps"]["allocators_graph"].append(edge);
        }
        writer_.WriteCommonEvent(event);
      }
    }
    return base::OkStatus();
  }

  int64_t UpidToPid(UniquePid upid) {
    auto pid_it = upids_to_exported_pids_.find(upid);
    PERFETTO_DCHECK(pid_it != upids_to_exported_pids_.end());
    return pid_it->second;
  }

  std::pair<int64_t, int64_t> UtidToPidAndTid(UniqueTid utid) {
    auto pid_and_tid_it = utids_to_exported_pids_and_tids_.find(utid);
    PERFETTO_DCHECK(pid_and_tid_it != utids_to_exported_pids_and_tids_.end());
    return pid_and_tid_it->second;
  }

  int64_t NextExportedPidOrTidForDuplicates() {
    // Ensure that the exported substitute value does not represent a valid
    // pid/tid. This would be very unlikely in practice.
    while (IsValidPidOrTid(next_exported_pid_or_tid_for_duplicates_))
      next_exported_pid_or_tid_for_duplicates_--;
    return next_exported_pid_or_tid_for_duplicates_--;
  }

  bool IsValidPidOrTid(int64_t pid_or_tid) {
    const auto& process_table = storage_->process_table();
    for (auto it = process_table.IterateRows(); it; ++it) {
      if (it.pid() == pid_or_tid)
        return true;
    }

    const auto& thread_table = storage_->thread_table();
    for (auto it = thread_table.IterateRows(); it; ++it) {
      if (it.tid() == pid_or_tid)
        return true;
    }
    return false;
  }

  static Json::Value FillInProcessEventDetails(const Json::Value& event,
                                               int64_t pid) {
    Json::Value output = event;
    output["pid"] = Json::Int(pid);
    output["tid"] = Json::Int(-1);
    return output;
  }

  static void AddAttributeToMemoryNode(Json::Value* event,
                                       const std::string& path,
                                       const std::string& key,
                                       int64_t value,
                                       const std::string& units) {
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["value"] =
        base::Uint64ToHexStringNoPrefix(static_cast<uint64_t>(value));
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["type"] =
        "scalar";
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["units"] =
        units;
  }

  static void AddAttributeToMemoryNode(Json::Value* event,
                                       const std::string& path,
                                       const std::string& key,
                                       const std::string& value,
                                       const std::string& units = "") {
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["value"] =
        value;
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["type"] =
        "string";
    (*event)["args"]["dumps"]["allocators"][path]["attrs"][key]["units"] =
        units;
  }

  uint64_t GetCounterValue(TrackId track_id, int64_t ts) {
    const auto& counter_table = storage_->counter_table();
    // The timestamp column is sorted, so we can binary search for a matching
    // timestamp. Note that we don't want to use dataframe apis here as that
    // would bloat the binary size of the Chrome binary.
    uint32_t idx = LowerBoundIndex(0, counter_table.row_count(), ts,
                                   [&](uint32_t i, int64_t expected_ts) {
                                     return counter_table[i].ts() < expected_ts;
                                   });
    for (; idx < counter_table.row_count(); ++idx) {
      auto rr = counter_table[idx];
      if (rr.ts() != ts) {
        break;
      }
      if (rr.track_id() == track_id) {
        return static_cast<uint64_t>(rr.value());
      }
    }
    return 0;
  }

  const TraceStorage* storage_;
  ArgsBuilder args_builder_;
  TraceFormatWriter writer_;

  // If a pid/tid is duplicated between two or more  different processes/threads
  // (pid/tid reuse), we export the subsequent occurrences with different
  // pids/tids that is visibly different from regular pids/tids - counting down
  // from uint32_t max.
  int64_t next_exported_pid_or_tid_for_duplicates_ =
      std::numeric_limits<int64_t>::max();

  std::map<UniquePid, int64_t> upids_to_exported_pids_;
  std::map<int64_t, UniquePid> exported_pids_to_upids_;
  std::map<UniqueTid, std::pair<int64_t, int64_t>>
      utids_to_exported_pids_and_tids_;
  std::map<std::pair<int64_t, int64_t>, UniqueTid>
      exported_pids_and_tids_to_utids_;
};

#endif  // PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)

}  // namespace

OutputWriter::OutputWriter() = default;
OutputWriter::~OutputWriter() = default;

base::Status ExportJson(const TraceStorage* storage,
                        OutputWriter* output,
                        ArgumentFilterPredicate argument_filter,
                        MetadataFilterPredicate metadata_filter,
                        LabelFilterPredicate label_filter) {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
  JsonExporter exporter(storage, output, std::move(argument_filter),
                        std::move(metadata_filter), std::move(label_filter));
  return exporter.Export();
#else
  perfetto::base::ignore_result(storage);
  perfetto::base::ignore_result(output);
  perfetto::base::ignore_result(argument_filter);
  perfetto::base::ignore_result(metadata_filter);
  perfetto::base::ignore_result(label_filter);
  return base::ErrStatus("JSON support is not compiled in this build");
#endif  // PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
}

base::Status ExportJson(TraceProcessorStorage* tp,
                        OutputWriter* output,
                        ArgumentFilterPredicate argument_filter,
                        MetadataFilterPredicate metadata_filter,
                        LabelFilterPredicate label_filter) {
  const TraceStorage* storage = reinterpret_cast<TraceProcessorStorageImpl*>(tp)
                                    ->context()
                                    ->storage.get();
  return ExportJson(storage, output, std::move(argument_filter),
                    std::move(metadata_filter), std::move(label_filter));
}

base::Status ExportJson(const TraceStorage* storage, FILE* output) {
  FileWriter writer(output);
  return ExportJson(storage, &writer, nullptr, nullptr, nullptr);
}

}  // namespace perfetto::trace_processor::json
