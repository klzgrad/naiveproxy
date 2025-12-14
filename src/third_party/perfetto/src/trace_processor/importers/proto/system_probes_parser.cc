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

#include "src/trace_processor/importers/proto/system_probes_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/traced/sys_stats_counters.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/public/compiler.h"
#include "src/kernel_utils/syscall_table.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/system_info_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/importers/syscalls/syscall_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/common/system_info.pbzero.h"
#include "protos/perfetto/trace/ps/process_stats.pbzero.h"
#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/sys_stats/sys_stats.pbzero.h"
#include "protos/perfetto/trace/system_info/cpu_info.pbzero.h"

namespace perfetto::trace_processor {

namespace {

std::optional<int> VersionStringToSdkVersion(const std::string& version) {
  // TODO(lalitm): remove this when the SDK version polling saturates
  // S/T traces in practice.
  if (base::StartsWith(version, "T") || base::StartsWith(version, "S")) {
    return 31;
  }

  // Documentation for this mapping can be found at
  // https://source.android.com/compatibility/cdd.
  if (version == "12") {
    return 31;
  }
  if (version == "11") {
    return 30;
  }
  if (version == "10") {
    return 29;
  }
  if (version == "9") {
    return 28;
  }
  if (version == "8.1") {
    return 27;
  }
  if (version == "8.0") {
    return 26;
  }
  if (version == "7.1") {
    return 25;
  }
  if (version == "7.0") {
    return 24;
  }
  if (version == "6.0") {
    return 23;
  }
  if (version == "5.1" || version == "5.1.1") {
    return 22;
  }
  if (version == "5.0" || version == "5.0.1" || version == "5.0.2") {
    return 21;
  }
  // If we reached this point, we don't know how to parse this version
  // so just return null.
  return std::nullopt;
}

std::optional<int> FingerprintToSdkVersion(const std::string& fingerprint) {
  // Try to parse the SDK version from the fingerprint.
  // Examples of fingerprints:
  // google/shamu/shamu:7.0/NBD92F/3753956:userdebug/dev-keys
  // google/coral/coral:12/SP1A.210812.015/7679548:userdebug/dev-keys
  size_t colon = fingerprint.find(':');
  if (colon == std::string::npos)
    return std::nullopt;

  size_t slash = fingerprint.find('/', colon);
  if (slash == std::string::npos)
    return std::nullopt;

  std::string version = fingerprint.substr(colon + 1, slash - (colon + 1));
  return VersionStringToSdkVersion(version);
}

struct ArmCpuIdentifier {
  uint32_t implementer;
  uint32_t architecture;
  uint32_t variant;
  uint32_t part;
  uint32_t revision;
};

struct CpuInfo {
  uint32_t cpu = 0;
  std::optional<uint32_t> capacity;
  std::vector<uint32_t> frequencies;
  protozero::ConstChars processor;
  // Extend the variant to support additional identifiers
  std::variant<std::nullopt_t, ArmCpuIdentifier> identifier = std::nullopt;
};

struct CpuMaxFrequency {
  uint32_t cpu = 0;
  uint32_t max_frequency = 0;
};

const char* GetPsiResourceKey(size_t resource) {
  using PsiResource = protos::pbzero::SysStats::PsiSample::PsiResource;
  switch (resource) {
    case PsiResource::PSI_RESOURCE_UNSPECIFIED:
      return "resource.unspecified";
    case PsiResource::PSI_RESOURCE_CPU_SOME:
      return "cpu.some";
    case PsiResource::PSI_RESOURCE_CPU_FULL:
      return "cpu.full";
    case PsiResource::PSI_RESOURCE_IO_SOME:
      return "io.some";
    case PsiResource::PSI_RESOURCE_IO_FULL:
      return "io.full";
    case PsiResource::PSI_RESOURCE_MEMORY_SOME:
      return "mem.some";
    case PsiResource::PSI_RESOURCE_MEMORY_FULL:
      return "mem.full";
    default:
      return nullptr;
  }
}

const char* GetProcessMemoryKey(uint32_t field_id) {
  using ProcessStats = protos::pbzero::ProcessStats;
  switch (field_id) {
    case ProcessStats::Process::kVmSizeKbFieldNumber:
      return "virt";
    case ProcessStats::Process::kVmRssKbFieldNumber:
      return "rss";
    case ProcessStats::Process::kRssAnonKbFieldNumber:
      return "rss.anon";
    case ProcessStats::Process::kRssFileKbFieldNumber:
      return "rss.file";
    case ProcessStats::Process::kRssShmemKbFieldNumber:
      return "rss.shmem";
    case ProcessStats::Process::kVmSwapKbFieldNumber:
      return "swap";
    case ProcessStats::Process::kVmLockedKbFieldNumber:
      return "locked";
    case ProcessStats::Process::kVmHwmKbFieldNumber:
      return "rss.watermark";
    case ProcessStats::Process::kDmabufRssKbFieldNumber:
      return "dmabuf_rss";
    default:
      return nullptr;
  }
}

const char* GetSmapsKey(uint32_t field_id) {
  using ProcessStats = protos::pbzero::ProcessStats;
  switch (field_id) {
    case ProcessStats::Process::kSmrRssKbFieldNumber:
      return "rss";
    case ProcessStats::Process::kSmrPssKbFieldNumber:
      return "pss";
    case ProcessStats::Process::kSmrPssAnonKbFieldNumber:
      return "pss.anon";
    case ProcessStats::Process::kSmrPssFileKbFieldNumber:
      return "pss.file";
    case ProcessStats::Process::kSmrPssShmemKbFieldNumber:
      return "pss.smem";
    case ProcessStats::Process::kSmrSwapPssKbFieldNumber:
      return "swap.pss";
    default:
      return nullptr;
  }
}

}  // namespace

SystemProbesParser::SystemProbesParser(TraceProcessorContext* context)
    : context_(context),
      utid_name_id_(context->storage->InternString("utid")),
      is_kthread_id_(context->storage->InternString("is_kthread")),
      arm_cpu_implementer(
          context->storage->InternString("arm_cpu_implementer")),
      arm_cpu_architecture(
          context->storage->InternString("arm_cpu_architecture")),
      arm_cpu_variant(context->storage->InternString("arm_cpu_variant")),
      arm_cpu_part(context->storage->InternString("arm_cpu_part")),
      arm_cpu_revision(context->storage->InternString("arm_cpu_revision")),
      meminfo_strs_(BuildMeminfoCounterNames()),
      vmstat_strs_(BuildVmstatCounterNames()) {}

void SystemProbesParser::ParseDiskStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::SysStats::DiskStat::Decoder ds(blob);

  // /proc/diskstats always uses 512 byte sector sizes.
  static constexpr double SECTORS_PER_MB = 2048.0;
  static constexpr double MS_PER_SEC = 1000.0;

  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "diskstat", tracks::DynamicUnitBlueprint(),
      tracks::DimensionBlueprints(
          tracks::StringDimensionBlueprint("device_name"),
          tracks::StringDimensionBlueprint("counter_name")),
      tracks::FnNameBlueprint([](base::StringView device_name,
                                 base::StringView counter_name) {
        return base::StackString<1024>(
            "diskstat.[%.*s].%.*s", int(device_name.size()), device_name.data(),
            int(counter_name.size()), counter_name.data());
      }));

  auto push_counter = [&, this](base::StringView counter_name,
                                base::StringView unit, double value) {
    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint,
        tracks::Dimensions(base::StringView(ds.device_name()),
                           base::StringView(counter_name)),
        tracks::BlueprintName(), {},
        tracks::DynamicUnit(context_->storage->InternString(unit)));
    context_->event_tracker->PushCounter(ts, value, track);
  };

  auto cur_read_amount = static_cast<int64_t>(ds.read_sectors());
  auto cur_write_amount = static_cast<int64_t>(ds.write_sectors());
  auto cur_discard_amount = static_cast<int64_t>(ds.discard_sectors());
  auto cur_flush_count = static_cast<int64_t>(ds.flush_count());
  auto cur_read_time = static_cast<int64_t>(ds.read_time_ms());
  auto cur_write_time = static_cast<int64_t>(ds.write_time_ms());
  auto cur_discard_time = static_cast<int64_t>(ds.discard_time_ms());
  auto cur_flush_time = static_cast<int64_t>(ds.flush_time_ms());

  StringId device_name_id = context_->storage->InternString(ds.device_name());
  DiskStatState& state = disk_state_map_[device_name_id];
  if (state.prev_read_amount != -1) {
    double read_amount =
        static_cast<double>(cur_read_amount - state.prev_read_amount) /
        SECTORS_PER_MB;
    double write_amount =
        static_cast<double>(cur_write_amount - state.prev_write_amount) /
        SECTORS_PER_MB;
    double discard_amount =
        static_cast<double>(cur_discard_amount - state.prev_discard_amount) /
        SECTORS_PER_MB;
    auto flush_count =
        static_cast<double>(cur_flush_count - state.prev_flush_count);
    int64_t read_time_diff = cur_read_time - state.prev_read_time;
    int64_t write_time_diff = cur_write_time - state.prev_write_time;
    int64_t discard_time_diff = cur_discard_time - state.prev_discard_time;
    auto flush_time_diff =
        static_cast<double>(cur_flush_time - state.prev_flush_time);

    auto calculate_throughput = [](double amount, int64_t diff) {
      return diff == 0 ? 0 : amount * MS_PER_SEC / static_cast<double>(diff);
    };
    double read_thpt = calculate_throughput(read_amount, read_time_diff);
    double write_thpt = calculate_throughput(write_amount, write_time_diff);
    double discard_thpt =
        calculate_throughput(discard_amount, discard_time_diff);

    push_counter("read_amount", "MB", read_amount);
    push_counter("read_throughput", "MB/s", read_thpt);
    push_counter("write_amount", "MB", write_amount);
    push_counter("write_throughput", "MB/s", write_thpt);
    push_counter("discard_amount", "MB", discard_amount);
    push_counter("discard_throughput", "MB/s", discard_thpt);
    push_counter("flush_amount", "count", flush_count);
    push_counter("flush_time", "ms", flush_time_diff);
  }
  state.prev_read_amount = cur_read_amount;
  state.prev_write_amount = cur_write_amount;
  state.prev_discard_amount = cur_discard_amount;
  state.prev_flush_count = cur_flush_count;
  state.prev_read_time = cur_read_time;
  state.prev_write_time = cur_write_time;
  state.prev_discard_time = cur_discard_time;
  state.prev_flush_time = cur_flush_time;
}

void SystemProbesParser::ParseSysStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::SysStats::Decoder sys_stats(blob);

  static constexpr auto kMeminfoBlueprint = tracks::CounterBlueprint(
      "meminfo", tracks::kBytesUnitBlueprint,
      tracks::DimensionBlueprints(
          tracks::StringDimensionBlueprint("meminfo_key")),
      tracks::FnNameBlueprint([](base::StringView name) {
        return base::StackString<1024>("%.*s", int(name.size()), name.data());
      }));
  for (auto it = sys_stats.meminfo(); it; ++it) {
    protos::pbzero::SysStats::MeminfoValue::Decoder mi(*it);
    auto key = static_cast<size_t>(mi.key());
    if (PERFETTO_UNLIKELY(key >= meminfo_strs_.size())) {
      PERFETTO_ELOG("MemInfo key %zu is not recognized.", key);
      context_->storage->IncrementStats(stats::meminfo_unknown_keys);
      continue;
    }
    // /proc/meminfo counters are in kB, convert to bytes
    TrackId track = context_->track_tracker->InternTrack(
        kMeminfoBlueprint, tracks::Dimensions(meminfo_strs_[key]),
        tracks::BlueprintName());
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(mi.value()) * 1024, track);
  }

  for (auto it = sys_stats.devfreq(); it; ++it) {
    protos::pbzero::SysStats::DevfreqValue::Decoder vm(*it);
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kClockFrequencyBlueprint, tracks::Dimensions(vm.key()));
    context_->event_tracker->PushCounter(ts, static_cast<double>(vm.value()),
                                         track);
  }

  uint32_t c = 0;
  for (auto it = sys_stats.cpufreq_khz(); it; ++it, ++c) {
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kCpuFrequencyBlueprint, tracks::Dimensions(c));
    context_->event_tracker->PushCounter(ts, static_cast<double>(*it), track);
  }

  static constexpr auto kVmStatBlueprint = tracks::CounterBlueprint(
      "vmstat", tracks::UnknownUnitBlueprint(),
      tracks::DimensionBlueprints(
          tracks::StringDimensionBlueprint("vmstat_key")),
      tracks::FnNameBlueprint([](base::StringView name) {
        return base::StackString<1024>("%.*s", int(name.size()), name.data());
      }));
  for (auto it = sys_stats.vmstat(); it; ++it) {
    protos::pbzero::SysStats::VmstatValue::Decoder vm(*it);
    auto key = static_cast<size_t>(vm.key());
    if (PERFETTO_UNLIKELY(key >= vmstat_strs_.size())) {
      PERFETTO_ELOG("VmStat key %zu is not recognized.", key);
      context_->storage->IncrementStats(stats::vmstat_unknown_keys);
      continue;
    }
    TrackId track = context_->track_tracker->InternTrack(
        kVmStatBlueprint, tracks::Dimensions(vmstat_strs_[key]));
    context_->event_tracker->PushCounter(ts, static_cast<double>(vm.value()),
                                         track);
  }

  for (auto it = sys_stats.cpu_stat(); it; ++it) {
    protos::pbzero::SysStats::CpuTimes::Decoder ct(*it);
    if (PERFETTO_UNLIKELY(!ct.has_cpu_id())) {
      PERFETTO_ELOG("CPU field not found in CpuTimes");
      context_->storage->IncrementStats(stats::invalid_cpu_times);
      continue;
    }

    static constexpr auto kCpuStatBlueprint = tracks::CounterBlueprint(
        "cpustat", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::kCpuDimensionBlueprint,
            tracks::StringDimensionBlueprint("cpustat_key")),
        tracks::FnNameBlueprint([](uint32_t, base::StringView key) {
          return base::StackString<1024>("cpu.times.%.*s", int(key.size()),
                                         key.data());
        }));
    auto intern_track = [&](const char* name) {
      return context_->track_tracker->InternTrack(
          kCpuStatBlueprint, tracks::Dimensions(ct.cpu_id(), name));
    };
    context_->event_tracker->PushCounter(ts, static_cast<double>(ct.user_ns()),
                                         intern_track("user_ns"));
    context_->event_tracker->PushCounter(ts,
                                         static_cast<double>(ct.user_nice_ns()),
                                         intern_track("user_nice_ns"));
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(ct.system_mode_ns()),
        intern_track("system_mode_ns"));
    context_->event_tracker->PushCounter(ts, static_cast<double>(ct.idle_ns()),
                                         intern_track("idle_ns"));
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(ct.io_wait_ns()), intern_track("io_wait_ns"));
    context_->event_tracker->PushCounter(ts, static_cast<double>(ct.irq_ns()),
                                         intern_track("irq_ns"));
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(ct.softirq_ns()), intern_track("softirq_ns"));
    context_->event_tracker->PushCounter(ts, static_cast<double>(ct.steal_ns()),
                                         intern_track("steal_ns"));
  }

  for (auto it = sys_stats.num_irq(); it; ++it) {
    static constexpr auto kTrackBlueprint = tracks::CounterBlueprint(
        "num_irq", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(tracks::kIrqDimensionBlueprint),
        tracks::FnNameBlueprint([](uint32_t irq) {
          return base::StackString<1024>("num_irq (id: %u)", irq);
        }));
    protos::pbzero::SysStats::InterruptCount::Decoder ic(*it);
    TrackId track = context_->track_tracker->InternTrack(
        kTrackBlueprint, tracks::Dimensions(ic.irq()));
    context_->event_tracker->PushCounter(ts, static_cast<double>(ic.count()),
                                         track);
  }

  for (auto it = sys_stats.num_softirq(); it; ++it) {
    static constexpr auto kTrackBlueprint = tracks::CounterBlueprint(
        "num_softirq", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(tracks::kIrqDimensionBlueprint),
        tracks::FnNameBlueprint([](uint32_t irq) {
          return base::StackString<1024>("num_softirq (id: %u)", irq);
        }));
    protos::pbzero::SysStats::InterruptCount::Decoder ic(*it);
    TrackId track = context_->track_tracker->InternTrack(
        kTrackBlueprint, tracks::Dimensions(ic.irq()));
    context_->event_tracker->PushCounter(ts, static_cast<double>(ic.count()),
                                         track);
  }

  if (sys_stats.has_num_forks()) {
    static constexpr auto kBlueprint =
        tracks::CounterBlueprint("num_forks", tracks::UnknownUnitBlueprint(),
                                 tracks::DimensionBlueprints(),
                                 tracks::StaticNameBlueprint("num_forks"));
    TrackId track = context_->track_tracker->InternTrack(kBlueprint);
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(sys_stats.num_forks()), track);
  }

  if (sys_stats.has_num_irq_total()) {
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "num_irq_total", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(),
        tracks::StaticNameBlueprint("num_irq_total"));
    TrackId track = context_->track_tracker->InternTrack(kBlueprint);
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(sys_stats.num_irq_total()), track);
  }

  if (sys_stats.has_num_softirq_total()) {
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "num_softirq_total", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(),
        tracks::StaticNameBlueprint("num_softirq_total"));
    TrackId track = context_->track_tracker->InternTrack(kBlueprint);
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(sys_stats.num_softirq_total()), track);
  }

  // Fragmentation of the kernel binary buddy memory allocator.
  // See /proc/buddyinfo in `man 5 proc`.
  for (auto it = sys_stats.buddy_info(); it; ++it) {
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "buddyinfo", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::StringDimensionBlueprint("buddyinfo_node"),
            tracks::StringDimensionBlueprint("buddyinfo_zone"),
            tracks::UintDimensionBlueprint("buddyinfo_chunk_size_kb")),
        tracks::FnNameBlueprint([](base::StringView node, base::StringView zone,
                                   uint32_t chunk_size_kb) {
          return base::StackString<1024>(
              "mem.buddyinfo[%.*s][%.*s][%u kB]", int(node.size()), node.data(),
              int(zone.size()), zone.data(), chunk_size_kb);
        }));
    protos::pbzero::SysStats::BuddyInfo::Decoder bi(*it);
    int order = 0;
    for (auto order_it = bi.order_pages(); order_it; ++order_it) {
      auto chunk_size_kb =
          static_cast<uint32_t>(((1 << order) * page_size_) / 1024);
      TrackId track = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(bi.node(), bi.zone(), chunk_size_kb));
      context_->event_tracker->PushCounter(ts, static_cast<double>(*order_it),
                                           track);
      order++;
    }
  }

  for (auto it = sys_stats.disk_stat(); it; ++it) {
    ParseDiskStats(ts, *it);
  }

  // Pressure Stall Information. See
  // https://docs.kernel.org/accounting/psi.html.
  for (auto it = sys_stats.psi(); it; ++it) {
    protos::pbzero::SysStats::PsiSample::Decoder psi(*it);

    auto resource = static_cast<size_t>(psi.resource());
    const char* resource_key = GetPsiResourceKey(resource);
    if (!resource_key) {
      context_->storage->IncrementStats(stats::psi_unknown_resource);
      return;
    }
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "psi", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::StringDimensionBlueprint("psi_resource")),
        tracks::FnNameBlueprint([](base::StringView resource) {
          return base::StackString<1024>("psi.%.*s", int(resource.size()),
                                         resource.data());
        }));
    // Unit = total blocked time on this resource in nanoseconds.
    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint, tracks::Dimensions(resource_key));
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(psi.total_ns()), track);
  }

  for (auto it = sys_stats.thermal_zone(); it; ++it) {
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "thermal_temperature_sys", tracks::StaticUnitBlueprint("C"),
        tracks::DimensionBlueprints(tracks::kThermalZoneDimensionBlueprint),
        tracks::FnNameBlueprint([](base::StringView thermal_zone) {
          return base::StackString<1024>("%.*s", int(thermal_zone.size()),
                                         thermal_zone.data());
        }));
    protos::pbzero::SysStats::ThermalZone::Decoder thermal(*it);
    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint, tracks::Dimensions(thermal.type()));
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(thermal.temp()), track);
  }

  for (auto it = sys_stats.cpuidle_state(); it; ++it) {
    ParseCpuIdleStats(ts, *it);
  }

  for (auto it = sys_stats.gpufreq_mhz(); it; ++it, ++c) {
    TrackId track = context_->track_tracker->InternTrack(
        tracks::kGpuFrequencyBlueprint, tracks::Dimensions(0));
    context_->event_tracker->PushCounter(ts, static_cast<double>(*it), track);
  }
}

void SystemProbesParser::ParseCpuIdleStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::SysStats::CpuIdleState::Decoder cpuidle_state(blob);
  uint32_t cpu = cpuidle_state.cpu_id();
  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "cpu_idle_state", tracks::StaticUnitBlueprint("us"),
      tracks::DimensionBlueprints(tracks::kCpuDimensionBlueprint,
                                  tracks::StringDimensionBlueprint("state")),
      tracks::FnNameBlueprint([](uint32_t cpu, base::StringView state) {
        return base::StackString<1024>("cpuidle%u.%.*s", cpu, int(state.size()),
                                       state.data());
      }));

  for (auto f = cpuidle_state.cpuidle_state_entry(); f; ++f) {
    protos::pbzero::SysStats::CpuIdleStateEntry::Decoder idle(*f);
    std::string state_name = idle.state().ToStdString();

    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint, tracks::Dimensions(cpu, state_name.c_str()),
        tracks::BlueprintName());

    context_->event_tracker->PushCounter(
        ts, static_cast<double>(idle.duration_us()), track);
  }
}

void SystemProbesParser::ParseProcessTree(int64_t ts, ConstBytes blob) {
  protos::pbzero::ProcessTree::Decoder ps(blob);

  for (auto it = ps.processes(); it; ++it) {
    protos::pbzero::ProcessTree::Process::Decoder proc(*it);
    if (!proc.has_cmdline())
      continue;
    auto pid = static_cast<uint32_t>(proc.pid());
    auto ppid = static_cast<uint32_t>(proc.ppid());

    if (proc.has_nspid()) {
      std::vector<int64_t> nspid;
      for (auto nspid_it = proc.nspid(); nspid_it; nspid_it++) {
        nspid.emplace_back(static_cast<int64_t>(*nspid_it));
      }
      context_->process_tracker->UpdateNamespacedProcess(pid, std::move(nspid));
    }

    protozero::RepeatedFieldIterator<protozero::ConstChars> raw_cmdline =
        proc.cmdline();
    base::StringView argv0 = raw_cmdline ? *raw_cmdline : base::StringView();
    base::StringView joined_cmdline{};

    // Special case: workqueue kernel threads (kworker). Worker threads are
    // organised in pools, which can process work from different workqueues.
    // When we read their thread name via procfs, the kernel takes a dedicated
    // codepath that appends the name of the current/last workqueue that the
    // worker processed. This is highly transient and therefore misleading to
    // users if we keep using this name for the kernel thread.
    // Example:
    //   kworker/45:2-mm_percpu_wq
    //   ^           ^
    //   [worker id ][last queue ]
    //
    // Instead, use a truncated version of the process name that identifies just
    // the worker itself. For the above example, this would be "kworker/45:2".
    //
    // https://github.com/torvalds/linux/blob/6d280f4d760e3bcb4a8df302afebf085b65ec982/kernel/workqueue.c#L5336
    uint32_t kThreaddPid = 2;
    if (ppid == kThreaddPid && argv0.StartsWith("kworker/")) {
      size_t delim_loc = std::min(argv0.find('+', 8), argv0.find('-', 8));
      if (delim_loc != base::StringView::npos) {
        argv0 = argv0.substr(0, delim_loc);
        joined_cmdline = argv0;
      }
    }

    // Special case: some processes rewrite their cmdline with spaces as a
    // separator instead of a NUL byte. Assume that's the case if there's only a
    // single cmdline element. This will be wrong for binaries that have spaces
    // in their path and are invoked without additional arguments, but those are
    // very rare. The full cmdline will still be correct either way.
    if (!static_cast<bool>(++proc.cmdline())) {
      size_t delim_pos = argv0.find(' ');
      if (delim_pos != base::StringView::npos) {
        argv0 = argv0.substr(0, delim_pos);
      }
    }

    std::string cmdline_str;
    if (joined_cmdline.empty()) {
      for (auto cmdline_it = raw_cmdline; cmdline_it;) {
        auto cmdline_part = *cmdline_it;
        cmdline_str.append(cmdline_part.data, cmdline_part.size);

        if (++cmdline_it)
          cmdline_str.append(" ");
      }
      joined_cmdline = base::StringView(cmdline_str);
    }

    UniquePid pupid = context_->process_tracker->GetOrCreateProcess(ppid);
    UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);

    upid = context_->process_tracker->UpdateProcessWithParent(
        upid, pupid, /*associate_main_thread=*/true);

    context_->process_tracker->SetProcessMetadata(upid, argv0, joined_cmdline);

    // perfetto v50+: additionally, if we know that the "cmdline" contents are
    // coming from the main thread's name ("comm"), then set the thread name as
    // well. This comes up with kernel threads, which are in fact single-thread
    // processes without a /proc/pid/cmdline. The reuse of "cmdline" for this
    // scenario is historical, but we maintain compatibility. Note:
    // cmdline_is_comm is not equivalent to "is a kernel thread", as the field
    // could also be set for e.g. zombie processes.
    if (proc.cmdline_is_comm()) {
      auto utid = context_->process_tracker->GetOrCreateThread(pid);
      auto thread_name_id = context_->storage->InternString(joined_cmdline);
      context_->process_tracker->UpdateThreadName(
          utid, thread_name_id, ThreadNamePriority::kProcessTree);
    }

    if (proc.has_uid()) {
      context_->process_tracker->SetProcessUid(
          upid, static_cast<uint32_t>(proc.uid()));
    }

    // note: early kernel threads can have an age of zero (at tick resolution)
    if (proc.has_process_start_from_boot()) {
      base::StatusOr<int64_t> start_ts = context_->clock_tracker->ToTraceTime(
          protos::pbzero::BUILTIN_CLOCK_BOOTTIME,
          static_cast<int64_t>(proc.process_start_from_boot()));
      if (start_ts.ok()) {
        context_->process_tracker->SetStartTsIfUnset(upid, *start_ts);
      }
    }

    // Linux v6.4+: explicit field for whether this is a kernel thread.
    if (proc.has_is_kthread()) {
      context_->process_tracker->AddArgsToProcess(upid).AddArg(
          is_kthread_id_, Variadic::Boolean(proc.is_kthread()));
    }
  }

  for (auto it = ps.threads(); it; ++it) {
    protos::pbzero::ProcessTree::Thread::Decoder thd(*it);
    auto tid = static_cast<uint32_t>(thd.tid());
    auto tgid = static_cast<uint32_t>(thd.tgid());
    context_->process_tracker->UpdateThread(tid, tgid);

    if (thd.has_name()) {
      StringId thread_name_id = context_->storage->InternString(thd.name());
      auto utid = context_->process_tracker->GetOrCreateThread(tid);
      context_->process_tracker->UpdateThreadName(
          utid, thread_name_id, ThreadNamePriority::kProcessTree);
    }

    if (thd.has_nstid()) {
      std::vector<int64_t> nstid;
      for (auto nstid_it = thd.nstid(); nstid_it; nstid_it++) {
        nstid.emplace_back(static_cast<int64_t>(*nstid_it));
      }
      if (!context_->process_tracker->UpdateNamespacedThread(
              tgid, tid, std::move(nstid))) {
        context_->import_logs_tracker->RecordParserError(
            stats::namespaced_thread_missing_process, ts);
      }
    }
  }
}

void SystemProbesParser::ParseProcessStats(int64_t ts, ConstBytes blob) {
  using Process = protos::pbzero::ProcessStats::Process;
  protos::pbzero::ProcessStats::Decoder stats(blob);
  for (auto it = stats.processes(); it; ++it) {
    protozero::ProtoDecoder proc(*it);
    uint32_t pid = proc.FindField(Process::kPidFieldNumber).as_uint32();
    for (auto fld = proc.ReadField(); fld.valid(); fld = proc.ReadField()) {
      if (fld.id() == Process::kPidFieldNumber) {
        continue;
      }
      if (fld.id() == Process::kThreadsFieldNumber) {
        ParseThreadStats(ts, pid, fld.as_bytes());
        continue;
      }
      if (fld.id() == Process::kFdsFieldNumber) {
        ParseProcessFds(ts, pid, fld.as_bytes());
        continue;
      }
      // Chrome fields are processed by ChromeSystemProbesParser.
      if (fld.id() == Process::kIsPeakRssResettableFieldNumber ||
          fld.id() == Process::kChromePrivateFootprintKbFieldNumber ||
          fld.id() == Process::kChromePrivateFootprintKbFieldNumber) {
        continue;
      }

      UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);
      if (fld.id() == Process::kOomScoreAdjFieldNumber) {
        TrackId track = context_->track_tracker->InternTrack(
            tracks::kOomScoreAdjBlueprint, tracks::DimensionBlueprints(upid));
        context_->event_tracker->PushCounter(
            ts, static_cast<double>(fld.as_int64()), track);
        continue;
      }
      {
        const char* process_memory_key = GetProcessMemoryKey(fld.id());
        if (process_memory_key) {
          // Memory counters are in KB, keep values in bytes in the trace
          // processor.
          int64_t value = fld.as_int64() * 1024;
          TrackId track = context_->track_tracker->InternTrack(
              tracks::kProcessMemoryBlueprint,
              tracks::DimensionBlueprints(upid, process_memory_key));
          context_->event_tracker->PushCounter(ts, static_cast<double>(value),
                                               track);
          continue;
        }
      }
      {
        const char* smaps = GetSmapsKey(fld.id());
        if (smaps) {
          static constexpr auto kBlueprint = tracks::CounterBlueprint(
              "smaps", tracks::UnknownUnitBlueprint(),
              tracks::DimensionBlueprints(
                  tracks::kProcessDimensionBlueprint,
                  tracks::StringDimensionBlueprint("smaps_key")),
              tracks::FnNameBlueprint([](UniquePid, base::StringView key) {
                return base::StackString<1024>("mem.smaps.%.*s",
                                               int(key.size()), key.data());
              }));

          // Memory counters are in KB, keep values in bytes in the trace
          // processor.
          int64_t value = fld.as_int64() * 1024;
          TrackId track = context_->track_tracker->InternTrack(
              kBlueprint, tracks::DimensionBlueprints(upid, smaps));
          context_->event_tracker->PushCounter(ts, static_cast<double>(value),
                                               track);
          continue;
        }
      }
      if (fld.id() == Process::kRuntimeUserModeFieldNumber ||
          fld.id() == Process::kRuntimeKernelModeFieldNumber) {
        static constexpr auto kBlueprint = tracks::CounterBlueprint(
            "proc_stat_runtime", tracks::UnknownUnitBlueprint(),
            tracks::DimensionBlueprints(
                tracks::kProcessDimensionBlueprint,
                tracks::StringDimensionBlueprint("proc_stat_runtime_key")),
            tracks::FnNameBlueprint([](UniquePid, base::StringView key) {
              return base::StackString<1024>("runtime.%.*s", int(key.size()),
                                             key.data());
            }));
        const char* key = fld.id() == Process::kRuntimeUserModeFieldNumber
                              ? "user_ns"
                              : "kernel_ns";
        TrackId track = context_->track_tracker->InternTrack(
            kBlueprint, tracks::DimensionBlueprints(upid, key));
        context_->event_tracker->PushCounter(
            ts, static_cast<double>(fld.as_int64()), track);
        continue;
      }

      // No handling for this field, so increment the error counter.
      context_->storage->IncrementStats(stats::proc_stat_unknown_counters);
    }
  }
}

void SystemProbesParser::ParseThreadStats(int64_t,
                                          uint32_t pid,
                                          ConstBytes blob) {
  protos::pbzero::ProcessStats::Thread::Decoder stats(blob);
  context_->process_tracker->UpdateThread(static_cast<uint32_t>(stats.tid()),
                                          pid);
}

void SystemProbesParser::ParseProcessFds(int64_t ts,
                                         uint32_t pid,
                                         ConstBytes blob) {
  protos::pbzero::ProcessStats::FDInfo::Decoder fd_info(blob);

  tables::FiledescriptorTable::Row row;
  row.fd = static_cast<int64_t>(fd_info.fd());
  row.ts = ts;
  row.path = context_->storage->InternString(fd_info.path());
  row.upid = context_->process_tracker->GetOrCreateProcess(pid);

  auto* fd_table = context_->storage->mutable_filedescriptor_table();
  fd_table->Insert(row);
}

void SystemProbesParser::ParseSystemInfo(ConstBytes blob) {
  protos::pbzero::SystemInfo::Decoder packet(blob);
  MachineTracker* machine_tracker = context_->machine_tracker.get();
  SystemInfoTracker* system_info_tracker =
      SystemInfoTracker::GetOrCreate(context_);
  if (packet.has_utsname()) {
    ConstBytes utsname_blob = packet.utsname();
    protos::pbzero::Utsname::Decoder utsname(utsname_blob);
    base::StringView machine = utsname.machine();
    SyscallTracker* syscall_tracker = SyscallTracker::GetOrCreate(context_);
    Architecture arch = SyscallTable::ArchFromString(machine);
    if (arch != Architecture::kUnknown) {
      syscall_tracker->SetArchitecture(arch);
    } else {
      PERFETTO_ELOG("Unknown architecture %s. Syscall traces will not work.",
                    machine.ToStdString().c_str());
    }

    system_info_tracker->SetKernelVersion(utsname.sysname(), utsname.release());

    StringPool::Id sysname_id =
        context_->storage->InternString(utsname.sysname());
    StringPool::Id version_id =
        context_->storage->InternString(utsname.version());
    StringPool::Id release_id =
        context_->storage->InternString(utsname.release());
    StringPool::Id machine_id =
        context_->storage->InternString(utsname.machine());

    machine_tracker->SetMachineInfo(sysname_id, release_id, version_id,
                                    machine_id);

    MetadataTracker* metadata = context_->metadata_tracker.get();
    metadata->SetMetadata(metadata::system_name, Variadic::String(sysname_id));
    metadata->SetMetadata(metadata::system_version,
                          Variadic::String(version_id));
    metadata->SetMetadata(metadata::system_release,
                          Variadic::String(release_id));
    metadata->SetMetadata(metadata::system_machine,
                          Variadic::String(machine_id));
  }

  if (packet.has_timezone_off_mins()) {
    static constexpr int64_t kNanosInMinute =
        60ull * 1000ull * 1000ull * 1000ull;
    context_->metadata_tracker->SetMetadata(
        metadata::timezone_off_mins,
        Variadic::Integer(packet.timezone_off_mins()));
    context_->clock_tracker->set_timezone_offset(packet.timezone_off_mins() *
                                                 kNanosInMinute);
  }

  if (packet.has_android_build_fingerprint()) {
    auto android_build_fingerprint =
        context_->storage->InternString(packet.android_build_fingerprint());
    context_->metadata_tracker->SetMetadata(
        metadata::android_build_fingerprint,
        Variadic::String(android_build_fingerprint));
    machine_tracker->SetAndroidBuildFingerprint(android_build_fingerprint);
  }

  if (packet.has_android_device_manufacturer()) {
    auto android_device_manufacturer =
        context_->storage->InternString(packet.android_device_manufacturer());
    context_->metadata_tracker->SetMetadata(
        metadata::android_device_manufacturer,
        Variadic::String(android_device_manufacturer));
    machine_tracker->SetAndroidDeviceManufacturer(android_device_manufacturer);
  }

  // If we have the SDK version in the trace directly just use that.
  // Otherwise, try and parse it from the fingerprint.
  std::optional<int64_t> opt_sdk_version;
  if (packet.has_android_sdk_version()) {
    opt_sdk_version = static_cast<int64_t>(packet.android_sdk_version());
  } else if (packet.has_android_build_fingerprint()) {
    opt_sdk_version = FingerprintToSdkVersion(
        packet.android_build_fingerprint().ToStdString());
  }

  if (opt_sdk_version) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_sdk_version, Variadic::Integer(*opt_sdk_version));
    machine_tracker->SetAndroidSdkVersion(*opt_sdk_version);
  }

  if (packet.has_android_soc_model()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_soc_model,
        Variadic::String(
            context_->storage->InternString(packet.android_soc_model())));
  }

  if (packet.has_android_guest_soc_model()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_guest_soc_model,
        Variadic::String(
            context_->storage->InternString(packet.android_guest_soc_model())));
  }

  if (packet.has_android_hardware_revision()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_hardware_revision,
        Variadic::String(context_->storage->InternString(
            packet.android_hardware_revision())));
  }

  if (packet.has_android_storage_model()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_storage_model,
        Variadic::String(
            context_->storage->InternString(packet.android_storage_model())));
  }

  if (packet.has_android_ram_model()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_ram_model,
        Variadic::String(
            context_->storage->InternString(packet.android_ram_model())));
  }

  if (packet.has_android_serial_console()) {
    context_->metadata_tracker->SetMetadata(
        metadata::android_serial_console,
        Variadic::String(
            context_->storage->InternString(packet.android_serial_console())));
  }

  page_size_ = packet.page_size();
  if (!page_size_) {
    page_size_ = 4096;
  }

  if (packet.has_num_cpus()) {
    machine_tracker->SetNumCpus(packet.num_cpus());
    system_info_tracker->SetNumCpus(packet.num_cpus());
  }
}

void SystemProbesParser::ParseCpuInfo(ConstBytes blob) {
  protos::pbzero::CpuInfo::Decoder packet(blob);
  std::vector<CpuInfo> cpu_infos;

  // Decode CpuInfo packet
  uint32_t cpu_id = 0;
  for (auto it = packet.cpus(); it; it++, cpu_id++) {
    protos::pbzero::CpuInfo::Cpu::Decoder cpu(*it);

    CpuInfo current_cpu_info;
    current_cpu_info.cpu = cpu_id;
    current_cpu_info.processor = cpu.processor();

    for (auto freq_it = cpu.frequencies(); freq_it; freq_it++) {
      uint32_t current_cpu_frequency = *freq_it;
      current_cpu_info.frequencies.push_back(current_cpu_frequency);
    }
    if (cpu.has_capacity()) {
      current_cpu_info.capacity = cpu.capacity();
    }

    if (cpu.has_arm_identifier()) {
      protos::pbzero::CpuInfo::ArmCpuIdentifier::Decoder identifier(
          cpu.arm_identifier());

      current_cpu_info.identifier = ArmCpuIdentifier{
          identifier.implementer(), identifier.architecture(),
          identifier.variant(),     identifier.part(),
          identifier.revision(),
      };
    }

    cpu_infos.push_back(current_cpu_info);
  }

  // Calculate cluster ids
  // We look to use capacities as it is an ARM provided metric which is designed
  // to measure the heterogeneity of CPU clusters however we fallback on the
  // maximum frequency as an estimate

  // Capacities are defined as existing on all CPUs if present and so we set
  // them as invalid if any is missing
  bool valid_capacities = std::all_of(
      cpu_infos.begin(), cpu_infos.end(),
      [](const CpuInfo& info) { return info.capacity.has_value(); });

  bool valid_frequencies = std::all_of(
      cpu_infos.begin(), cpu_infos.end(),
      [](const CpuInfo& info) { return !info.frequencies.empty(); });

  std::vector<uint32_t> cluster_ids(cpu_infos.size());
  uint32_t cluster_id = 0;

  if (valid_capacities) {
    std::sort(cpu_infos.begin(), cpu_infos.end(),
              [](auto a, auto b) { return a.capacity < b.capacity; });
    uint32_t previous_capacity = *cpu_infos[0].capacity;
    for (CpuInfo& cpu_info : cpu_infos) {
      uint32_t capacity = *cpu_info.capacity;
      // If cpus have the same capacity, they should have the same cluster id
      if (previous_capacity < capacity) {
        previous_capacity = capacity;
        cluster_id++;
      }
      cluster_ids[cpu_info.cpu] = cluster_id;
    }
  } else if (valid_frequencies) {
    // Use max frequency if capacities are invalid
    std::vector<CpuMaxFrequency> cpu_max_freqs;
    cpu_max_freqs.reserve(cpu_infos.size());
    for (CpuInfo& info : cpu_infos) {
      cpu_max_freqs.push_back(
          {info.cpu, *std::max_element(info.frequencies.begin(),
                                       info.frequencies.end())});
    }
    std::sort(cpu_max_freqs.begin(), cpu_max_freqs.end(),
              [](auto a, auto b) { return a.max_frequency < b.max_frequency; });

    uint32_t previous_max_freq = cpu_max_freqs[0].max_frequency;
    for (CpuMaxFrequency& cpu_max_freq : cpu_max_freqs) {
      uint32_t max_freq = cpu_max_freq.max_frequency;
      // If cpus have the same max frequency, they should have the same
      // cluster_id
      if (previous_max_freq < max_freq) {
        previous_max_freq = max_freq;
        cluster_id++;
      }
      cluster_ids[cpu_max_freq.cpu] = cluster_id;
    }
  }

  // Add values to tables
  for (CpuInfo& cpu_info : cpu_infos) {
    tables::CpuTable::Id ucpu = context_->cpu_tracker->SetCpuInfo(
        cpu_info.cpu, cpu_info.processor, cluster_ids[cpu_info.cpu],
        cpu_info.capacity);
    for (uint32_t frequency : cpu_info.frequencies) {
      tables::CpuFreqTable::Row cpu_freq_row;
      cpu_freq_row.ucpu = ucpu;
      cpu_freq_row.freq = frequency;
      context_->storage->mutable_cpu_freq_table()->Insert(cpu_freq_row);
    }

    if (auto* id = std::get_if<ArmCpuIdentifier>(&cpu_info.identifier)) {
      ArgsTracker args_tracker(context_);
      args_tracker.AddArgsTo(ucpu)
          .AddArg(arm_cpu_implementer,
                  Variadic::UnsignedInteger(id->implementer))
          .AddArg(arm_cpu_architecture,
                  Variadic::UnsignedInteger(id->architecture))
          .AddArg(arm_cpu_variant, Variadic::UnsignedInteger(id->variant))
          .AddArg(arm_cpu_part, Variadic::UnsignedInteger(id->part))
          .AddArg(arm_cpu_revision, Variadic::UnsignedInteger(id->revision));
    }
  }
}

}  // namespace perfetto::trace_processor
