// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/trace_config.h"

#include <stddef.h>

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_split.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "base/trace_event/trace_event.h"

namespace base {
namespace trace_event {

namespace {

// String options that can be used to initialize TraceOptions.
const char kRecordUntilFull[] = "record-until-full";
const char kRecordContinuously[] = "record-continuously";
const char kRecordAsMuchAsPossible[] = "record-as-much-as-possible";
const char kTraceToConsole[] = "trace-to-console";
const char kEnableSystrace[] = "enable-systrace";
const char kEnableArgumentFilter[] = "enable-argument-filter";

// String parameters that can be used to parse the trace config string.
const char kRecordModeParam[] = "record_mode";
const char kEnableSystraceParam[] = "enable_systrace";
const char kEnableArgumentFilterParam[] = "enable_argument_filter";

// String parameters that is used to parse memory dump config in trace config
// string.
const char kMemoryDumpConfigParam[] = "memory_dump_config";
const char kAllowedDumpModesParam[] = "allowed_dump_modes";
const char kTriggersParam[] = "triggers";
const char kTriggerModeParam[] = "mode";
const char kMinTimeBetweenDumps[] = "min_time_between_dumps_ms";
const char kTriggerTypeParam[] = "type";
const char kPeriodicIntervalLegacyParam[] = "periodic_interval_ms";
const char kHeapProfilerOptions[] = "heap_profiler_options";
const char kBreakdownThresholdBytes[] = "breakdown_threshold_bytes";

// String parameters used to parse category event filters.
const char kEventFiltersParam[] = "event_filters";
const char kFilterPredicateParam[] = "filter_predicate";
const char kFilterArgsParam[] = "filter_args";

class ConvertableTraceConfigToTraceFormat
    : public base::trace_event::ConvertableToTraceFormat {
 public:
  explicit ConvertableTraceConfigToTraceFormat(const TraceConfig& trace_config)
      : trace_config_(trace_config) {}

  ~ConvertableTraceConfigToTraceFormat() override = default;

  void AppendAsTraceFormat(std::string* out) const override {
    out->append(trace_config_.ToString());
  }

 private:
  const TraceConfig trace_config_;
};

std::set<MemoryDumpLevelOfDetail> GetDefaultAllowedMemoryDumpModes() {
  std::set<MemoryDumpLevelOfDetail> all_modes;
  for (uint32_t mode = static_cast<uint32_t>(MemoryDumpLevelOfDetail::FIRST);
       mode <= static_cast<uint32_t>(MemoryDumpLevelOfDetail::LAST); mode++) {
    all_modes.insert(static_cast<MemoryDumpLevelOfDetail>(mode));
  }
  return all_modes;
}

}  // namespace

TraceConfig::MemoryDumpConfig::HeapProfiler::HeapProfiler()
    : breakdown_threshold_bytes(kDefaultBreakdownThresholdBytes) {}

void TraceConfig::MemoryDumpConfig::HeapProfiler::Clear() {
  breakdown_threshold_bytes = kDefaultBreakdownThresholdBytes;
}

void TraceConfig::ResetMemoryDumpConfig(
    const TraceConfig::MemoryDumpConfig& memory_dump_config) {
  memory_dump_config_.Clear();
  memory_dump_config_ = memory_dump_config;
}

TraceConfig::MemoryDumpConfig::MemoryDumpConfig() = default;

TraceConfig::MemoryDumpConfig::MemoryDumpConfig(
    const MemoryDumpConfig& other) = default;

TraceConfig::MemoryDumpConfig::~MemoryDumpConfig() = default;

void TraceConfig::MemoryDumpConfig::Clear() {
  allowed_dump_modes.clear();
  triggers.clear();
  heap_profiler_options.Clear();
}

void TraceConfig::MemoryDumpConfig::Merge(
    const TraceConfig::MemoryDumpConfig& config) {
  triggers.insert(triggers.end(), config.triggers.begin(),
                  config.triggers.end());
  allowed_dump_modes.insert(config.allowed_dump_modes.begin(),
                            config.allowed_dump_modes.end());
  heap_profiler_options.breakdown_threshold_bytes =
      std::min(heap_profiler_options.breakdown_threshold_bytes,
               config.heap_profiler_options.breakdown_threshold_bytes);
}

TraceConfig::EventFilterConfig::EventFilterConfig(
    const std::string& predicate_name)
    : predicate_name_(predicate_name) {}

TraceConfig::EventFilterConfig::~EventFilterConfig() = default;

TraceConfig::EventFilterConfig::EventFilterConfig(const EventFilterConfig& tc) {
  *this = tc;
}

TraceConfig::EventFilterConfig& TraceConfig::EventFilterConfig::operator=(
    const TraceConfig::EventFilterConfig& rhs) {
  if (this == &rhs)
    return *this;

  predicate_name_ = rhs.predicate_name_;
  category_filter_ = rhs.category_filter_;

  if (rhs.args_)
    args_ = rhs.args_->CreateDeepCopy();

  return *this;
}

void TraceConfig::EventFilterConfig::InitializeFromConfigDict(
    const base::DictionaryValue* event_filter) {
  category_filter_.InitializeFromConfigDict(*event_filter);

  const base::DictionaryValue* args_dict = nullptr;
  if (event_filter->GetDictionary(kFilterArgsParam, &args_dict))
    args_ = args_dict->CreateDeepCopy();
}

void TraceConfig::EventFilterConfig::SetCategoryFilter(
    const TraceConfigCategoryFilter& category_filter) {
  category_filter_ = category_filter;
}

void TraceConfig::EventFilterConfig::ToDict(
    DictionaryValue* filter_dict) const {
  filter_dict->SetString(kFilterPredicateParam, predicate_name());

  category_filter_.ToDict(filter_dict);

  if (args_)
    filter_dict->Set(kFilterArgsParam, args_->CreateDeepCopy());
}

bool TraceConfig::EventFilterConfig::GetArgAsSet(
    const char* key,
    std::unordered_set<std::string>* out_set) const {
  const ListValue* list = nullptr;
  if (!args_->GetList(key, &list))
    return false;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    std::string value;
    if (list->GetString(i, &value))
      out_set->insert(value);
  }
  return true;
}

bool TraceConfig::EventFilterConfig::IsCategoryGroupEnabled(
    const StringPiece& category_group_name) const {
  return category_filter_.IsCategoryGroupEnabled(category_group_name);
}

TraceConfig::TraceConfig() {
  InitializeDefault();
}

TraceConfig::TraceConfig(StringPiece category_filter_string,
                         StringPiece trace_options_string) {
  InitializeFromStrings(category_filter_string, trace_options_string);
}

TraceConfig::TraceConfig(StringPiece category_filter_string,
                         TraceRecordMode record_mode) {
  std::string trace_options_string;
  switch (record_mode) {
    case RECORD_UNTIL_FULL:
      trace_options_string = kRecordUntilFull;
      break;
    case RECORD_CONTINUOUSLY:
      trace_options_string = kRecordContinuously;
      break;
    case RECORD_AS_MUCH_AS_POSSIBLE:
      trace_options_string = kRecordAsMuchAsPossible;
      break;
    case ECHO_TO_CONSOLE:
      trace_options_string = kTraceToConsole;
      break;
    default:
      NOTREACHED();
  }
  InitializeFromStrings(category_filter_string, trace_options_string);
}

TraceConfig::TraceConfig(const DictionaryValue& config) {
  InitializeFromConfigDict(config);
}

TraceConfig::TraceConfig(StringPiece config_string) {
  if (!config_string.empty())
    InitializeFromConfigString(config_string);
  else
    InitializeDefault();
}

TraceConfig::TraceConfig(const TraceConfig& tc) = default;

TraceConfig::~TraceConfig() = default;

TraceConfig& TraceConfig::operator=(const TraceConfig& rhs) {
  if (this == &rhs)
    return *this;

  record_mode_ = rhs.record_mode_;
  enable_systrace_ = rhs.enable_systrace_;
  enable_argument_filter_ = rhs.enable_argument_filter_;
  category_filter_ = rhs.category_filter_;
  memory_dump_config_ = rhs.memory_dump_config_;
  event_filters_ = rhs.event_filters_;
  return *this;
}

std::string TraceConfig::ToString() const {
  std::unique_ptr<DictionaryValue> dict = ToDict();
  std::string json;
  JSONWriter::Write(*dict, &json);
  return json;
}

std::unique_ptr<ConvertableToTraceFormat>
TraceConfig::AsConvertableToTraceFormat() const {
  return std::make_unique<ConvertableTraceConfigToTraceFormat>(*this);
}

std::string TraceConfig::ToCategoryFilterString() const {
  return category_filter_.ToFilterString();
}

bool TraceConfig::IsCategoryGroupEnabled(
    const StringPiece& category_group_name) const {
  // TraceLog should call this method only as part of enabling/disabling
  // categories.
  return category_filter_.IsCategoryGroupEnabled(category_group_name);
}

void TraceConfig::Merge(const TraceConfig& config) {
  if (record_mode_ != config.record_mode_
      || enable_systrace_ != config.enable_systrace_
      || enable_argument_filter_ != config.enable_argument_filter_) {
    DLOG(ERROR) << "Attempting to merge trace config with a different "
                << "set of options.";
  }

  category_filter_.Merge(config.category_filter_);

  memory_dump_config_.Merge(config.memory_dump_config_);

  event_filters_.insert(event_filters_.end(), config.event_filters().begin(),
                        config.event_filters().end());
}

void TraceConfig::Clear() {
  record_mode_ = RECORD_UNTIL_FULL;
  enable_systrace_ = false;
  enable_argument_filter_ = false;
  category_filter_.Clear();
  memory_dump_config_.Clear();
  event_filters_.clear();
}

void TraceConfig::InitializeDefault() {
  record_mode_ = RECORD_UNTIL_FULL;
  enable_systrace_ = false;
  enable_argument_filter_ = false;
}

void TraceConfig::InitializeFromConfigDict(const DictionaryValue& dict) {
  record_mode_ = RECORD_UNTIL_FULL;
  std::string record_mode;
  if (dict.GetString(kRecordModeParam, &record_mode)) {
    if (record_mode == kRecordUntilFull) {
      record_mode_ = RECORD_UNTIL_FULL;
    } else if (record_mode == kRecordContinuously) {
      record_mode_ = RECORD_CONTINUOUSLY;
    } else if (record_mode == kTraceToConsole) {
      record_mode_ = ECHO_TO_CONSOLE;
    } else if (record_mode == kRecordAsMuchAsPossible) {
      record_mode_ = RECORD_AS_MUCH_AS_POSSIBLE;
    }
  }

  bool val;
  enable_systrace_ = dict.GetBoolean(kEnableSystraceParam, &val) ? val : false;
  enable_argument_filter_ =
      dict.GetBoolean(kEnableArgumentFilterParam, &val) ? val : false;

  category_filter_.InitializeFromConfigDict(dict);

  const base::ListValue* category_event_filters = nullptr;
  if (dict.GetList(kEventFiltersParam, &category_event_filters))
    SetEventFiltersFromConfigList(*category_event_filters);

  if (category_filter_.IsCategoryEnabled(MemoryDumpManager::kTraceCategory)) {
    // If dump triggers not set, the client is using the legacy with just
    // category enabled. So, use the default periodic dump config.
    const DictionaryValue* memory_dump_config = nullptr;
    if (dict.GetDictionary(kMemoryDumpConfigParam, &memory_dump_config))
      SetMemoryDumpConfigFromConfigDict(*memory_dump_config);
    else
      SetDefaultMemoryDumpConfig();
  }
}

void TraceConfig::InitializeFromConfigString(StringPiece config_string) {
  auto dict = DictionaryValue::From(JSONReader::Read(config_string));
  if (dict)
    InitializeFromConfigDict(*dict);
  else
    InitializeDefault();
}

void TraceConfig::InitializeFromStrings(StringPiece category_filter_string,
                                        StringPiece trace_options_string) {
  if (!category_filter_string.empty())
    category_filter_.InitializeFromString(category_filter_string);

  record_mode_ = RECORD_UNTIL_FULL;
  enable_systrace_ = false;
  enable_argument_filter_ = false;
  if (!trace_options_string.empty()) {
    std::vector<std::string> split =
        SplitString(trace_options_string, ",", TRIM_WHITESPACE, SPLIT_WANT_ALL);
    for (const std::string& token : split) {
      if (token == kRecordUntilFull) {
        record_mode_ = RECORD_UNTIL_FULL;
      } else if (token == kRecordContinuously) {
        record_mode_ = RECORD_CONTINUOUSLY;
      } else if (token == kTraceToConsole) {
        record_mode_ = ECHO_TO_CONSOLE;
      } else if (token == kRecordAsMuchAsPossible) {
        record_mode_ = RECORD_AS_MUCH_AS_POSSIBLE;
      } else if (token == kEnableSystrace) {
        enable_systrace_ = true;
      } else if (token == kEnableArgumentFilter) {
        enable_argument_filter_ = true;
      }
    }
  }

  if (category_filter_.IsCategoryEnabled(MemoryDumpManager::kTraceCategory)) {
    SetDefaultMemoryDumpConfig();
  }
}

void TraceConfig::SetMemoryDumpConfigFromConfigDict(
    const DictionaryValue& memory_dump_config) {
  // Set allowed dump modes.
  memory_dump_config_.allowed_dump_modes.clear();
  const ListValue* allowed_modes_list;
  if (memory_dump_config.GetList(kAllowedDumpModesParam, &allowed_modes_list)) {
    for (size_t i = 0; i < allowed_modes_list->GetSize(); ++i) {
      std::string level_of_detail_str;
      allowed_modes_list->GetString(i, &level_of_detail_str);
      memory_dump_config_.allowed_dump_modes.insert(
          StringToMemoryDumpLevelOfDetail(level_of_detail_str));
    }
  } else {
    // If allowed modes param is not given then allow all modes by default.
    memory_dump_config_.allowed_dump_modes = GetDefaultAllowedMemoryDumpModes();
  }

  // Set triggers
  memory_dump_config_.triggers.clear();
  const ListValue* trigger_list = nullptr;
  if (memory_dump_config.GetList(kTriggersParam, &trigger_list) &&
      trigger_list->GetSize() > 0) {
    for (size_t i = 0; i < trigger_list->GetSize(); ++i) {
      const DictionaryValue* trigger = nullptr;
      if (!trigger_list->GetDictionary(i, &trigger))
        continue;

      MemoryDumpConfig::Trigger dump_config;
      int interval = 0;
      if (!trigger->GetInteger(kMinTimeBetweenDumps, &interval)) {
        // If "min_time_between_dumps_ms" param was not given, then the trace
        // config uses old format where only periodic dumps are supported.
        trigger->GetInteger(kPeriodicIntervalLegacyParam, &interval);
        dump_config.trigger_type = MemoryDumpType::PERIODIC_INTERVAL;
      } else {
        std::string trigger_type_str;
        trigger->GetString(kTriggerTypeParam, &trigger_type_str);
        dump_config.trigger_type = StringToMemoryDumpType(trigger_type_str);
      }
      DCHECK_GT(interval, 0);
      dump_config.min_time_between_dumps_ms = static_cast<uint32_t>(interval);

      std::string level_of_detail_str;
      trigger->GetString(kTriggerModeParam, &level_of_detail_str);
      dump_config.level_of_detail =
          StringToMemoryDumpLevelOfDetail(level_of_detail_str);

      memory_dump_config_.triggers.push_back(dump_config);
    }
  }

  // Set heap profiler options
  const DictionaryValue* heap_profiler_options = nullptr;
  if (memory_dump_config.GetDictionary(kHeapProfilerOptions,
                                       &heap_profiler_options)) {
    int min_size_bytes = 0;
    if (heap_profiler_options->GetInteger(kBreakdownThresholdBytes,
                                         &min_size_bytes)
        && min_size_bytes >= 0) {
      memory_dump_config_.heap_profiler_options.breakdown_threshold_bytes =
          static_cast<size_t>(min_size_bytes);
    } else {
      memory_dump_config_.heap_profiler_options.breakdown_threshold_bytes =
          MemoryDumpConfig::HeapProfiler::kDefaultBreakdownThresholdBytes;
    }
  }
}

void TraceConfig::SetDefaultMemoryDumpConfig() {
  memory_dump_config_.Clear();
  memory_dump_config_.allowed_dump_modes = GetDefaultAllowedMemoryDumpModes();
}

void TraceConfig::SetEventFiltersFromConfigList(
    const base::ListValue& category_event_filters) {
  event_filters_.clear();

  for (size_t event_filter_index = 0;
       event_filter_index < category_event_filters.GetSize();
       ++event_filter_index) {
    const base::DictionaryValue* event_filter = nullptr;
    if (!category_event_filters.GetDictionary(event_filter_index,
                                              &event_filter))
      continue;

    std::string predicate_name;
    CHECK(event_filter->GetString(kFilterPredicateParam, &predicate_name))
        << "Invalid predicate name in category event filter.";

    EventFilterConfig new_config(predicate_name);
    new_config.InitializeFromConfigDict(event_filter);
    event_filters_.push_back(new_config);
  }
}

std::unique_ptr<DictionaryValue> TraceConfig::ToDict() const {
  auto dict = std::make_unique<DictionaryValue>();
  switch (record_mode_) {
    case RECORD_UNTIL_FULL:
      dict->SetString(kRecordModeParam, kRecordUntilFull);
      break;
    case RECORD_CONTINUOUSLY:
      dict->SetString(kRecordModeParam, kRecordContinuously);
      break;
    case RECORD_AS_MUCH_AS_POSSIBLE:
      dict->SetString(kRecordModeParam, kRecordAsMuchAsPossible);
      break;
    case ECHO_TO_CONSOLE:
      dict->SetString(kRecordModeParam, kTraceToConsole);
      break;
    default:
      NOTREACHED();
  }

  dict->SetBoolean(kEnableSystraceParam, enable_systrace_);
  dict->SetBoolean(kEnableArgumentFilterParam, enable_argument_filter_);

  category_filter_.ToDict(dict.get());

  if (!event_filters_.empty()) {
    std::unique_ptr<base::ListValue> filter_list(new base::ListValue());
    for (const EventFilterConfig& filter : event_filters_) {
      std::unique_ptr<base::DictionaryValue> filter_dict(
          new base::DictionaryValue());
      filter.ToDict(filter_dict.get());
      filter_list->Append(std::move(filter_dict));
    }
    dict->Set(kEventFiltersParam, std::move(filter_list));
  }

  if (category_filter_.IsCategoryEnabled(MemoryDumpManager::kTraceCategory)) {
    auto allowed_modes = std::make_unique<ListValue>();
    for (auto dump_mode : memory_dump_config_.allowed_dump_modes)
      allowed_modes->AppendString(MemoryDumpLevelOfDetailToString(dump_mode));

    auto memory_dump_config = std::make_unique<DictionaryValue>();
    memory_dump_config->Set(kAllowedDumpModesParam, std::move(allowed_modes));

    auto triggers_list = std::make_unique<ListValue>();
    for (const auto& config : memory_dump_config_.triggers) {
      auto trigger_dict = std::make_unique<DictionaryValue>();
      trigger_dict->SetString(kTriggerTypeParam,
                              MemoryDumpTypeToString(config.trigger_type));
      trigger_dict->SetInteger(
          kMinTimeBetweenDumps,
          static_cast<int>(config.min_time_between_dumps_ms));
      trigger_dict->SetString(
          kTriggerModeParam,
          MemoryDumpLevelOfDetailToString(config.level_of_detail));
      triggers_list->Append(std::move(trigger_dict));
    }

    // Empty triggers will still be specified explicitly since it means that
    // the periodic dumps are not enabled.
    memory_dump_config->Set(kTriggersParam, std::move(triggers_list));

    if (memory_dump_config_.heap_profiler_options.breakdown_threshold_bytes !=
        MemoryDumpConfig::HeapProfiler::kDefaultBreakdownThresholdBytes) {
      auto options = std::make_unique<DictionaryValue>();
      options->SetInteger(
          kBreakdownThresholdBytes,
          memory_dump_config_.heap_profiler_options.breakdown_threshold_bytes);
      memory_dump_config->Set(kHeapProfilerOptions, std::move(options));
    }
    dict->Set(kMemoryDumpConfigParam, std::move(memory_dump_config));
  }
  return dict;
}

std::string TraceConfig::ToTraceOptionsString() const {
  std::string ret;
  switch (record_mode_) {
    case RECORD_UNTIL_FULL:
      ret = kRecordUntilFull;
      break;
    case RECORD_CONTINUOUSLY:
      ret = kRecordContinuously;
      break;
    case RECORD_AS_MUCH_AS_POSSIBLE:
      ret = kRecordAsMuchAsPossible;
      break;
    case ECHO_TO_CONSOLE:
      ret = kTraceToConsole;
      break;
    default:
      NOTREACHED();
  }
  if (enable_systrace_)
    ret = ret + "," + kEnableSystrace;
  if (enable_argument_filter_)
    ret = ret + "," + kEnableArgumentFilter;
  return ret;
}

}  // namespace trace_event
}  // namespace base
