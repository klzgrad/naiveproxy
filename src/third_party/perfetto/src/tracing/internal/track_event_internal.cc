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

#include "perfetto/tracing/internal/track_event_internal.h"

#include "perfetto/base/proc_utils.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/internal/track_event_interned_fields.h"
#include "perfetto/tracing/track_event.h"
#include "perfetto/tracing/track_event_category_registry.h"
#include "perfetto/tracing/track_event_interned_data_index.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/common/track_event_descriptor.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.pbzero.h"
#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)
#include <os/signpost.h>
#endif

using perfetto::protos::pbzero::ClockSnapshot;

namespace perfetto {

TrackEventSessionObserver::~TrackEventSessionObserver() = default;
void TrackEventSessionObserver::OnSetup(const DataSourceBase::SetupArgs&) {}
void TrackEventSessionObserver::OnStart(const DataSourceBase::StartArgs&) {}
void TrackEventSessionObserver::OnStop(const DataSourceBase::StopArgs&) {}
void TrackEventSessionObserver::WillClearIncrementalState(
    const DataSourceBase::ClearIncrementalStateArgs&) {}

TrackEventTlsStateUserData::~TrackEventTlsStateUserData() = default;

namespace internal {

BaseTrackEventInternedDataIndex::~BaseTrackEventInternedDataIndex() = default;

namespace {

static constexpr const char kSlowTag[] = "slow";
static constexpr const char kDebugTag[] = "debug";
static constexpr const char kFilteredEventName[] = "FILTERED";

constexpr auto kClockIdIncremental =
    TrackEventIncrementalState::kClockIdIncremental;

constexpr auto kClockIdAbsolute = TrackEventIncrementalState::kClockIdAbsolute;

class TrackEventSessionObserverRegistry {
 public:
  static TrackEventSessionObserverRegistry* GetInstance() {
    static TrackEventSessionObserverRegistry* instance =
        new TrackEventSessionObserverRegistry();  // leaked
    return instance;
  }

  void AddObserverForRegistry(const TrackEventCategoryRegistry& registry,
                              TrackEventSessionObserver* observer) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    observers_.emplace_back(&registry, observer);
  }

  void RemoveObserverForRegistry(const TrackEventCategoryRegistry& registry,
                                 TrackEventSessionObserver* observer) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(),
                                 RegisteredObserver(&registry, observer)),
                     observers_.end());
  }

  void ForEachObserverForRegistries(
      const std::vector<const TrackEventCategoryRegistry*>& registries,
      std::function<void(TrackEventSessionObserver*)> callback) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    for (auto& registered_observer : observers_) {
      for (const auto* registry : registries) {
        if (registry == registered_observer.registry) {
          callback(registered_observer.observer);
        }
      }
    }
  }

 private:
  struct RegisteredObserver {
    RegisteredObserver(const TrackEventCategoryRegistry* r,
                       TrackEventSessionObserver* o)
        : registry(r), observer(o) {}
    bool operator==(const RegisteredObserver& other) {
      return registry == other.registry && observer == other.observer;
    }
    const TrackEventCategoryRegistry* registry;
    TrackEventSessionObserver* observer;
  };

  std::recursive_mutex mutex_;
  std::vector<RegisteredObserver> observers_;
};

enum class MatchType { kExact, kPattern, kWildcard };

bool NameMatchesPattern(const std::string& pattern,
                        const std::string& name,
                        MatchType match_type) {
  // To avoid pulling in all of std::regex, for now we only support a single "*"
  // wildcard at the end of the pattern.
  size_t i = pattern.find('*');
  if (i != std::string::npos) {
    PERFETTO_DCHECK(i == pattern.size() - 1);
    if (i == 0) {
      return match_type == MatchType::kWildcard;
    }
    if (match_type != MatchType::kPattern)
      return false;
    return name.substr(0, i) == pattern.substr(0, i);
  }
  return name == pattern;
}

bool NameMatchesPatternList(const std::vector<std::string>& patterns,
                            const std::string& name,
                            MatchType match_type) {
  for (const auto& pattern : patterns) {
    if (NameMatchesPattern(pattern, name, match_type))
      return true;
  }
  return false;
}

}  // namespace

TrackEventInternal& TrackEventInternal::GetInstance() {
  static base::NoDestructor<TrackEventInternal> state;
  return state.ref();
}

// static
const Track TrackEventInternal::kDefaultTrack{};

// static
std::atomic<int> TrackEventInternal::session_count_{};

std::vector<const TrackEventCategoryRegistry*>
TrackEventInternal::GetRegistries() {
  std::unique_lock<std::mutex> lock(mu_);
  return registries_;
}

std::vector<const TrackEventCategoryRegistry*> TrackEventInternal::AddRegistry(
    const TrackEventCategoryRegistry* registry) {
  std::unique_lock<std::mutex> lock(mu_);
  registries_.push_back(registry);
  return registries_;
}

void TrackEventInternal::ResetRegistriesForTesting() {
  std::unique_lock<std::mutex> lock(mu_);
  registries_.clear();
}

// static
bool TrackEventInternal::Initialize(
    const std::vector<const TrackEventCategoryRegistry*> registries,
    bool (*register_data_source)(const DataSourceDescriptor&)) {
  DataSourceDescriptor dsd;
  dsd.set_name("track_event");

  protozero::HeapBuffered<protos::pbzero::TrackEventDescriptor> ted;
  for (const auto* registry : registries) {
    for (size_t i = 0; i < registry->category_count(); i++) {
      auto category = registry->GetCategory(i);
      // Don't register group categories.
      if (category->IsGroup())
        continue;
      auto cat = ted->add_available_categories();
      cat->set_name(category->name);
      if (category->description)
        cat->set_description(category->description);
      for (const auto& tag : category->tags) {
        if (tag) {
          cat->add_tags(tag);
        }
      }
    }
  }
  dsd.set_track_event_descriptor_raw(ted.SerializeAsString());

  return register_data_source(dsd);
}

// static
bool TrackEventInternal::AddSessionObserver(
    const TrackEventCategoryRegistry& registry,
    TrackEventSessionObserver* observer) {
  TrackEventSessionObserverRegistry::GetInstance()->AddObserverForRegistry(
      registry, observer);
  return true;
}

// static
void TrackEventInternal::RemoveSessionObserver(
    const TrackEventCategoryRegistry& registry,
    TrackEventSessionObserver* observer) {
  TrackEventSessionObserverRegistry::GetInstance()->RemoveObserverForRegistry(
      registry, observer);
}

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
static constexpr protos::pbzero::BuiltinClock kDefaultTraceClock =
    protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
#else
static constexpr protos::pbzero::BuiltinClock kDefaultTraceClock =
    protos::pbzero::BUILTIN_CLOCK_MONOTONIC;
#endif

// static
protos::pbzero::BuiltinClock TrackEventInternal::clock_ = kDefaultTraceClock;

// static
bool TrackEventInternal::disallow_merging_with_system_tracks_ = false;

// static
void TrackEventInternal::EnableRegistry(
    const TrackEventCategoryRegistry* registry,
    const protos::gen::TrackEventConfig& config,
    uint32_t internal_instance_index) {
  for (size_t i = 0; i < registry->category_count(); i++) {
    if (IsCategoryEnabled(*registry, config, *registry->GetCategory(i))) {
      PERFETTO_DLOG("EnableRegistry %" PRIu32 " %zu", internal_instance_index,
                    i);
      registry->EnableCategoryForInstance(i, internal_instance_index);
    }
  }
}

void TrackEventInternal::EnableTracing(
    const protos::gen::TrackEventConfig& config,
    const DataSourceBase::SetupArgs& args) {
  std::vector<const TrackEventCategoryRegistry*> registries;
  {
    std::unique_lock<std::mutex> lock(mu_);
    registries = registries_;
    for (const auto& registry : registries) {
      for (size_t i = 0; i < registry->category_count(); i++) {
        if (IsCategoryEnabled(*registry, config, *registry->GetCategory(i)))
          registry->EnableCategoryForInstance(i, args.internal_instance_index);
      }
    }
  }
  TrackEventSessionObserverRegistry::GetInstance()
      ->ForEachObserverForRegistries(
          registries, [&](TrackEventSessionObserver* o) { o->OnSetup(args); });
}

void TrackEventInternal::DisableTracing(uint32_t internal_instance_index) {
  std::unique_lock<std::mutex> lock(mu_);
  for (const auto& registry : registries_) {
    for (size_t i = 0; i < registry->category_count(); i++)
      registry->DisableCategoryForInstance(i, internal_instance_index);
  }
}

// static
void TrackEventInternal::OnStart(const DataSourceBase::StartArgs& args) {
  session_count_.fetch_add(1);
  TrackEventSessionObserverRegistry::GetInstance()
      ->ForEachObserverForRegistries(
          GetInstance().GetRegistries(),
          [&](TrackEventSessionObserver* o) { o->OnStart(args); });
}

// static
void TrackEventInternal::OnStop(const DataSourceBase::StopArgs& args) {
  TrackEventSessionObserverRegistry::GetInstance()
      ->ForEachObserverForRegistries(
          GetInstance().GetRegistries(),
          [&](TrackEventSessionObserver* o) { o->OnStop(args); });
}

// static
void TrackEventInternal::WillClearIncrementalState(
    const DataSourceBase::ClearIncrementalStateArgs& args) {
  TrackEventSessionObserverRegistry::GetInstance()
      ->ForEachObserverForRegistries(GetInstance().GetRegistries(),
                                     [&](TrackEventSessionObserver* o) {
                                       o->WillClearIncrementalState(args);
                                     });
}

// static
bool TrackEventInternal::IsCategoryEnabled(
    const TrackEventCategoryRegistry& registry,
    const protos::gen::TrackEventConfig& config,
    const Category& category) {
  // If this is a group category, check if any of its constituent categories are
  // enabled. If so, then this one is enabled too.
  if (category.IsGroup()) {
    bool result = false;
    category.ForEachGroupMember([&](const char* member_name, size_t name_size) {
      for (size_t i = 0; i < registry.category_count(); i++) {
        const auto ref_category = registry.GetCategory(i);
        // Groups can't refer to other groups.
        if (ref_category->IsGroup())
          continue;
        // Require an exact match.
        if (ref_category->name_size() != name_size ||
            strncmp(ref_category->name, member_name, name_size)) {
          continue;
        }
        if (IsCategoryEnabled(registry, config, *ref_category)) {
          result = true;
          // Break ForEachGroupMember() loop.
          return false;
        }
        break;
      }
      // No match? Must be a dynamic category.
      DynamicCategory dyn_category(std::string(member_name, name_size));
      Category ref_category{Category::FromDynamicCategory(dyn_category)};
      if (IsCategoryEnabled(registry, config, ref_category)) {
        result = true;
        // Break ForEachGroupMember() loop.
        return false;
      }
      // No match found => keep iterating.
      return true;
    });
    return result;
  }

  auto has_matching_tag = [&](std::function<bool(const char*)> matcher) {
    for (const auto& tag : category.tags) {
      if (!tag)
        break;
      if (matcher(tag))
        return true;
    }
    return false;
  };

  // First try exact matches, then pattern matches. Last, try the global
  // wildcard.
  const std::array<MatchType, 3> match_types = {
      {MatchType::kExact, MatchType::kPattern, MatchType::kWildcard}};
  for (auto match_type : match_types) {
    // 1. Enabled categories.
    if (NameMatchesPatternList(config.enabled_categories(), category.name,
                               match_type)) {
      return true;
    }

    // 2. Disabled categories.
    if (NameMatchesPatternList(config.disabled_categories(), category.name,
                               match_type)) {
      return false;
    }

    // 3. Disabled tags.
    if (has_matching_tag([&](const char* tag) {
          if (config.disabled_tags_size()) {
            return NameMatchesPatternList(config.disabled_tags(), tag,
                                          match_type);
          } else if (config.enabled_tags_size() == 0) {
            // The "slow" and "debug" tags are disabled by default.
            return NameMatchesPattern(kSlowTag, tag, match_type) ||
                   NameMatchesPattern(kDebugTag, tag, match_type);
          }
          return false;
        })) {
      return false;
    }

    // 4. Enabled tags.
    if (has_matching_tag([&](const char* tag) {
          return NameMatchesPatternList(config.enabled_tags(), tag, match_type);
        })) {
      return true;
    }
  }

  // If nothing matched, enable the category by default.
  return true;
}

// static
uint64_t TrackEventInternal::GetTimeNs() {
  if (GetClockId() == protos::pbzero::BUILTIN_CLOCK_BOOTTIME)
    return static_cast<uint64_t>(perfetto::base::GetBootTimeNs().count());
  else if (GetClockId() == protos::pbzero::BUILTIN_CLOCK_MONOTONIC)
    return static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count());
  PERFETTO_DCHECK(GetClockId() == protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW);
  return static_cast<uint64_t>(perfetto::base::GetWallTimeRawNs().count());
}

// static
TraceTimestamp TrackEventInternal::GetTraceTime() {
  return {kClockIdIncremental, GetTimeNs()};
}

// static
int TrackEventInternal::GetSessionCount() {
  return session_count_.load();
}

// static
void TrackEventInternal::ResetIncrementalState(
    TraceWriterBase* trace_writer,
    TrackEventIncrementalState* incr_state,
    const TrackEventTlsState& tls_state,
    const TraceTimestamp& timestamp) {
  auto sequence_timestamp = timestamp;
  if (timestamp.clock_id != kClockIdIncremental) {
    sequence_timestamp = TrackEventInternal::GetTraceTime();
  }

  incr_state->last_timestamp_ns = sequence_timestamp.value;
  auto default_track = ThreadTrack::Current();
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  auto thread_time_counter_track =
      CounterTrack("thread_time", default_track)
          .set_is_incremental(true)
          .set_unit_multiplier(static_cast<int64_t>(ts_unit_multiplier))
          .set_type(protos::gen::CounterDescriptor::COUNTER_THREAD_TIME_NS);
  {
    // Mark any incremental state before this point invalid. Also set up
    // defaults so that we don't need to repeat constant data for each packet.
    auto packet = NewTracePacket(
        trace_writer, incr_state, tls_state, timestamp,
        protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
    auto defaults = packet->set_trace_packet_defaults();
    defaults->set_timestamp_clock_id(tls_state.default_clock);
    // Establish the default track for this event sequence.
    auto track_defaults = defaults->set_track_event_defaults();
    track_defaults->set_track_uuid(default_track.uuid);
    if (tls_state.enable_thread_time_sampling) {
      track_defaults->add_extra_counter_track_uuids(
          thread_time_counter_track.uuid);
    }

#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)
    // Emit a MacOS point-of-interest signpost to synchronize Mac profiler time
    // with boot time.
    // TODO(leszeks): Consider allowing synchronization against other clocks
    // than boot time.
    static os_log_t log_handle = os_log_create(
        "dev.perfetto.clock_sync", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
    os_signpost_event_emit(
        log_handle, OS_SIGNPOST_ID_EXCLUSIVE, "boottime", "%" PRId64,
        static_cast<uint64_t>(perfetto::base::GetBootTimeNs().count()));
#endif

    if (tls_state.default_clock != static_cast<uint32_t>(GetClockId())) {
      ClockSnapshot* clocks = packet->set_clock_snapshot();
      // Trace clock.
      ClockSnapshot::Clock* trace_clock = clocks->add_clocks();
      trace_clock->set_clock_id(static_cast<uint32_t>(GetClockId()));
      trace_clock->set_timestamp(sequence_timestamp.value);

      if (PERFETTO_LIKELY(tls_state.default_clock == kClockIdIncremental)) {
        // Delta-encoded incremental clock in nanoseconds by default but
        // configurable by |tls_state.timestamp_unit_multiplier|.
        ClockSnapshot::Clock* clock_incremental = clocks->add_clocks();
        clock_incremental->set_clock_id(kClockIdIncremental);
        clock_incremental->set_timestamp(sequence_timestamp.value /
                                         ts_unit_multiplier);
        clock_incremental->set_is_incremental(true);
        clock_incremental->set_unit_multiplier_ns(ts_unit_multiplier);
      }
      if (ts_unit_multiplier > 1) {
        // absolute clock with custom timestamp_unit_multiplier.
        ClockSnapshot::Clock* absolute_clock = clocks->add_clocks();
        absolute_clock->set_clock_id(kClockIdAbsolute);
        absolute_clock->set_timestamp(sequence_timestamp.value /
                                      ts_unit_multiplier);
        absolute_clock->set_is_incremental(false);
        absolute_clock->set_unit_multiplier_ns(ts_unit_multiplier);
      }
    }
  }

  // Every thread should write a descriptor for its default track, because most
  // trace points won't explicitly reference it. We also write the process
  // descriptor from every thread that writes trace events to ensure it gets
  // emitted at least once.
  incr_state->seen_tracks.insert(default_track.uuid);
  WriteTrackDescriptor(default_track, trace_writer, incr_state, tls_state,
                       sequence_timestamp);

  incr_state->seen_tracks.insert(ProcessTrack::Current().uuid);
  WriteTrackDescriptor(ProcessTrack::Current(), trace_writer, incr_state,
                       tls_state, sequence_timestamp);

  if (tls_state.enable_thread_time_sampling) {
    WriteTrackDescriptor(thread_time_counter_track, trace_writer, incr_state,
                         tls_state, sequence_timestamp);
  }
}

// static
protozero::MessageHandle<protos::pbzero::TracePacket>
TrackEventInternal::NewTracePacket(TraceWriterBase* trace_writer,
                                   TrackEventIncrementalState* incr_state,
                                   const TrackEventTlsState& tls_state,
                                   TraceTimestamp timestamp,
                                   uint32_t seq_flags) {
  if (PERFETTO_UNLIKELY(tls_state.default_clock != kClockIdIncremental &&
                        timestamp.clock_id == kClockIdIncremental)) {
    timestamp.clock_id = tls_state.default_clock;
  }
  auto packet = trace_writer->NewTracePacket();
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  if (PERFETTO_LIKELY(timestamp.clock_id == kClockIdIncremental)) {
    if (PERFETTO_LIKELY(incr_state->last_timestamp_ns <= timestamp.value)) {
      // No need to set the clock id here, since kClockIdIncremental is the
      // clock id assumed by default.
      auto time_diff_ns = timestamp.value - incr_state->last_timestamp_ns;
      auto time_diff_units = time_diff_ns / ts_unit_multiplier;
      packet->set_timestamp(time_diff_units);
      incr_state->last_timestamp_ns += time_diff_units * ts_unit_multiplier;
    } else {
      packet->set_timestamp(timestamp.value / ts_unit_multiplier);
      packet->set_timestamp_clock_id(ts_unit_multiplier == 1
                                         ? static_cast<uint32_t>(GetClockId())
                                         : kClockIdAbsolute);
    }
  } else if (PERFETTO_LIKELY(timestamp.clock_id == tls_state.default_clock)) {
    packet->set_timestamp(timestamp.value / ts_unit_multiplier);
  } else {
    packet->set_timestamp(timestamp.value);
    packet->set_timestamp_clock_id(timestamp.clock_id);
  }
  packet->set_sequence_flags(seq_flags);
  return packet;
}

// static
void TrackEventInternal::WriteEventName(StaticString event_name,
                                        perfetto::EventContext& event_ctx,
                                        const TrackEventTlsState&) {
  if (PERFETTO_LIKELY(event_name.value != nullptr)) {
    size_t name_iid = InternedEventName::Get(&event_ctx, event_name.value);
    event_ctx.event()->set_name_iid(name_iid);
  }
}

// static
void TrackEventInternal::WriteEventName(perfetto::DynamicString event_name,
                                        perfetto::EventContext& event_ctx,
                                        const TrackEventTlsState& tls_state) {
  if (PERFETTO_UNLIKELY(tls_state.filter_dynamic_event_names)) {
    event_ctx.event()->set_name(kFilteredEventName,
                                sizeof(kFilteredEventName) - 1);
  } else {
    event_ctx.event()->set_name(event_name.value, event_name.length);
  }
}

// static
EventContext TrackEventInternal::WriteEvent(
    TraceWriterBase* trace_writer,
    TrackEventIncrementalState* incr_state,
    TrackEventTlsState& tls_state,
    const Category* category,
    perfetto::protos::pbzero::TrackEvent::Type type,
    const TraceTimestamp& timestamp,
    bool on_current_thread_track) {
  PERFETTO_DCHECK(!incr_state->was_cleared);
  auto packet = NewTracePacket(trace_writer, incr_state, tls_state, timestamp);
  EventContext ctx(std::move(packet), incr_state, &tls_state);

  auto track_event = ctx.event();
  if (type != protos::pbzero::TrackEvent::TYPE_UNSPECIFIED)
    track_event->set_type(type);

  if (tls_state.enable_thread_time_sampling && on_current_thread_track) {
    if (tls_state.thread_time_subsampling_ns == 0 ||
        incr_state->last_thread_time_timestamp_ns == 0 ||
        timestamp.value >= incr_state->last_thread_time_timestamp_ns +
                               tls_state.thread_time_subsampling_ns) {
      auto thread_time_ns = base::GetThreadCPUTimeNs().count();
      auto thread_time_delta_ns =
          thread_time_ns - incr_state->last_thread_time_ns;
      incr_state->last_thread_time_ns = thread_time_ns;
      incr_state->last_thread_time_timestamp_ns = timestamp.value;
      track_event->add_extra_counter_values(
          thread_time_delta_ns /
          static_cast<int64_t>(tls_state.timestamp_unit_multiplier));
    } else {
      // When subsampling, the skip emitting values.
      track_event->add_extra_counter_values(0);
    }
  }

  // We assume that |category| points to the string with static lifetime.
  // This means we can use their addresses as interning keys.
  // TODO(skyostil): Intern categories at compile time.
  if (category && type != protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      type != protos::pbzero::TrackEvent::TYPE_COUNTER) {
    category->ForEachGroupMember(
        [&](const char* member_name, size_t name_size) {
          size_t category_iid =
              InternedEventCategory::Get(&ctx, member_name, name_size);
          track_event->add_category_iids(category_iid);
          return true;
        });
  }
  return ctx;
}

// static
protos::pbzero::DebugAnnotation* TrackEventInternal::AddDebugAnnotation(
    perfetto::EventContext* event_ctx,
    const char* name) {
  auto annotation = event_ctx->event()->add_debug_annotations();
  annotation->set_name_iid(InternedDebugAnnotationName::Get(event_ctx, name));
  return annotation;
}

// static
protos::pbzero::DebugAnnotation* TrackEventInternal::AddDebugAnnotation(
    perfetto::EventContext* event_ctx,
    perfetto::DynamicString name) {
  auto annotation = event_ctx->event()->add_debug_annotations();
  annotation->set_name(name.value);
  return annotation;
}

void TrackEventDataSource::OnStart(const DataSourceBase::StartArgs& args) {
  TrackEventInternal::OnStart(args);
}

}  // namespace internal

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS_WITH_ATTRS(
    PERFETTO_EXPORT_COMPONENT,
    perfetto::internal::TrackEventDataSource,
    perfetto::internal::TrackEventDataSourceTraits);

}  // namespace perfetto
