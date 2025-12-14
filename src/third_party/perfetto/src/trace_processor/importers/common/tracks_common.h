/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACKS_COMMON_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACKS_COMMON_H_

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/tracks.h"

namespace perfetto::trace_processor::tracks {

// This file acts as a shared source track, dimension and unit blueprints which
// are used from many places throughout the codebase. It's strongly recommended
// to use the shared blueprints from this file where possible.

// Documentation URLs for memory counters
// These are defined as constexpr to ensure they stay in sync with the markdown
// anchors in docs/data-sources/memory-counters.md
constexpr const char kMemoryCountersPolledUrl[] =
    "https://perfetto.dev/docs/data-sources/memory-counters#per-process-"
    "polled-counters";
constexpr const char kMemoryCountersFtraceUrl[] =
    "https://perfetto.dev/docs/data-sources/memory-counters#per-process-"
    "memory-events-ftrace";

// Begin dimension blueprints.

inline constexpr auto kGpuDimensionBlueprint =
    tracks::UintDimensionBlueprint("gpu");

inline constexpr auto kUidDimensionBlueprint =
    tracks::UintDimensionBlueprint("uid");

inline constexpr auto kCpuDimensionBlueprint =
    tracks::UintDimensionBlueprint("cpu");

inline constexpr auto kNameFromTraceDimensionBlueprint =
    tracks::StringDimensionBlueprint("name");

inline constexpr auto kLinuxDeviceDimensionBlueprint =
    tracks::StringDimensionBlueprint("linux_device");

inline constexpr auto kIrqDimensionBlueprint =
    tracks::UintDimensionBlueprint("irq");

inline constexpr auto kProcessDimensionBlueprint =
    tracks::UintDimensionBlueprint("upid");

inline constexpr auto kThreadDimensionBlueprint =
    tracks::UintDimensionBlueprint("utid");

inline constexpr auto kNetworkInterfaceDimensionBlueprint =
    tracks::StringDimensionBlueprint("network_interface");

inline constexpr auto kThermalZoneDimensionBlueprint =
    tracks::StringDimensionBlueprint("thermal_zone");

// End dimension blueprints.

// Begin of unit blueprints.

inline constexpr auto kBytesUnitBlueprint =
    tracks::StaticUnitBlueprint("bytes");

// Begin of shared unit blueprints.

// Begin slice blueprints

inline constexpr auto kLegacyGlobalInstantsBlueprint =
    tracks::SliceBlueprint("legacy_chrome_global_instants");

static constexpr auto kAndroidDeviceStateBlueprint =
    tracks::SliceBlueprint("android_device_state",
                           tracks::DimensionBlueprints(),
                           tracks::StaticNameBlueprint("DeviceStateChanged"));

static constexpr auto kAndroidLmkBlueprint = tracks::SliceBlueprint(
    "android_lmk",
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint),
    tracks::StaticNameBlueprint("LMK"));

static constexpr auto kChromeProcessInstantBlueprint = tracks::SliceBlueprint(
    "chrome_process_instant",
    tracks::Dimensions(tracks::kProcessDimensionBlueprint));

// End slice blueprints.

// Begin counter blueprints.

inline constexpr auto kBatteryCounterBlueprint = tracks::CounterBlueprint(
    "battery_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::StringDimensionBlueprint("battery_name"),
        tracks::StringDimensionBlueprint("counter_name")),
    tracks::FnNameBlueprint(
        [](base::StringView battery_name, base::StringView counter_name) {
          if (battery_name.size() > 0) {
            return base::StackString<1024>(
                "batt.%.*s.%.*s", int(battery_name.size()), battery_name.data(),
                int(counter_name.size()), counter_name.data());
          }
          return base::StackString<1024>("batt.%.*s", int(counter_name.size()),
                                         counter_name.data());
        }));

inline constexpr auto kClockFrequencyBlueprint = tracks::CounterBlueprint(
    "clock_frequency",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kLinuxDeviceDimensionBlueprint),
    tracks::FnNameBlueprint([](base::StringView key) {
      return base::StackString<255>("%.*s Frequency", int(key.size()),
                                    key.data());
    }));

inline constexpr auto kClockStateBlueprint = tracks::CounterBlueprint(
    "clock_state",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kLinuxDeviceDimensionBlueprint),
    tracks::FnNameBlueprint([](base::StringView key) {
      return base::StackString<255>("%.*s State", int(key.size()), key.data());
    }));

inline constexpr auto kCpuFrequencyBlueprint = tracks::CounterBlueprint(
    "cpu_frequency",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kCpuDimensionBlueprint),
    tracks::StaticNameBlueprint("cpufreq"));

inline constexpr auto kCpuMaxFrequencyLimitBlueprint = tracks::CounterBlueprint(
    "cpu_max_frequency_limit",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kCpuDimensionBlueprint),
    tracks::FnNameBlueprint([](uint32_t cpu) {
      return base::StackString<255>("Cpu %u Max Freq Limit", cpu);
    }));

inline constexpr auto kCpuMinFrequencyLimitBlueprint = tracks::CounterBlueprint(
    "cpu_min_frequency_limit",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kCpuDimensionBlueprint),
    tracks::FnNameBlueprint([](uint32_t cpu) {
      return base::StackString<255>("Cpu %u Min Freq Limit", cpu);
    }));

inline constexpr auto kGpuFrequencyBlueprint = tracks::CounterBlueprint(
    "gpu_frequency",
    tracks::StaticUnitBlueprint("MHz"),
    tracks::DimensionBlueprints(kGpuDimensionBlueprint),
    tracks::StaticNameBlueprint("gpufreq"));

inline constexpr auto kCpuIdleBlueprint = tracks::CounterBlueprint(
    "cpu_idle",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kCpuDimensionBlueprint),
    tracks::StaticNameBlueprint("cpuidle"));

inline constexpr auto kThermalTemperatureBlueprint = tracks::CounterBlueprint(
    "thermal_temperature",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kThermalZoneDimensionBlueprint),
    tracks::FnNameBlueprint([](base::StringView tz) {
      return base::StackString<255>("%.*s Temperature",
                                    static_cast<int>(tz.size()), tz.data());
    }));

inline constexpr auto kCoolingDeviceCounterBlueprint = tracks::CounterBlueprint(
    "cooling_device_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kLinuxDeviceDimensionBlueprint),
    tracks::FnNameBlueprint([](base::StringView cdev) {
      return base::StackString<255>("%.*s Cooling Device",
                                    static_cast<int>(cdev.size()), cdev.data());
    }));

inline constexpr auto kChromeProcessStatsBlueprint = tracks::CounterBlueprint(
    "chrome_process_stats",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint,
                                tracks::StringDimensionBlueprint("key")),
    tracks::FnNameBlueprint([](uint32_t, base::StringView name) {
      return base::StackString<128>("chrome.%.*s", int(name.size()),
                                    name.data());
    }));

inline constexpr auto kAndroidScreenStateBlueprint =
    tracks::CounterBlueprint("screen_state",
                             tracks::UnknownUnitBlueprint(),
                             tracks::DimensionBlueprints(),
                             tracks::StaticNameBlueprint("ScreenState"));

inline constexpr auto kAndroidBatteryStatsBlueprint = tracks::CounterBlueprint(
    "battery_stats",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::StringDimensionBlueprint("counter_key")),
    tracks::FnNameBlueprint([](base::StringView name) {
      return base::StackString<1024>("%.*s", int(name.size()), name.data());
    }));

inline constexpr auto kAndroidAtraceCounterBlueprint = tracks::CounterBlueprint(
    "atrace_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint,
                                tracks::kNameFromTraceDimensionBlueprint),
    tracks::FnNameBlueprint([](uint32_t, base::StringView name) {
      return base::StackString<1024>("%.*s", int(name.size()), name.data());
    }));

inline constexpr auto kOomScoreAdjBlueprint = tracks::CounterBlueprint(
    "oom_score_adj",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint),
    tracks::StaticNameBlueprint("oom_score_adj"));

inline constexpr auto kOomScoreAdjThreadFallbackBlueprint =
    tracks::CounterBlueprint(
        "oom_score_adj_thread_fallback",
        tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint),
        tracks::StaticNameBlueprint("oom_score_adj"));

inline constexpr auto kMmEventFnNameBlueprint = tracks::FnNameBlueprint(
    [](uint32_t, base::StringView type, base::StringView metric) {
      return base::StackString<1024>("mem.mm.%.*s.%.*s", int(type.size()),
                                     type.data(), int(metric.size()),
                                     metric.data());
    });

inline constexpr auto kMmEventBlueprint = tracks::CounterBlueprint(
    "mm_event",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kProcessDimensionBlueprint,
        tracks::StringDimensionBlueprint("mm_event_type"),
        tracks::StringDimensionBlueprint("mm_event_metric")),
    kMmEventFnNameBlueprint);

inline constexpr auto kMmEventThreadFallbackBlueprint =
    tracks::CounterBlueprint(
        "mm_event_thread_fallback",
        tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::kThreadDimensionBlueprint,
            tracks::StringDimensionBlueprint("mm_event_type"),
            tracks::StringDimensionBlueprint("mm_event_metric")),
        kMmEventFnNameBlueprint);

inline constexpr auto kPerfGlobalCounterBlueprint = tracks::CounterBlueprint(
    "perf_global_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::UintDimensionBlueprint("perf_session_id"),
        tracks::kNameFromTraceDimensionBlueprint),
    tracks::DynamicNameBlueprint());

inline constexpr auto kPerfCpuCounterBlueprint = tracks::CounterBlueprint(
    "perf_cpu_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kCpuDimensionBlueprint,
        tracks::UintDimensionBlueprint("perf_session_id"),
        tracks::kNameFromTraceDimensionBlueprint),
    tracks::DynamicNameBlueprint());

inline constexpr auto kGlobalGpuMemoryBlueprint =
    tracks::CounterBlueprint("gpu_memory",
                             tracks::kBytesUnitBlueprint,
                             tracks::DimensionBlueprints(),
                             tracks::StaticNameBlueprint("GPU Memory"));

inline constexpr auto kProcessGpuMemoryBlueprint = tracks::CounterBlueprint(
    "process_gpu_memory",
    tracks::kBytesUnitBlueprint,
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint),
    tracks::StaticNameBlueprint("GPU Memory"));

inline constexpr auto kProcessMemoryBlueprint = tracks::CounterBlueprint(
    "process_memory",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kProcessDimensionBlueprint,
        tracks::StringDimensionBlueprint("process_memory_key")),
    tracks::FnNameBlueprint([](uint32_t, base::StringView key) {
      return base::StackString<1024>("mem.%.*s", int(key.size()), key.data());
    }),
    tracks::FnDescriptionBlueprint([](uint32_t, base::StringView key) {
      if (key == "virt") {
        return base::StackString<1024>(
            "Virtual memory size (VmSize): Total virtual address space "
            "reserved by the process. Useful for understanding address space "
            "layout and detecting excessive memory reservations. WARNING: "
            "Polled periodically, so may miss short-lived changes. See %s for "
            "information.",
            kMemoryCountersPolledUrl);
      }
      if (key == "rss") {
        return base::StackString<1024>(
            "Resident set size (VmRSS): Total physical memory currently used "
            "by the process (rss.anon + rss.file + rss.shmem). Key metric for "
            "understanding actual memory pressure and OOM risk. WARNING: "
            "Polled periodically, so use rss.anon + rss.file + rss.shmem from "
            "ftrace for complementary high-resolution tracking. See %s for "
            "information.",
            kMemoryCountersPolledUrl);
      }
      if (key == "rss.anon") {
        return base::StackString<1024>(
            "Anonymous resident memory (RssAnon): Physical memory for heap and "
            "stack allocations not backed by files. High values indicate "
            "memory allocations from malloc/new. Most important for app memory "
            "usage and detecting memory leaks. See %s for information.",
            kMemoryCountersFtraceUrl);
      }
      if (key == "rss.file") {
        return base::StackString<1024>(
            "File-backed resident memory (RssFile): Physical memory used for "
            "memory-mapped files (e.g., code, libraries, mmap'd files). Useful "
            "for understanding shared library overhead. Can be reclaimed under "
            "memory pressure. See %s for information.",
            kMemoryCountersFtraceUrl);
      }
      if (key == "rss.shmem") {
        return base::StackString<1024>(
            "Shared memory resident pages (RssShmem): Physical memory in "
            "shared memory regions (shmem/tmpfs). Used for IPC and shared "
            "data. Important for identifying inter-process memory usage. See "
            "%s for information.",
            kMemoryCountersFtraceUrl);
      }
      if (key == "swap") {
        return base::StackString<1024>(
            "Swapped memory (VmSwap): Memory moved to swap storage. Does not "
            "necessarily indicate severe pressure - kswapd may swap out "
            "inactive memory proactively. However, if actively used and "
            "swap/anon ratio is high, this suggests inefficient memory usage "
            "or leaks. Accessing swapped memory causes major page faults and "
            "performance degradation. Available from both ftrace and polling "
            "for complementary views. See %s or %s for information.",
            kMemoryCountersFtraceUrl, kMemoryCountersPolledUrl);
      }
      if (key == "locked") {
        return base::StackString<1024>(
            "Locked memory pages (VmLocked): Memory pinned in RAM that cannot "
            "be swapped out or reclaimed (via mlock). High values prevent "
            "memory reclamation during pressure. Typically set via profiler-"
            "guided optimization to minimize faults on critical paths. "
            "WARNING: Polled periodically, so may miss short-lived changes. "
            "See %s for information.",
            kMemoryCountersPolledUrl);
      }
      if (key == "rss.watermark") {
        return base::StackString<1024>(
            "Peak RSS (VmHWM): Historical maximum RSS reached by the "
            "process. Useful for capacity planning and detecting memory usage "
            "spikes that may have triggered OOM or performance issues. "
            "WARNING: Polled periodically, so the time of the spike may not be "
            "accurate. Prefer relying on rss.anon + rss.file + rss.shmem which "
            "have pushed ftrace events so are more time accurate. See %s for "
            "information.",
            kMemoryCountersPolledUrl);
      }
      if (key == "dmabuf_rss") {
        return base::StackString<1024>(
            "DMA buffer RSS: Physical memory used for DMA buffers (successor "
            "to Android ION). Used for surfaces, hardware bitmaps, media/"
            "camera buffers, and other GPU/hardware accelerator memory. "
            "Critical for graphics/camera apps. High values indicate GPU "
            "memory pressure or resource leaks in graphics/media pipelines. "
            "See dmabuf ftrace events or /proc/[pid]/fdinfo polling for "
            "information.");
      }
      if (key == "unreclaimable") {
        return base::StackString<1024>(
            "Unreclaimable memory: KGSL driver specific event, rarely useful.");
      }
      if (key == "unknown") {
        return base::StackString<1024>(
            "Unknown memory type: Memory category not recognized by the "
            "tracer. May indicate new kernel memory types or parsing errors. "
            "Should be investigated if non-zero.");
      }
      PERFETTO_FATAL("Cannot describe unknown memory track key");
    }));

inline constexpr auto kProcessMemoryThreadFallbackBlueprint =
    tracks::CounterBlueprint(
        "process_memory_thread_fallback",
        tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::kThreadDimensionBlueprint,
            tracks::StringDimensionBlueprint("process_memory_key")),
        tracks::FnNameBlueprint([](uint32_t, base::StringView key) {
          return base::StackString<1024>("mem.%.*s", int(key.size()),
                                         key.data());
        }));

static constexpr auto kJsonCounterBlueprint = tracks::CounterBlueprint(
    "json_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint,
                                tracks::kNameFromTraceDimensionBlueprint),
    tracks::DynamicNameBlueprint());

inline constexpr auto kJsonCounterThreadFallbackBlueprint =
    tracks::CounterBlueprint(
        "json_counter_thread_fallback",
        tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint,
                                    tracks::kNameFromTraceDimensionBlueprint),
        tracks::DynamicNameBlueprint());

inline constexpr auto kGpuCounterBlueprint = tracks::CounterBlueprint(
    "gpu_counter",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kGpuDimensionBlueprint,
                                tracks::kNameFromTraceDimensionBlueprint),
    tracks::DynamicNameBlueprint());

// End counter blueprints.

}  // namespace perfetto::trace_processor::tracks

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACKS_COMMON_H_
