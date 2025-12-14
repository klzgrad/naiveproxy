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

#include "src/traced/probes/ftrace/ftrace_config_muxer.h"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <cctype>
#include <cstdint>

#include <algorithm>
#include <iterator>
#include <limits>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/utils.h"
#include "protos/perfetto/config/ftrace/ftrace_config.gen.h"
#include "protos/perfetto/trace/ftrace/generic.pbzero.h"
#include "src/traced/probes/ftrace/atrace_wrapper.h"
#include "src/traced/probes/ftrace/compact_sched.h"
#include "src/traced/probes/ftrace/ftrace_config_utils.h"
#include "src/traced/probes/ftrace/ftrace_stats.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"

namespace perfetto {
namespace {

using protos::pbzero::KprobeEvent;

constexpr uint64_t kDefaultLowRamPerCpuBufferSizeKb = 2 * (1ULL << 10);   // 2mb
constexpr uint64_t kDefaultHighRamPerCpuBufferSizeKb = 8 * (1ULL << 10);  // 8mb

// Threshold for physical ram size used when deciding on default kernel buffer
// sizes. We want to detect 8 GB, but the size reported through sysconf is
// usually lower.
constexpr uint64_t kHighMemBytes = 7 * (1ULL << 30);  // 7gb

// A fake "syscall id" that indicates all syscalls should be recorded. This
// allows us to distinguish between the case where `syscall_events` is empty
// because raw_syscalls aren't enabled, or the case where it is and we want to
// record all events.
constexpr size_t kAllSyscallsId = kMaxSyscalls + 1;

// trace_clocks in preference order.
// If this list is changed, the FtraceClocks enum in ftrace_event_bundle.proto
// and FtraceConfigMuxer::SetupClock() should be also changed accordingly.
constexpr const char* kClocks[] = {"boot", "global", "local"};

// optional monotonic raw clock.
// Enabled by the "use_monotonic_raw_clock" option in the ftrace config.
constexpr const char* kClockMonoRaw = "mono_raw";

std::set<GroupAndName> ReadEventsInGroupFromFs(const Tracefs& tracefs,
                                               const std::string& group) {
  std::set<std::string> names =
      tracefs.GetEventNamesForGroup("events/" + group);
  std::set<GroupAndName> events;
  for (const auto& name : names)
    events.insert(GroupAndName(group, name));
  return events;
}

std::pair<std::string, std::string> EventToStringGroupAndName(
    const std::string& event) {
  auto slash_pos = event.find('/');
  if (slash_pos == std::string::npos)
    return std::make_pair("", event);
  return std::make_pair(event.substr(0, slash_pos),
                        event.substr(slash_pos + 1));
}

void UnionInPlace(const std::vector<std::string>& unsorted_a,
                  std::vector<std::string>* out) {
  std::vector<std::string> a = unsorted_a;
  std::sort(a.begin(), a.end());
  std::sort(out->begin(), out->end());
  std::vector<std::string> v;
  std::set_union(a.begin(), a.end(), out->begin(), out->end(),
                 std::back_inserter(v));
  *out = std::move(v);
}

void IntersectInPlace(const std::vector<std::string>& unsorted_a,
                      std::vector<std::string>* out) {
  std::vector<std::string> a = unsorted_a;
  std::sort(a.begin(), a.end());
  std::sort(out->begin(), out->end());
  std::vector<std::string> v;
  std::set_intersection(a.begin(), a.end(), out->begin(), out->end(),
                        std::back_inserter(v));
  *out = std::move(v);
}

std::vector<std::string> Subtract(const std::vector<std::string>& unsorted_a,
                                  const std::vector<std::string>& unsorted_b) {
  std::vector<std::string> a = unsorted_a;
  std::sort(a.begin(), a.end());
  std::vector<std::string> b = unsorted_b;
  std::sort(b.begin(), b.end());
  std::vector<std::string> v;
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                      std::back_inserter(v));
  return v;
}

// This is just to reduce binary size and stack frame size of the insertions.
// It effectively undoes STL's set::insert inlining.
void PERFETTO_NO_INLINE InsertEvent(const char* group,
                                    const char* name,
                                    std::set<GroupAndName>* dst) {
  dst->insert(GroupAndName(group, name));
}

std::map<GroupAndName, KprobeEvent::KprobeType> GetFtraceKprobeEvents(
    const FtraceConfig& request) {
  using CFG = protos::gen::FtraceConfig::KprobeEvent;
  using TRACE = protos::pbzero::KprobeEvent;

  std::map<GroupAndName, TRACE::KprobeType> events;
  auto add_kprobe = [&events](const std::string& name, auto type) {
    events.emplace(GroupAndName(kKprobeGroup, name), type);
  };
  auto add_kretprobe = [&events](const std::string& name, auto type) {
    events.emplace(GroupAndName(kKretprobeGroup, name), type);
  };

  for (const auto& cfg_evt : request.kprobe_events()) {
    switch (cfg_evt.type()) {
      case CFG::KPROBE_TYPE_KPROBE:
        add_kprobe(cfg_evt.probe(), TRACE::KPROBE_TYPE_INSTANT);
        break;
      case CFG::KPROBE_TYPE_KRETPROBE:
        add_kretprobe(cfg_evt.probe(), TRACE::KPROBE_TYPE_INSTANT);
        break;
      case CFG::KPROBE_TYPE_BOTH:
        add_kprobe(cfg_evt.probe(), TRACE::KPROBE_TYPE_BEGIN);
        add_kretprobe(cfg_evt.probe(), TRACE::KPROBE_TYPE_END);
        break;
      case CFG::KPROBE_TYPE_UNKNOWN:
        PERFETTO_DLOG("Unknown kprobe event");
        break;
    }
    PERFETTO_DLOG("Added kprobe event: %s", cfg_evt.probe().c_str());
  }
  return events;
}

bool ValidateKprobeName(const std::string& name) {
  return std::all_of(name.begin(), name.end(),
                     [](char c) { return std::isalnum(c) || c == '_'; });
}

// See: "Exclusive single-tenant features" in ftrace_config.proto for more
// details.
bool HasExclusiveFeatures(const FtraceConfig& request) {
  return !request.tids_to_trace().empty() ||
         !request.tracefs_options().empty() ||
         !request.tracing_cpumask().empty();
}

bool IsValidTracefsOptionName(const std::string& name) {
  return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
    return std::isalnum(c) || c == '-' || c == '_';
  });
}

}  // namespace

std::set<GroupAndName> FtraceConfigMuxer::GetFtraceEvents(
    const FtraceConfig& request,
    const ProtoTranslationTable* table) {
  std::set<GroupAndName> events;
  for (const auto& config_value : request.ftrace_events()) {
    std::string group;
    std::string name;
    std::tie(group, name) = EventToStringGroupAndName(config_value);
    if (name == "*") {
      for (const auto& event : ReadEventsInGroupFromFs(*tracefs_, group))
        events.insert(event);
    } else if (group.empty()) {
      // If there is no group specified, find an event with that name and
      // use it's group.
      const Event* e = table->GetEventByName(name);
      if (!e) {
        PERFETTO_DLOG(
            "Event doesn't exist: %s. Include the group in the config to allow "
            "the event to be output as a generic event.",
            name.c_str());
        continue;
      }
      events.insert(GroupAndName(e->group, e->name));
    } else {
      events.insert(GroupAndName(group, name));
    }
  }

  if (RequiresAtrace(request)) {
    InsertEvent("ftrace", "print", &events);
  }
  if (!request.atrace_userspace_only()) {
    // Legacy: some atrace categories enable not just userspace tracing, but
    // also a predefined set of kernel tracepoints, as that's what the original
    // "atrace" binary did.
    for (const std::string& category : request.atrace_categories()) {
      if (predefined_events_.count(category)) {
        for (const GroupAndName& event : predefined_events_[category]) {
          events.insert(event);
        }
      }
    }

    // Android: vendors can provide a set of extra ftrace categories to be
    // enabled when a specific atrace category is used
    // (e.g. "gfx" -> ["my_hw/my_custom_event", "my_hw/my_special_gpu"]).
    for (const std::string& category : request.atrace_categories()) {
      if (vendor_events_.count(category)) {
        for (const GroupAndName& event : vendor_events_[category]) {
          events.insert(event);
        }
      }
    }
  }

  // recording a subset of syscalls -> enable the backing events
  if (request.syscall_events_size() > 0) {
    InsertEvent("raw_syscalls", "sys_enter", &events);
    InsertEvent("raw_syscalls", "sys_exit", &events);
  }

  // function_graph tracer emits two builtin ftrace events
  if (request.enable_function_graph()) {
    InsertEvent("ftrace", "funcgraph_entry", &events);
    InsertEvent("ftrace", "funcgraph_exit", &events);
  }

  // If throttle_rss_stat: true, use the rss_stat_throttled event if supported
  if (request.throttle_rss_stat() && tracefs_->SupportsRssStatThrottled()) {
    auto it = std::find_if(
        events.begin(), events.end(), [](const GroupAndName& event) {
          return event.group() == "kmem" && event.name() == "rss_stat";
        });

    if (it != events.end()) {
      events.erase(it);
      InsertEvent("synthetic", "rss_stat_throttled", &events);
    }
  }

  return events;
}

base::FlatSet<int64_t> FtraceConfigMuxer::GetSyscallsReturningFds(
    const SyscallTable& syscalls) {
  auto insertSyscallId = [&syscalls](base::FlatSet<int64_t>& set,
                                     const char* syscall) {
    auto syscall_id = syscalls.GetByName(syscall);
    if (syscall_id)
      set.insert(static_cast<int64_t>(*syscall_id));
  };

  base::FlatSet<int64_t> call_ids;
  insertSyscallId(call_ids, "sys_open");
  insertSyscallId(call_ids, "sys_openat");
  insertSyscallId(call_ids, "sys_socket");
  insertSyscallId(call_ids, "sys_dup");
  insertSyscallId(call_ids, "sys_dup2");
  insertSyscallId(call_ids, "sys_dup3");
  return call_ids;
}

bool FtraceConfigMuxer::FilterHasGroup(const EventFilter& filter,
                                       const std::string& group) {
  const std::vector<const Event*>* events = table_->GetEventsByGroup(group);
  if (!events) {
    return false;
  }

  for (const Event* event : *events) {
    if (filter.IsEventEnabled(event->ftrace_event_id)) {
      return true;
    }
  }
  return false;
}

EventFilter FtraceConfigMuxer::BuildSyscallFilter(
    const EventFilter& ftrace_filter,
    const FtraceConfig& request) {
  EventFilter output;

  if (!FilterHasGroup(ftrace_filter, "raw_syscalls")) {
    return output;
  }

  if (request.syscall_events().empty()) {
    output.AddEnabledEvent(kAllSyscallsId);
    return output;
  }

  for (const std::string& syscall : request.syscall_events()) {
    std::optional<size_t> id = syscalls_.GetByName(syscall);
    if (!id.has_value()) {
      PERFETTO_ELOG("Can't enable %s, syscall not known", syscall.c_str());
      continue;
    }
    output.AddEnabledEvent(*id);
  }

  return output;
}

bool FtraceConfigMuxer::SetSyscallEventFilter(
    const EventFilter& extra_syscalls) {
  EventFilter syscall_filter;

  syscall_filter.EnableEventsFrom(extra_syscalls);
  for (const auto& id_config : ds_configs_) {
    const perfetto::FtraceDataSourceConfig& config = id_config.second;
    syscall_filter.EnableEventsFrom(config.syscall_filter);
  }

  std::set<size_t> filter_set = syscall_filter.GetEnabledEvents();
  if (syscall_filter.IsEventEnabled(kAllSyscallsId)) {
    filter_set.clear();
  }

  if (current_state_.syscall_filter != filter_set) {
    if (!tracefs_->SetSyscallFilter(filter_set)) {
      return false;
    }

    current_state_.syscall_filter = filter_set;
  }

  return true;
}

void FtraceConfigMuxer::EnableFtraceEvent(const Event* event,
                                          const GroupAndName& group_and_name,
                                          EventFilter* filter,
                                          FtraceSetupErrors* errors) {
  // Note: ftrace events are always implicitly enabled (and don't have an
  // "enable" file). So they aren't tracked by the central event filter (but
  // still need to be added to the per data source event filter to retain
  // the events during parsing).
  if (current_state_.ftrace_events.IsEventEnabled(event->ftrace_event_id) ||
      std::string("ftrace") == event->group) {
    filter->AddEnabledEvent(event->ftrace_event_id);
    return;
  }
  if (tracefs_->EnableEvent(event->group, event->name)) {
    current_state_.ftrace_events.AddEnabledEvent(event->ftrace_event_id);
    filter->AddEnabledEvent(event->ftrace_event_id);
  } else {
    PERFETTO_DPLOG("Failed to enable %s.", group_and_name.ToString().c_str());
    if (errors)
      errors->failed_ftrace_events.push_back(group_and_name.ToString());
  }
}

FtraceConfigMuxer::FtraceConfigMuxer(
    Tracefs* tracefs,
    AtraceWrapper* atrace_wrapper,
    ProtoTranslationTable* table,
    SyscallTable syscalls,
    std::map<std::string, base::FlatSet<GroupAndName>> predefined_events,
    std::map<std::string, std::vector<GroupAndName>> vendor_events,
    bool secondary_instance)
    : tracefs_(tracefs),
      atrace_wrapper_(atrace_wrapper),
      table_(table),
      syscalls_(syscalls),
      current_state_(),
      predefined_events_(std::move(predefined_events)),
      vendor_events_(std::move(vendor_events)),
      secondary_instance_(secondary_instance) {}
FtraceConfigMuxer::~FtraceConfigMuxer() = default;

bool FtraceConfigMuxer::SetupConfig(FtraceConfigId id,
                                    const FtraceConfig& request,
                                    FtraceSetupErrors* errors) {
  EventFilter filter;
  bool config_has_exclusive_features = HasExclusiveFeatures(request);
  if (ds_configs_.empty()) {
    PERFETTO_DCHECK(active_configs_.empty());

    // If someone outside of perfetto is using a non-nop tracer, yield. We can't
    // realistically figure out all notions of "in use" even if we look at
    // set_event or events/enable, so this is all we check for.
    if (!request.preserve_ftrace_buffer() && !tracefs_->IsTracingAvailable()) {
      PERFETTO_ELOG(
          "ftrace in use by non-Perfetto. Check that %s current_tracer is nop.",
          tracefs_->GetRootPath().c_str());
      return false;
    }

    // Clear tracefs state, remembering which value of "tracing_on" to restore
    // to after we're done, though we won't restore the rest of the tracefs
    // state.
    current_state_.saved_tracing_on = tracefs_->GetTracingOn();
    if (!request.preserve_ftrace_buffer()) {
      tracefs_->SetTracingOn(false);
      // Android: this will fail on release ("user") builds due to ACLs, but
      // that's acceptable since the per-event enabling/disabling should still
      // be balanced.
      tracefs_->DisableAllEvents();
      tracefs_->ClearTrace();
    }

    // Set up the new tracefs state, without starting recording.
    if (!request.preserve_ftrace_buffer()) {
      SetupClock(request);
      SetupBufferSize(request);
    } else {
      // If preserving the existing ring buffer contents, we cannot change the
      // clock or buffer sizes because that clears the kernel buffers.
      RememberActiveClock();
    }
  } else {
    std::string exclusive_feature_error;
    if (config_has_exclusive_features) {
      exclusive_feature_error =
          "Attempted to start an ftrace session with advanced features while "
          "another session was active.";
    } else if (current_state_.exclusive_feature_active) {
      exclusive_feature_error =
          "Attempted to start an ftrace session while another session with "
          "advanced features was active.";
    }

    if (!exclusive_feature_error.empty()) {
      PERFETTO_ELOG("%s", exclusive_feature_error.c_str());
      if (errors)
        errors->exclusive_feature_error = exclusive_feature_error;
      return false;
    }
  }

  if (!request.tids_to_trace().empty()) {
    std::vector<std::string> tid_strings;
    for (const auto& tid : request.tids_to_trace()) {
      tid_strings.push_back(std::to_string(tid));
    }

    if (!tracefs_->SetEventTidFilter(tid_strings)) {
      PERFETTO_ELOG("Failed to set event tid filter");
      return false;
    }
  }

  if (!request.tracefs_options().empty()) {
    base::FlatHashMap<std::string, bool> current_tracefs_options;
    for (const auto& tracefs_option : request.tracefs_options()) {
      // Skip unset options.
      if (tracefs_option.state() ==
          FtraceConfig::TracefsOption::STATE_UNKNOWN) {
        continue;
      }
      const auto& name = tracefs_option.name();
      if (!IsValidTracefsOptionName(name)) {
        PERFETTO_ELOG(
            "Invalid tracefs option name: %s. The string can only contain "
            "alphanumeric characters, hyphens and underscores.",
            name.c_str());
        return false;
      }
      // Get the current option state and save it for later.
      auto option_state = tracefs_->GetTracefsOption(name);
      if (!option_state.has_value()) {
        PERFETTO_ELOG("Tracefs option not found: %s", name.c_str());
        return false;
      }
      current_tracefs_options[name] = option_state.value();

      bool new_state =
          tracefs_option.state() == FtraceConfig::TracefsOption::STATE_ENABLED;
      if (!tracefs_->SetTracefsOption(name, new_state)) {
        PERFETTO_ELOG("Failed to set tracefs option: %s", name.c_str());
        return false;
      }
    }
    current_state_.saved_tracefs_options = std::move(current_tracefs_options);
  }

  if (!request.tracing_cpumask().empty()) {
    auto current_tracing_cpumask = tracefs_->GetTracingCpuMask();
    if (!current_tracing_cpumask.has_value()) {
      PERFETTO_ELOG("Failed to get tracing cpumask");
      return false;
    }
    if (!tracefs_->SetTracingCpuMask(request.tracing_cpumask())) {
      PERFETTO_ELOG("Failed to set tracing cpumask: %s",
                    request.tracing_cpumask().c_str());
      return false;
    }
    current_state_.saved_tracing_cpumask =
        std::move(current_tracing_cpumask.value());
  }

  current_state_.exclusive_feature_active = config_has_exclusive_features;

  std::set<GroupAndName> events = GetFtraceEvents(request, table_);

  // Android: update userspace tracing control state if necessary.
  if (RequiresAtrace(request)) {
    if (secondary_instance_) {
      PERFETTO_ELOG(
          "Secondary ftrace instances do not support atrace_categories and "
          "atrace_apps options as they affect global state");
      return false;
    }
    if (!atrace_wrapper_->SupportsUserspaceOnly() && !ds_configs_.empty()) {
      PERFETTO_ELOG(
          "Concurrent atrace sessions are not supported before Android P, "
          "bailing out.");
      return false;
    }
    UpdateAtrace(request, errors ? &errors->atrace_errors : nullptr);
  }

  // Set up and enable kprobe events.
  std::map<GroupAndName, KprobeEvent::KprobeType> events_kprobes =
      GetFtraceKprobeEvents(request);
  base::FlatHashMap<uint32_t, KprobeEvent::KprobeType> kprobes;
  for (const auto& [group_and_name, type] : events_kprobes) {
    if (!ValidateKprobeName(group_and_name.name())) {
      PERFETTO_ELOG("Invalid kprobes event %s", group_and_name.name().c_str());
      if (errors)
        errors->failed_ftrace_events.push_back(group_and_name.ToString());
      continue;
    }
    // Create kprobe in the kernel by writing to the tracefs.
    if (!tracefs_->CreateKprobeEvent(
            group_and_name.group(), group_and_name.name(),
            group_and_name.group() == kKretprobeGroup)) {
      PERFETTO_ELOG("Failed creation of kprobes event %s",
                    group_and_name.name().c_str());
      if (errors)
        errors->failed_ftrace_events.push_back(group_and_name.ToString());
      continue;
    }
    // Create the mapping in ProtoTranslationTable.
    const Event* event = table_->GetEvent(group_and_name);
    if (!event) {
      event = table_->CreateKprobeEvent(group_and_name);
    }
    if (!event || event->proto_field_id !=
                      protos::pbzero::FtraceEvent::kKprobeEventFieldNumber) {
      tracefs_->RemoveKprobeEvent(group_and_name.group(),
                                  group_and_name.name());

      PERFETTO_ELOG("Can't enable kprobe %s",
                    group_and_name.ToString().c_str());
      if (errors)
        errors->unknown_ftrace_events.push_back(group_and_name.ToString());
      continue;
    }
    current_state_.installed_kprobes.insert(group_and_name);
    EnableFtraceEvent(event, group_and_name, &filter, errors);
    kprobes[event->ftrace_event_id] = type;
  }

  // Enable ftrace events.
  for (const auto& group_and_name : events) {
    // Kprobe group is reserved.
    if (group_and_name.group() == kKprobeGroup ||
        group_and_name.group() == kKretprobeGroup) {
      continue;
    }

    const Event* event = table_->GetEvent(group_and_name);
    // If it's neither known at compile-time nor already created, create a
    // generic proto description.
    if (!event) {
      event = table_->CreateGenericEvent(group_and_name);
    }
    // Niche option to skip such generic events (still creating the entry helps
    // distinguish skipped vs unknown events).
    if (request.disable_generic_events() && event &&
        table_->IsGenericEventProtoId(event->proto_field_id)) {
      if (errors)
        errors->failed_ftrace_events.push_back(group_and_name.ToString());
      continue;
    }
    // Skip, event doesn't exist or is inaccessible.
    if (!event) {
      PERFETTO_DLOG("Can't enable %s, event not known",
                    group_and_name.ToString().c_str());
      if (errors)
        errors->unknown_ftrace_events.push_back(group_and_name.ToString());
      continue;
    }

    EnableFtraceEvent(event, group_and_name, &filter, errors);
  }

  // Syscall tracing via kernel-filtered "raw_syscalls" tracepoint.
  EventFilter syscall_filter = BuildSyscallFilter(filter, request);
  if (!SetSyscallEventFilter(syscall_filter)) {
    PERFETTO_ELOG("Failed to set raw_syscall ftrace filter in SetupConfig");
    return false;
  }

  // Kernel function tracing (function_graph).
  // Note 1: there is no cleanup in |RemoveConfig| because tracers cannot be
  // changed while tracing pipes are opened. So we'll keep the current_tracer
  // until all data sources are gone, at which point ftrace_controller will
  // make an explicit call to |ResetCurrentTracer|.
  // Note 2: we don't track the set of filters ourselves and instead let the
  // kernel statefully collate them, hence the use of |AppendFunctionFilters|.
  // This is because each concurrent data source that wants funcgraph will get
  // all of the enabled functions (we don't go as far as doing per-DS event
  // steering in the parser), and we don't want to remove functions midway
  // through a trace (but some might get added).
  if (request.enable_function_graph()) {
    if (!current_state_.funcgraph_on && !tracefs_->ClearFunctionFilters()) {
      PERFETTO_PLOG("Failed to clear .../set_ftrace_filter");
      return false;
    }
    if (!current_state_.funcgraph_on &&
        !tracefs_->ClearFunctionGraphFilters()) {
      PERFETTO_PLOG("Failed to clear .../set_graph_function");
      return false;
    }
    if (!current_state_.funcgraph_on && !tracefs_->ClearMaxGraphDepth()) {
      PERFETTO_PLOG("Failed to clear .../max_graph_depth");
      return false;
    }
    if (!tracefs_->AppendFunctionFilters(request.function_filters())) {
      PERFETTO_PLOG("Failed to append to .../set_ftrace_filter");
      return false;
    }
    if (!tracefs_->AppendFunctionGraphFilters(request.function_graph_roots())) {
      PERFETTO_PLOG("Failed to append to .../set_graph_function");
      return false;
    }
    if (!tracefs_->SetMaxGraphDepth(request.function_graph_max_depth())) {
      PERFETTO_PLOG("Failed to write to .../max_graph_depth");
      return false;
    }
    if (!current_state_.funcgraph_on &&
        !tracefs_->SetCurrentTracer("function_graph")) {
      PERFETTO_LOG(
          "Unable to enable function_graph tracing since a concurrent ftrace "
          "data source is using a different tracer");
      return false;
    }
    current_state_.funcgraph_on = true;
  }

  const auto& compact_format = table_->compact_sched_format();
  auto compact_sched = CreateCompactSchedConfig(
      request, filter.IsEventEnabled(compact_format.sched_switch.event_id),
      compact_format);
  if (errors && !compact_format.format_valid) {
    errors->failed_ftrace_events.emplace_back(
        "perfetto/compact_sched (unexpected sched event format)");
  }

  std::optional<FtracePrintFilterConfig> ftrace_print_filter;
  if (request.has_print_filter()) {
    ftrace_print_filter =
        FtracePrintFilterConfig::Create(request.print_filter(), table_);
    if (!ftrace_print_filter.has_value()) {
      if (errors) {
        errors->failed_ftrace_events.emplace_back(
            "ftrace/print (unexpected format for filtering)");
      }
    }
  }

  // perfetto v53: self-describing protos are now enabled by default.
  bool denser_generic_event_encoding =
      request.has_denser_generic_event_encoding()
          ? request.denser_generic_event_encoding()
          : true;

  std::vector<std::string> apps(request.atrace_apps());
  std::vector<std::string> categories(request.atrace_categories());
  std::vector<std::string> categories_sdk_optout = Subtract(
      request.atrace_categories(), request.atrace_categories_prefer_sdk());
  ds_configs_.emplace(
      std::piecewise_construct, std::forward_as_tuple(id),
      std::forward_as_tuple(
          std::move(filter), std::move(syscall_filter), compact_sched,
          std::move(ftrace_print_filter), std::move(apps),
          std::move(categories), std::move(categories_sdk_optout),
          request.symbolize_ksyms(), request.drain_buffer_percent(),
          GetSyscallsReturningFds(syscalls_), std::move(kprobes),
          request.debug_ftrace_abi(), denser_generic_event_encoding));
  return true;
}

bool FtraceConfigMuxer::ActivateConfig(FtraceConfigId id) {
  if (!id || ds_configs_.count(id) == 0) {
    PERFETTO_DFATAL("Config not found");
    return false;
  }

  bool first_config = active_configs_.empty();
  active_configs_.insert(id);

  // Pick the lowest buffer_percent across the new set of active configs.
  if (!UpdateBufferPercent()) {
    PERFETTO_ELOG(
        "Invalid FtraceConfig.drain_buffer_percent or "
        "/sys/kernel/tracing/buffer_percent file permissions.");
    // carry on, non-critical error
  }

  // Enable kernel event writer.
  if (first_config) {
    if (!tracefs_->SetTracingOn(true)) {
      PERFETTO_ELOG("Failed to enable ftrace.");
      active_configs_.erase(id);
      return false;
    }
  }
  return true;
}

bool FtraceConfigMuxer::RemoveConfig(FtraceConfigId config_id) {
  if (!config_id || !ds_configs_.erase(config_id))
    return false;
  EventFilter expected_ftrace_events;
  std::vector<std::string> expected_apps;
  std::vector<std::string> expected_categories;
  std::vector<std::string> expected_categories_sdk_optout;
  for (const auto& id_config : ds_configs_) {
    const perfetto::FtraceDataSourceConfig& config = id_config.second;
    expected_ftrace_events.EnableEventsFrom(config.event_filter);
    UnionInPlace(config.atrace_apps, &expected_apps);
    UnionInPlace(config.atrace_categories, &expected_categories);
    UnionInPlace(config.atrace_categories_sdk_optout,
                 &expected_categories_sdk_optout);
  }
  std::vector<std::string> expected_categories_prefer_sdk =
      Subtract(expected_categories, expected_categories_sdk_optout);

  // At this point expected_{apps,categories} contains the union of the
  // leftover configs (if any) that should be still on. However we did not
  // necessarily succeed in turning on atrace for each of those configs
  // previously so we now intersect the {apps,categories} that we *did* manage
  // to turn on with those we want on to determine the new state we should aim
  // for:
  IntersectInPlace(current_state_.atrace_apps, &expected_apps);
  IntersectInPlace(current_state_.atrace_categories, &expected_categories);

  // Work out if there is any difference between the current state and the
  // desired state: It's sufficient to compare sizes here (since we know from
  // above that expected_{apps,categories} is now a subset of
  // atrace_{apps,categories}:
  bool atrace_changed =
      (current_state_.atrace_apps.size() != expected_apps.size()) ||
      (current_state_.atrace_categories.size() != expected_categories.size());

  bool atrace_prefer_sdk_changed =
      current_state_.atrace_categories_prefer_sdk !=
      expected_categories_prefer_sdk;

  if (!SetSyscallEventFilter(/*extra_syscalls=*/{})) {
    PERFETTO_ELOG("Failed to set raw_syscall ftrace filter in RemoveConfig");
  }

  // Disable any events that are currently enabled, but are not in any configs
  // anymore.
  std::set<size_t> event_ids = current_state_.ftrace_events.GetEnabledEvents();
  for (size_t id : event_ids) {
    if (expected_ftrace_events.IsEventEnabled(id))
      continue;
    const Event* event = table_->GetEventById(id);
    // Any event that was enabled must exist.
    PERFETTO_DCHECK(event);
    if (tracefs_->DisableEvent(event->group, event->name))
      current_state_.ftrace_events.DisableEvent(event->ftrace_event_id);
  }

  auto active_it = active_configs_.find(config_id);
  if (active_it != active_configs_.end()) {
    active_configs_.erase(active_it);
    if (active_configs_.empty()) {
      // This was the last active config for now, but potentially more dormant
      // configs need to be activated. We are not interested in reading while no
      // active configs so disable tracing_on here.
      tracefs_->SetTracingOn(false);
    }
  }

  // Update buffer_percent to the minimum of the remaining configs.
  UpdateBufferPercent();

  // Even if we don't have any other active configs, we might still have idle
  // configs around. Tear down the rest of the ftrace config only if all
  // configs are removed.
  if (ds_configs_.empty()) {
    if (tracefs_->SetCpuBufferSizeInPages(1))
      current_state_.cpu_buffer_size_pages = 1;
    tracefs_->SetBufferPercent(50);
    tracefs_->DisableAllEvents();
    tracefs_->ClearTrace();
    tracefs_->SetTracingOn(current_state_.saved_tracing_on);

    // Kprobe cleanup cannot happen while we're still tracing as uninstalling
    // kprobes clears all tracing buffers in the kernel.
    for (const GroupAndName& probe : current_state_.installed_kprobes) {
      tracefs_->RemoveKprobeEvent(probe.group(), probe.name());
      table_->RemoveEvent(probe);
    }

    if (current_state_.exclusive_feature_active) {
      tracefs_->ClearEventTidFilter();
      if (current_state_.saved_tracing_cpumask.has_value()) {
        tracefs_->SetTracingCpuMask(
            current_state_.saved_tracing_cpumask.value());
        current_state_.saved_tracing_cpumask.reset();
      }
      for (auto it = current_state_.saved_tracefs_options.GetIterator(); it;
           ++it) {
        tracefs_->SetTracefsOption(it.key(), it.value());
      }
      current_state_.saved_tracefs_options.Clear();
      current_state_.exclusive_feature_active = false;
    }

    current_state_.installed_kprobes.clear();
  }

  if (current_state_.atrace_on) {
    if (expected_apps.empty() && expected_categories.empty()) {
      DisableAtrace();
    } else if (atrace_changed) {
      // Update atrace to remove the no longer wanted categories/apps. For
      // some categories this won't disable them (e.g. categories that just
      // enable ftrace events) for those there is nothing we can do till the
      // last ftrace config is removed.
      if (StartAtrace(expected_apps, expected_categories,
                      /*atrace_errors=*/nullptr)) {
        // Update current_state_ to reflect this change.
        current_state_.atrace_apps = expected_apps;
        current_state_.atrace_categories = expected_categories;
      }
    }
  }

  if (atrace_prefer_sdk_changed) {
    if (SetAtracePreferSdk(expected_categories_prefer_sdk,
                           /*atrace_errors=*/nullptr)) {
      current_state_.atrace_categories_prefer_sdk =
          expected_categories_prefer_sdk;
    }
  }

  return true;
}

bool FtraceConfigMuxer::ResetCurrentTracer() {
  if (!current_state_.funcgraph_on)
    return true;
  if (!tracefs_->ResetCurrentTracer()) {
    PERFETTO_PLOG("Failed to reset current_tracer to nop");
    return false;
  }
  current_state_.funcgraph_on = false;
  if (!tracefs_->ClearFunctionFilters()) {
    PERFETTO_PLOG("Failed to reset set_ftrace_filter.");
    return false;
  }
  if (!tracefs_->ClearFunctionGraphFilters()) {
    PERFETTO_PLOG("Failed to reset set_function_graph.");
    return false;
  }
  return true;
}

const FtraceDataSourceConfig* FtraceConfigMuxer::GetDataSourceConfig(
    FtraceConfigId id) {
  if (!ds_configs_.count(id))
    return nullptr;
  return &ds_configs_.at(id);
}

void FtraceConfigMuxer::SetupClock(const FtraceConfig& config) {
  std::set<std::string> clocks = tracefs_->AvailableClocks();

  if (config.use_monotonic_raw_clock() && clocks.count(kClockMonoRaw)) {
    tracefs_->SetClock(kClockMonoRaw);
  } else {
    std::string current_clock = tracefs_->GetClock();
    for (size_t i = 0; i < base::ArraySize(kClocks); i++) {
      std::string clock = std::string(kClocks[i]);
      if (!clocks.count(clock))
        continue;
      if (current_clock == clock)
        break;
      tracefs_->SetClock(clock);
      break;
    }
  }

  RememberActiveClock();
}

void FtraceConfigMuxer::RememberActiveClock() {
  std::string current_clock = tracefs_->GetClock();
  namespace pb0 = protos::pbzero;
  if (current_clock == "boot") {
    // "boot" is the default expectation on modern kernels, which is why we
    // don't have an explicit FTRACE_CLOCK_BOOT enum and leave it unset.
    // See comments in ftrace_event_bundle.proto.
    current_state_.ftrace_clock = pb0::FTRACE_CLOCK_UNSPECIFIED;
  } else if (current_clock == "global") {
    current_state_.ftrace_clock = pb0::FTRACE_CLOCK_GLOBAL;
  } else if (current_clock == "local") {
    current_state_.ftrace_clock = pb0::FTRACE_CLOCK_LOCAL;
  } else if (current_clock == kClockMonoRaw) {
    current_state_.ftrace_clock = pb0::FTRACE_CLOCK_MONO_RAW;
  } else {
    current_state_.ftrace_clock = pb0::FTRACE_CLOCK_UNKNOWN;
  }
}

void FtraceConfigMuxer::SetupBufferSize(const FtraceConfig& request) {
  int64_t phys_ram_pages = sysconf(_SC_PHYS_PAGES);
  size_t pages = ComputeCpuBufferSizeInPages(request.buffer_size_kb(),
                                             request.buffer_size_lower_bound(),
                                             phys_ram_pages);
  tracefs_->SetCpuBufferSizeInPages(pages);
  current_state_.cpu_buffer_size_pages = pages;
}

// Post-conditions:
// * result >= 1 (should have at least one page per CPU)
// * If input is 0 output is a good default number
size_t ComputeCpuBufferSizeInPages(size_t requested_buffer_size_kb,
                                   bool buffer_size_lower_bound,
                                   int64_t sysconf_phys_pages) {
  uint32_t page_sz = base::GetSysPageSize();
  uint64_t default_size_kb =
      (sysconf_phys_pages > 0 &&
       (static_cast<uint64_t>(sysconf_phys_pages) >= (kHighMemBytes / page_sz)))
          ? kDefaultHighRamPerCpuBufferSizeKb
          : kDefaultLowRamPerCpuBufferSizeKb;

  size_t actual_size_kb = requested_buffer_size_kb;
  if ((requested_buffer_size_kb == 0) ||
      (buffer_size_lower_bound && default_size_kb > requested_buffer_size_kb)) {
    actual_size_kb = default_size_kb;
  }

  size_t pages = actual_size_kb / (page_sz / 1024);
  return pages ? pages : 1;
}

// TODO(rsavitski): stop caching the "input" value, as the kernel can and will
// choose a slightly different buffer size (especially on 6.x kernels). And even
// then the value might not be exactly page accurate due to scratch pages (more
// of a concern for the |FtraceController::FlushForInstance| caller).
size_t FtraceConfigMuxer::GetPerCpuBufferSizePages() {
  return current_state_.cpu_buffer_size_pages;
}

// If new_cfg_id is set, consider it in addition to already active configs
// as we're trying to activate it.
bool FtraceConfigMuxer::UpdateBufferPercent() {
  uint32_t kUnsetPercent = std::numeric_limits<uint32_t>::max();
  uint32_t min_percent = kUnsetPercent;
  for (auto cfg_id : active_configs_) {
    auto ds_it = ds_configs_.find(cfg_id);
    if (ds_it != ds_configs_.end() && ds_it->second.buffer_percent > 0) {
      min_percent = std::min(min_percent, ds_it->second.buffer_percent);
    }
  }
  if (min_percent == kUnsetPercent)
    return true;
  // Let the kernel ignore values >100.
  return tracefs_->SetBufferPercent(min_percent);
}

void FtraceConfigMuxer::UpdateAtrace(const FtraceConfig& request,
                                     std::string* atrace_errors) {
  // We want to avoid poisoning current_state_.atrace_{categories, apps}
  // if for some reason these args make atrace unhappy so we stash the
  // union into temps and only update current_state_ if we successfully
  // run atrace.

  std::vector<std::string> combined_categories = request.atrace_categories();
  UnionInPlace(current_state_.atrace_categories, &combined_categories);

  std::vector<std::string> combined_apps = request.atrace_apps();
  UnionInPlace(current_state_.atrace_apps, &combined_apps);

  // Each data source can list some atrace categories for which the SDK is
  // preferred (the rest of the categories are considered to opt out of the
  // SDK). When merging multiple data sources, opting out wins. Therefore this
  // code does a union of the opt outs for all data sources.
  std::vector<std::string> combined_categories_sdk_optout = Subtract(
      request.atrace_categories(), request.atrace_categories_prefer_sdk());

  std::vector<std::string> current_categories_sdk_optout =
      Subtract(current_state_.atrace_categories,
               current_state_.atrace_categories_prefer_sdk);
  UnionInPlace(current_categories_sdk_optout, &combined_categories_sdk_optout);

  std::vector<std::string> combined_categories_prefer_sdk =
      Subtract(combined_categories, combined_categories_sdk_optout);

  if (combined_categories_prefer_sdk !=
      current_state_.atrace_categories_prefer_sdk) {
    if (SetAtracePreferSdk(combined_categories_prefer_sdk, atrace_errors)) {
      current_state_.atrace_categories_prefer_sdk =
          combined_categories_prefer_sdk;
    }
  }

  if (!current_state_.atrace_on ||
      combined_apps.size() != current_state_.atrace_apps.size() ||
      combined_categories.size() != current_state_.atrace_categories.size()) {
    if (StartAtrace(combined_apps, combined_categories, atrace_errors)) {
      current_state_.atrace_categories = combined_categories;
      current_state_.atrace_apps = combined_apps;
      current_state_.atrace_on = true;
    }
  }
}

bool FtraceConfigMuxer::StartAtrace(const std::vector<std::string>& apps,
                                    const std::vector<std::string>& categories,
                                    std::string* atrace_errors) {
  PERFETTO_DLOG("Update atrace config...");

  std::vector<std::string> args;
  args.emplace_back("atrace");  // argv0 for exec()
  args.emplace_back("--async_start");
  if (atrace_wrapper_->SupportsUserspaceOnly())
    args.emplace_back("--only_userspace");

  for (const auto& category : categories)
    args.push_back(category);

  if (!apps.empty()) {
    args.emplace_back("-a");
    std::string arg;
    for (const auto& app : apps) {
      arg += app;
      arg += ",";
    }
    arg.resize(arg.size() - 1);
    args.push_back(arg);
  }

  bool result = atrace_wrapper_->RunAtrace(args, atrace_errors);
  PERFETTO_DLOG("...done (%s)", result ? "success" : "fail");
  return result;
}

bool FtraceConfigMuxer::SetAtracePreferSdk(
    const std::vector<std::string>& prefer_sdk_categories,
    std::string* atrace_errors) {
  if (!atrace_wrapper_->SupportsPreferSdk()) {
    return false;
  }
  PERFETTO_DLOG("Update atrace prefer sdk categories...");

  std::vector<std::string> args;
  args.emplace_back("atrace");  // argv0 for exec()
  args.emplace_back("--prefer_sdk");

  for (const auto& category : prefer_sdk_categories)
    args.push_back(category);

  bool result = atrace_wrapper_->RunAtrace(args, atrace_errors);
  PERFETTO_DLOG("...done (%s)", result ? "success" : "fail");
  return result;
}

void FtraceConfigMuxer::DisableAtrace() {
  PERFETTO_DCHECK(current_state_.atrace_on);

  PERFETTO_DLOG("Stop atrace...");

  std::vector<std::string> args{"atrace", "--async_stop"};
  if (atrace_wrapper_->SupportsUserspaceOnly())
    args.emplace_back("--only_userspace");
  if (atrace_wrapper_->RunAtrace(args, /*atrace_errors=*/nullptr)) {
    current_state_.atrace_categories.clear();
    current_state_.atrace_apps.clear();
    current_state_.atrace_on = false;
  }

  PERFETTO_DLOG("...done");
}

}  // namespace perfetto
