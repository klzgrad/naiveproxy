/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "perfetto/public/abi/track_event_abi.h"
#include "perfetto/public/abi/track_event_hl_abi.h"
#include "perfetto/public/abi/track_event_ll_abi.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <optional>

#include "perfetto/base/compiler.h"
#include "perfetto/base/flat_set.h"
#include "perfetto/base/thread_annotations.h"
#include "perfetto/base/thread_utils.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/thread_utils.h"
#include "perfetto/protozero/contiguous_memory_range.h"
#include "perfetto/public/compiler.h"
#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/internal/basic_types.h"
#include "perfetto/tracing/internal/data_source_internal.h"
#include "perfetto/tracing/internal/track_event_internal.h"
#include "perfetto/tracing/track.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/common/track_event_descriptor.pbzero.h"
#include "protos/perfetto/config/data_source_config.gen.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"
#include "src/shared_lib/intern_map.h"
#include "src/shared_lib/reset_for_testing.h"

struct PerfettoTeCategoryImpl* perfetto_te_any_categories;

PERFETTO_ATOMIC(bool) * perfetto_te_any_categories_enabled;

uint64_t perfetto_te_process_track_uuid;

struct PerfettoTeCategoryImpl {
  std::atomic<bool> flag{false};
  std::atomic<uint8_t> instances{0};
  PerfettoTeCategoryDescriptor* desc = nullptr;
  uint64_t cat_iid = 0;
  PerfettoTeCategoryImplCallback cb = nullptr;
  void* cb_user_arg = nullptr;
};

enum class MatchType { kExact, kPattern };

static bool NameMatchesPattern(const std::string& pattern,
                               const perfetto::base::StringView& name,
                               MatchType match_type) {
  // To avoid pulling in all of std::regex, for now we only support a single "*"
  // wildcard at the end of the pattern.
  size_t i = pattern.find('*');
  if (i != std::string::npos) {
    if (match_type != MatchType::kPattern)
      return false;
    return name.substr(0, i) ==
           perfetto::base::StringView(pattern).substr(0, i);
  }
  return name == perfetto::base::StringView(pattern);
}

static bool NameMatchesPatternList(const std::vector<std::string>& patterns,
                                   const perfetto::base::StringView& name,
                                   MatchType match_type) {
  for (const auto& pattern : patterns) {
    if (NameMatchesPattern(pattern, name, match_type))
      return true;
  }
  return false;
}

static bool IsSingleCategoryEnabled(
    const PerfettoTeCategoryDescriptor& c,
    const perfetto::protos::gen::TrackEventConfig& config) {
  auto has_matching_tag = [&](std::function<bool(const char*)> matcher) {
    for (size_t i = 0; i < c.num_tags; ++i) {
      if (matcher(c.tags[i]))
        return true;
    }
    return false;
  };
  // First try exact matches, then pattern matches.
  const std::array<MatchType, 2> match_types = {
      {MatchType::kExact, MatchType::kPattern}};
  for (auto match_type : match_types) {
    // 1. Enabled categories.
    if (NameMatchesPatternList(config.enabled_categories(), c.name,
                               match_type)) {
      return true;
    }

    // 2. Enabled tags.
    if (has_matching_tag([&](const char* tag) {
          return NameMatchesPatternList(config.enabled_tags(), tag, match_type);
        })) {
      return true;
    }

    // 3. Disabled categories.
    if (NameMatchesPatternList(config.disabled_categories(), c.name,
                               match_type)) {
      return false;
    }

    // 4. Disabled tags.
    if (has_matching_tag([&](const char* tag) {
          return NameMatchesPatternList(config.disabled_tags(), tag,
                                        match_type);
        })) {
      return false;
    }
  }

  // If nothing matched, the category is disabled by default. N.B. this behavior
  // is different than the C++ TrackEvent API.
  return false;
}

static bool IsRegisteredCategoryEnabled(
    const PerfettoTeCategoryImpl& cat,
    const perfetto::protos::gen::TrackEventConfig& config) {
  if (!cat.desc) {
    return false;
  }
  return IsSingleCategoryEnabled(*cat.desc, config);
}

static void EnableRegisteredCategory(PerfettoTeCategoryImpl* cat,
                                     uint32_t instance_index) {
  PERFETTO_DCHECK(instance_index < perfetto::internal::kMaxDataSourceInstances);
  // Matches the acquire_load in DataSource::Trace().
  uint8_t old = cat->instances.fetch_or(
      static_cast<uint8_t>(1u << instance_index), std::memory_order_release);
  bool global_state_changed = old == 0;
  if (global_state_changed) {
    cat->flag.store(true, std::memory_order_relaxed);
  }
  if (cat->cb) {
    cat->cb(cat, instance_index, /*created=*/true, global_state_changed,
            cat->cb_user_arg);
  }
}

static void DisableRegisteredCategory(PerfettoTeCategoryImpl* cat,
                                      uint32_t instance_index) {
  PERFETTO_DCHECK(instance_index < perfetto::internal::kMaxDataSourceInstances);
  // Matches the acquire_load in DataSource::Trace().
  cat->instances.fetch_and(static_cast<uint8_t>(~(1u << instance_index)),
                           std::memory_order_release);
  bool global_state_changed = false;
  if (!cat->instances.load(std::memory_order_relaxed)) {
    cat->flag.store(false, std::memory_order_relaxed);
    global_state_changed = true;
  }
  if (cat->cb) {
    cat->cb(cat, instance_index, /*created=*/false, global_state_changed,
            cat->cb_user_arg);
  }
}

static void SerializeCategory(
    const PerfettoTeCategoryDescriptor& desc,
    perfetto::protos::pbzero::TrackEventDescriptor* ted) {
  auto* c = ted->add_available_categories();
  c->set_name(desc.name);
  if (desc.desc)
    c->set_description(desc.desc);
  for (size_t j = 0; j < desc.num_tags; ++j) {
    c->add_tags(desc.tags[j]);
  }
}

namespace perfetto {
namespace shlib {

struct TrackEventIncrementalState {
  // A heap-allocated message for storing newly seen interned data while we are
  // in the middle of writing a track event. When a track event wants to write
  // new interned data into the trace, it is first serialized into this message
  // and then flushed to the real trace in EventContext when the packet ends.
  // The message is cached here as a part of incremental state so that we can
  // reuse the underlying buffer allocation for subsequently written interned
  // data.
  uint64_t last_timestamp_ns = 0;
  protozero::HeapBuffered<protos::pbzero::InternedData>
      serialized_interned_data;
  bool was_cleared = true;
  base::FlatSet<uint64_t> seen_track_uuids;
  // Map from serialized representation of a dynamic category to its enabled
  // state.
  base::FlatHashMap<std::string, bool> dynamic_categories;
  InternMap iids;
};

struct TrackEventTlsState {
  template <typename TraceContext>
  explicit TrackEventTlsState(const TraceContext& trace_context) {
    auto locked_ds = trace_context.GetDataSourceLocked();
    bool disable_incremental_timestamps = false;
    timestamp_unit_multiplier = 1;
    if (locked_ds.valid()) {
      const auto& config = locked_ds->GetConfig();
      disable_incremental_timestamps = config.disable_incremental_timestamps();
      if (config.has_timestamp_unit_multiplier() &&
          config.timestamp_unit_multiplier() != 0) {
        timestamp_unit_multiplier = config.timestamp_unit_multiplier();
      }
    }
    if (disable_incremental_timestamps) {
      if (timestamp_unit_multiplier == 1) {
        default_clock_id = PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH;
      } else {
        default_clock_id = PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE;
      }
    } else {
      default_clock_id = PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL;
    }
  }
  uint32_t default_clock_id;
  uint64_t timestamp_unit_multiplier;
};

struct TrackEventDataSourceTraits : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = TrackEventIncrementalState;
  using TlsStateType = TrackEventTlsState;
};

class TrackEvent
    : public perfetto::DataSource<TrackEvent, TrackEventDataSourceTraits> {
 public:
  ~TrackEvent() override;
  void OnSetup(const DataSourceBase::SetupArgs& args) override {
    const std::string& config_raw = args.config->track_event_config_raw();
    bool ok = config_.ParseFromArray(config_raw.data(), config_raw.size());
    if (!ok) {
      PERFETTO_LOG("Failed to parse config");
    }
    inst_id_ = args.internal_instance_index;
  }

  void OnStart(const DataSourceBase::StartArgs&) override {
    GlobalState::Instance().OnStart(config_, inst_id_);
  }

  void OnStop(const DataSourceBase::StopArgs&) override {
    GlobalState::Instance().OnStop(inst_id_);
  }

  const perfetto::protos::gen::TrackEventConfig& GetConfig() const {
    return config_;
  }

  static void Init() {
    DataSourceDescriptor dsd =
        GlobalState::Instance().GenerateDescriptorFromCategories();
    Register(dsd);
  }

  static void RegisterCategory(PerfettoTeCategoryImpl* cat) {
    GlobalState::Instance().RegisterCategory(cat);
  }

  static void UpdateDescriptorFromCategories() {
    DataSourceDescriptor dsd =
        GlobalState::Instance().GenerateDescriptorFromCategories();
    UpdateDescriptor(dsd);
  }

  static void UnregisterCategory(PerfettoTeCategoryImpl* cat) {
    GlobalState::Instance().UnregisterCategory(cat);
  }

  static void CategorySetCallback(struct PerfettoTeCategoryImpl* cat,
                                  PerfettoTeCategoryImplCallback cb,
                                  void* user_arg) {
    GlobalState::Instance().CategorySetCallback(cat, cb, user_arg);
  }

  static internal::DataSourceType* GetType();

  static internal::DataSourceThreadLocalState** GetTlsState();

 private:
  class GlobalState {
   public:
    static GlobalState& Instance() {
      static GlobalState* instance = new GlobalState();
      return *instance;
    }

    void OnStart(const perfetto::protos::gen::TrackEventConfig& config,
                 uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_) {
      std::lock_guard<std::mutex> lock(mu_);
      EnableRegisteredCategory(perfetto_te_any_categories, instance_id);
      for (PerfettoTeCategoryImpl* cat : categories_) {
        if (IsRegisteredCategoryEnabled(*cat, config)) {
          EnableRegisteredCategory(cat, instance_id);
        }
      }
    }

    void OnStop(uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_) {
      std::lock_guard<std::mutex> lock(mu_);
      for (PerfettoTeCategoryImpl* cat : categories_) {
        DisableRegisteredCategory(cat, instance_id);
      }
      DisableRegisteredCategory(perfetto_te_any_categories, instance_id);
    }

    void RegisterCategory(PerfettoTeCategoryImpl* cat)
        PERFETTO_LOCKS_EXCLUDED(mu_) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        Trace([cat](TraceContext ctx) {
          auto ds = ctx.GetDataSourceLocked();

          if (IsRegisteredCategoryEnabled(*cat, ds->GetConfig())) {
            EnableRegisteredCategory(cat, ds->inst_id_);
          }
        });
        categories_.push_back(cat);
        cat->cat_iid = ++interned_categories_;
      }
    }

    void UnregisterCategory(PerfettoTeCategoryImpl* cat)
        PERFETTO_LOCKS_EXCLUDED(mu_) {
      std::lock_guard<std::mutex> lock(mu_);
      categories_.erase(
          std::remove(categories_.begin(), categories_.end(), cat),
          categories_.end());
    }

    void CategorySetCallback(struct PerfettoTeCategoryImpl* cat,
                             PerfettoTeCategoryImplCallback cb,
                             void* user_arg) PERFETTO_LOCKS_EXCLUDED(mu_) {
      std::lock_guard<std::mutex> lock(mu_);
      cat->cb = cb;
      cat->cb_user_arg = user_arg;
      if (!cat->cb) {
        return;
      }

      bool first = true;
      uint8_t active_instances = cat->instances.load(std::memory_order_relaxed);
      for (PerfettoDsInstanceIndex i = 0; i < internal::kMaxDataSourceInstances;
           i++) {
        if ((active_instances & (1 << i)) == 0) {
          continue;
        }
        cb(cat, i, true, first, user_arg);
        first = false;
      }
    }

    DataSourceDescriptor GenerateDescriptorFromCategories() const
        PERFETTO_LOCKS_EXCLUDED(mu_) {
      DataSourceDescriptor dsd;
      dsd.set_name("track_event");

      protozero::HeapBuffered<perfetto::protos::pbzero::TrackEventDescriptor>
          ted;
      {
        std::lock_guard<std::mutex> lock(mu_);
        for (PerfettoTeCategoryImpl* cat : categories_) {
          SerializeCategory(*cat->desc, ted.get());
        }
      }
      dsd.set_track_event_descriptor_raw(ted.SerializeAsString());
      return dsd;
    }

   private:
    GlobalState() : interned_categories_(0) {
      perfetto_te_any_categories = new PerfettoTeCategoryImpl;
      perfetto_te_any_categories_enabled = &perfetto_te_any_categories->flag;
    }

    mutable std::mutex mu_;
    std::vector<PerfettoTeCategoryImpl*> categories_ PERFETTO_GUARDED_BY(mu_);
    uint64_t interned_categories_ PERFETTO_GUARDED_BY(mu_);
  };

  uint32_t inst_id_;
  perfetto::protos::gen::TrackEventConfig config_;
};

TrackEvent::~TrackEvent() = default;

void ResetTrackEventTls() {
  *TrackEvent::GetTlsState() = nullptr;
}

struct TracePointTraits {
  struct TracePointData {
    struct PerfettoTeCategoryImpl* enabled;
  };
  static constexpr std::atomic<uint8_t>* GetActiveInstances(
      TracePointData data) {
    return &data.enabled->instances;
  }
};

}  // namespace shlib
}  // namespace perfetto

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(
    perfetto::shlib::TrackEvent,
    perfetto::shlib::TrackEventDataSourceTraits);

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(
    perfetto::shlib::TrackEvent,
    perfetto::shlib::TrackEventDataSourceTraits);

perfetto::internal::DataSourceType* perfetto::shlib::TrackEvent::GetType() {
  return &perfetto::shlib::TrackEvent::Helper::type();
}

perfetto::internal::DataSourceThreadLocalState**
perfetto::shlib::TrackEvent::GetTlsState() {
  return &tls_state_;
}

namespace {

using perfetto::internal::TrackEventInternal;

perfetto::protos::pbzero::TrackEvent::Type EventType(int32_t type) {
  using Type = perfetto::protos::pbzero::TrackEvent::Type;
  auto enum_type = static_cast<PerfettoTeType>(type);
  switch (enum_type) {
    case PERFETTO_TE_TYPE_SLICE_BEGIN:
      return Type::TYPE_SLICE_BEGIN;
    case PERFETTO_TE_TYPE_SLICE_END:
      return Type::TYPE_SLICE_END;
    case PERFETTO_TE_TYPE_INSTANT:
      return Type::TYPE_INSTANT;
    case PERFETTO_TE_TYPE_COUNTER:
      return Type::TYPE_COUNTER;
  }
  return Type::TYPE_UNSPECIFIED;
}

protozero::MessageHandle<perfetto::protos::pbzero::TracePacket>
NewTracePacketInternal(perfetto::TraceWriterBase* trace_writer,
                       perfetto::shlib::TrackEventIncrementalState* incr_state,
                       const perfetto::shlib::TrackEventTlsState& tls_state,
                       perfetto::TraceTimestamp timestamp,
                       uint32_t seq_flags) {
  // PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL is the default timestamp returned
  // by TrackEventInternal::GetTraceTime(). If the configuration in `tls_state`
  // uses a different clock, we have to use that instead.
  if (PERFETTO_UNLIKELY(tls_state.default_clock_id !=
                            PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL &&
                        timestamp.clock_id ==
                            PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
    timestamp.clock_id = tls_state.default_clock_id;
  }
  auto packet = trace_writer->NewTracePacket();
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  if (PERFETTO_LIKELY(timestamp.clock_id ==
                      PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
    if (PERFETTO_LIKELY(incr_state->last_timestamp_ns <= timestamp.value)) {
      // No need to set the clock id here, since
      // PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL is the clock id assumed by
      // default.
      auto time_diff_ns = timestamp.value - incr_state->last_timestamp_ns;
      auto time_diff_units = time_diff_ns / ts_unit_multiplier;
      packet->set_timestamp(time_diff_units);
      incr_state->last_timestamp_ns += time_diff_units * ts_unit_multiplier;
    } else {
      packet->set_timestamp(timestamp.value / ts_unit_multiplier);
      packet->set_timestamp_clock_id(
          ts_unit_multiplier == 1
              ? static_cast<uint32_t>(PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH)
              : static_cast<uint32_t>(PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE));
    }
  } else if (PERFETTO_LIKELY(timestamp.clock_id ==
                             tls_state.default_clock_id)) {
    packet->set_timestamp(timestamp.value / ts_unit_multiplier);
  } else {
    packet->set_timestamp(timestamp.value);
    packet->set_timestamp_clock_id(timestamp.clock_id);
  }
  packet->set_sequence_flags(seq_flags);
  return packet;
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
std::vector<std::string> GetCmdLine() {
  std::vector<std::string> cmdline_str;
  std::string cmdline;
  if (perfetto::base::ReadFile("/proc/self/cmdline", &cmdline)) {
    perfetto::base::StringSplitter splitter(std::move(cmdline), '\0');
    while (splitter.Next()) {
      cmdline_str.emplace_back(splitter.cur_token(), splitter.cur_token_size());
    }
  }
  return cmdline_str;
}
#endif

void ResetIncrementalStateIfRequired(
    perfetto::TraceWriterBase* trace_writer,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    const perfetto::shlib::TrackEventTlsState& tls_state,
    const perfetto::TraceTimestamp& timestamp) {
  if (!incr_state->was_cleared) {
    return;
  }
  incr_state->was_cleared = false;

  auto sequence_timestamp = timestamp;
  if (timestamp.clock_id != PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH &&
      timestamp.clock_id != PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL) {
    sequence_timestamp = TrackEventInternal::GetTraceTime();
  }

  incr_state->last_timestamp_ns = sequence_timestamp.value;
  auto tid = perfetto::base::GetThreadId();
  auto pid = perfetto::Platform::GetCurrentProcessId();
  uint64_t thread_track_uuid =
      perfetto_te_process_track_uuid ^ static_cast<uint64_t>(tid);
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  {
    // Mark any incremental state before this point invalid. Also set up
    // defaults so that we don't need to repeat constant data for each packet.
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
    auto defaults = packet->set_trace_packet_defaults();
    defaults->set_timestamp_clock_id(tls_state.default_clock_id);
    // Establish the default track for this event sequence.
    auto track_defaults = defaults->set_track_event_defaults();
    track_defaults->set_track_uuid(thread_track_uuid);

    if (tls_state.default_clock_id != PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH) {
      perfetto::protos::pbzero::ClockSnapshot* clocks =
          packet->set_clock_snapshot();
      // Trace clock.
      perfetto::protos::pbzero::ClockSnapshot::Clock* trace_clock =
          clocks->add_clocks();
      trace_clock->set_clock_id(PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH);
      trace_clock->set_timestamp(sequence_timestamp.value);

      if (PERFETTO_LIKELY(tls_state.default_clock_id ==
                          PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
        // Delta-encoded incremental clock in nanoseconds by default but
        // configurable by |tls_state.timestamp_unit_multiplier|.
        perfetto::protos::pbzero::ClockSnapshot::Clock* clock_incremental =
            clocks->add_clocks();
        clock_incremental->set_clock_id(PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL);
        clock_incremental->set_timestamp(sequence_timestamp.value /
                                         ts_unit_multiplier);
        clock_incremental->set_is_incremental(true);
        clock_incremental->set_unit_multiplier_ns(ts_unit_multiplier);
      }
      if (ts_unit_multiplier > 1) {
        // absolute clock with custom timestamp_unit_multiplier.
        perfetto::protos::pbzero::ClockSnapshot::Clock* absolute_clock =
            clocks->add_clocks();
        absolute_clock->set_clock_id(PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE);
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
  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track = packet->set_track_descriptor();
    track->set_uuid(thread_track_uuid);
    track->set_parent_uuid(perfetto_te_process_track_uuid);
    auto* td = track->set_thread();

    td->set_pid(static_cast<int32_t>(pid));
    td->set_tid(static_cast<int32_t>(tid));
    std::string thread_name;
    if (perfetto::base::GetThreadName(thread_name))
      td->set_thread_name(thread_name);
  }
  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track = packet->set_track_descriptor();
    track->set_uuid(perfetto_te_process_track_uuid);
    auto* pd = track->set_process();

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    static perfetto::base::NoDestructor<std::vector<std::string>> cmdline(
        GetCmdLine());
    if (!cmdline.ref().empty()) {
      // Since cmdline is a zero-terminated list of arguments, this ends up
      // writing just the first element, i.e., the process name, into the
      // process name field.
      pd->set_process_name(cmdline.ref()[0]);
      for (const std::string& arg : cmdline.ref()) {
        pd->add_cmdline(arg);
      }
    }
#endif
    pd->set_pid(static_cast<int32_t>(pid));
  }
}

// Appends the fields described by `fields` to `msg`.
void AppendHlProtoFields(protozero::Message* msg,
                         PerfettoTeHlProtoField* const* fields) {
  for (PerfettoTeHlProtoField* const* p = fields; *p != nullptr; p++) {
    switch ((*p)->type) {
      case PERFETTO_TE_HL_PROTO_TYPE_CSTR: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldCstr*>(*p);
        msg->AppendString(field->header.id, field->str);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_BYTES: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldBytes*>(*p);
        msg->AppendBytes(field->header.id, field->buf, field->len);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_NESTED: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldNested*>(*p);
        auto* nested =
            msg->BeginNestedMessage<protozero::Message>(field->header.id);
        AppendHlProtoFields(nested, field->fields);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_VARINT: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldVarInt*>(*p);
        msg->AppendVarInt(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FIXED64: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFixed64*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FIXED32: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFixed32*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_DOUBLE: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldDouble*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FLOAT: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFloat*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
    }
  }
}

void WriteTrackEvent(perfetto::shlib::TrackEventIncrementalState* incr,
                     perfetto::protos::pbzero::TrackEvent* event,
                     PerfettoTeCategoryImpl* cat,
                     perfetto::protos::pbzero::TrackEvent::Type type,
                     const char* name,
                     const PerfettoTeHlExtra* const* extra_data,
                     std::optional<uint64_t> track_uuid,
                     const PerfettoTeCategoryDescriptor* dynamic_cat,
                     bool use_interning) {
  if (type != perfetto::protos::pbzero::TrackEvent::TYPE_UNSPECIFIED) {
    event->set_type(type);
  }

  if (!dynamic_cat &&
      type != perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      type != perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER) {
    uint64_t iid = cat->cat_iid;
    auto res = incr->iids.FindOrAssign(
        perfetto::protos::pbzero::InternedData::kEventCategoriesFieldNumber,
        &iid, sizeof(iid));
    if (res.newly_assigned) {
      auto* ser = incr->serialized_interned_data->add_event_categories();
      ser->set_iid(iid);
      ser->set_name(cat->desc->name);
    }
    event->add_category_iids(iid);
  }

  if (type != perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END) {
    if (name) {
      if (use_interning) {
        const void* str = name;
        size_t len = strlen(name);
        auto res = incr->iids.FindOrAssign(
            perfetto::protos::pbzero::InternedData::kEventNamesFieldNumber, str,
            len);
        if (res.newly_assigned) {
          auto* ser = incr->serialized_interned_data->add_event_names();
          ser->set_iid(res.iid);
          ser->set_name(name);
        }
        event->set_name_iid(res.iid);
      } else {
        event->set_name(name);
      }
    }
  }

  if (dynamic_cat &&
      type != perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      type != perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER) {
    event->add_categories(dynamic_cat->name);
  }

  if (track_uuid) {
    event->set_track_uuid(*track_uuid);
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64 &&
        type == perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER) {
      event->set_counter_value(
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE) {
      event->set_double_counter_value(
          reinterpret_cast<const struct PerfettoTeHlExtraCounterDouble&>(extra)
              .value);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64 ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64 ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER) {
      auto* dbg = event->add_debug_annotations();
      const char* arg_name = nullptr;
      if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgBool&>(
                extra);
        dbg->set_bool_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgUint64&>(
                extra);
        dbg->set_uint_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgInt64&>(
                extra);
        dbg->set_int_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgDouble&>(
                extra);
        dbg->set_double_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgString&>(
                extra);
        dbg->set_string_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgPointer&>(
                extra);
        dbg->set_pointer_value(arg.value);
        arg_name = arg.name;
      }

      if (arg_name != nullptr) {
        const void* str = arg_name;
        size_t len = strlen(arg_name);
        auto res =
            incr->iids.FindOrAssign(perfetto::protos::pbzero::InternedData::
                                        kDebugAnnotationNamesFieldNumber,
                                    str, len);
        if (res.newly_assigned) {
          auto* ser =
              incr->serialized_interned_data->add_debug_annotation_names();
          ser->set_iid(res.iid);
          ser->set_name(arg_name);
        }
        dbg->set_name_iid(res.iid);
      }
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_FLOW) {
      event->add_flow_ids(
          reinterpret_cast<const struct PerfettoTeHlExtraFlow&>(extra).id);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW) {
      event->add_terminating_flow_ids(
          reinterpret_cast<const struct PerfettoTeHlExtraFlow&>(extra).id);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS) {
      const auto* fields =
          reinterpret_cast<const struct PerfettoTeHlExtraProtoFields&>(extra)
              .fields;
      AppendHlProtoFields(event, fields);
    }
  }
}

uint64_t EmitNamedTrack(uint64_t parent_uuid,
                        const char* name,
                        uint64_t id,
                        perfetto::shlib::TrackEventIncrementalState* incr_state,
                        perfetto::TraceWriterBase* trace_writer) {
  uint64_t uuid = parent_uuid;
  uuid ^= PerfettoFnv1a(name, strlen(name));
  uuid ^= id;
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    if (parent_uuid) {
      track_descriptor->set_parent_uuid(parent_uuid);
    }
    track_descriptor->set_name(name);
  }
  return uuid;
}

uint64_t EmitRegisteredTrack(
    const PerfettoTeRegisteredTrackImpl* registered_track,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(registered_track->uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->AppendRawProtoBytes(registered_track->descriptor,
                                          registered_track->descriptor_size);
  }
  return registered_track->uuid;
}

uint64_t EmitProtoTrack(uint64_t uuid,
                        PerfettoTeHlProtoField* const* fields,
                        perfetto::shlib::TrackEventIncrementalState* incr_state,
                        perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    AppendHlProtoFields(track_descriptor, fields);
  }
  return uuid;
}

uint64_t EmitProtoTrackWithParentUuid(
    uint64_t uuid,
    uint64_t parent_uuid,
    PerfettoTeHlProtoField* const* fields,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    track_descriptor->set_parent_uuid(parent_uuid);
    AppendHlProtoFields(track_descriptor, fields);
  }
  return uuid;
}

}  // namespace

struct PerfettoTeCategoryImpl* PerfettoTeCategoryImplCreate(
    struct PerfettoTeCategoryDescriptor* desc) {
  auto* cat = new PerfettoTeCategoryImpl;
  cat->desc = desc;
  perfetto::shlib::TrackEvent::RegisterCategory(cat);
  return cat;
}

void PerfettoTePublishCategories() {
  perfetto::shlib::TrackEvent::UpdateDescriptorFromCategories();
}

void PerfettoTeCategoryImplSetCallback(struct PerfettoTeCategoryImpl* cat,
                                       PerfettoTeCategoryImplCallback cb,
                                       void* user_arg) {
  perfetto::shlib::TrackEvent::CategorySetCallback(cat, cb, user_arg);
}

PERFETTO_ATOMIC(bool) *
    PerfettoTeCategoryImplGetEnabled(struct PerfettoTeCategoryImpl* cat) {
  return &cat->flag;
}

uint64_t PerfettoTeCategoryImplGetIid(struct PerfettoTeCategoryImpl* cat) {
  return cat->cat_iid;
}

void PerfettoTeCategoryImplDestroy(struct PerfettoTeCategoryImpl* cat) {
  perfetto::shlib::TrackEvent::UnregisterCategory(cat);
  delete cat;
}

void PerfettoTeInit(void) {
  perfetto::shlib::TrackEvent::Init();
  perfetto_te_process_track_uuid =
      perfetto::internal::TrackRegistry::ComputeProcessUuid();
}

struct PerfettoTeTimestamp PerfettoTeGetTimestamp(void) {
  struct PerfettoTeTimestamp ret;
  ret.clock_id = PERFETTO_TE_TIMESTAMP_TYPE_BOOT;
  ret.value = TrackEventInternal::GetTimeNs();
  return ret;
}

static bool IsDynamicCategoryEnabled(
    uint32_t inst_idx,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    const struct PerfettoTeCategoryDescriptor& desc) {
  constexpr size_t kMaxCacheSize = 20;
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();
  auto& cache = incr_state->dynamic_categories;
  protozero::HeapBuffered<perfetto::protos::pbzero::TrackEventDescriptor> ted;
  SerializeCategory(desc, ted.get());
  std::string serialized = ted.SerializeAsString();
  auto* cached = cache.Find(serialized);
  if (cached) {
    return *cached;
  }

  auto* internal_state = ds->static_state()->TryGet(inst_idx);
  if (!internal_state)
    return false;
  std::unique_lock<std::recursive_mutex> lock(internal_state->lock);
  auto* sds = static_cast<perfetto::shlib::TrackEvent*>(
      internal_state->data_source.get());

  bool res = IsSingleCategoryEnabled(desc, sds->GetConfig());
  if (cache.size() < kMaxCacheSize) {
    cache[serialized] = res;
  }
  return res;
}

// If the category `dyn_cat` is enabled on the data source instance pointed by
// `ii`, returns immediately. Otherwise, advances `ii` to a data source instance
// where `dyn_cat` is enabled. If there's no data source instance where
// `dyn_cat` is enabled, `ii->instance` will be nullptr.
static void AdvanceToFirstEnabledDynamicCategory(
    perfetto::internal::DataSourceType::InstancesIterator* ii,
    perfetto::internal::DataSourceThreadLocalState* tls_state,
    struct PerfettoTeCategoryImpl* cat,
    const PerfettoTeCategoryDescriptor& dyn_cat) {
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();
  for (; ii->instance;
       ds->NextIteration</*Traits=*/perfetto::shlib::TracePointTraits>(
           ii, tls_state, {cat})) {
    auto* incr_state =
        static_cast<perfetto::shlib::TrackEventIncrementalState*>(
            ds->GetIncrementalState(ii->instance, ii->i));
    if (IsDynamicCategoryEnabled(ii->i, incr_state, dyn_cat)) {
      break;
    }
  }
}

static void InstanceOp(
    perfetto::internal::DataSourceType* ds,
    perfetto::internal::DataSourceType::InstancesIterator* ii,
    perfetto::internal::DataSourceThreadLocalState* tls_state,
    struct PerfettoTeCategoryImpl* cat,
    perfetto::protos::pbzero::TrackEvent::Type type,
    const char* name,
    struct PerfettoTeHlExtra* const* extra_data) {
  if (!ii->instance) {
    return;
  }

  std::variant<std::monostate, const PerfettoTeRegisteredTrackImpl*,
               const PerfettoTeHlExtraNamedTrack*,
               const PerfettoTeHlExtraProtoTrack*,
               const PerfettoTeHlExtraNestedTracks*>
      track;
  std::optional<uint64_t> track_uuid;

  const struct PerfettoTeHlExtraTimestamp* custom_timestamp = nullptr;
  const struct PerfettoTeCategoryDescriptor* dynamic_cat = nullptr;
  std::optional<int64_t> int_counter;
  std::optional<double> double_counter;
  bool use_interning = true;
  bool flush = false;

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK) {
      const auto& cast =
          reinterpret_cast<const struct PerfettoTeHlExtraRegisteredTrack&>(
              extra);
      track = cast.track;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK) {
      track =
          &reinterpret_cast<const struct PerfettoTeHlExtraNamedTrack&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_PROTO_TRACK) {
      track =
          &reinterpret_cast<const struct PerfettoTeHlExtraProtoTrack&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NESTED_TRACKS) {
      auto* nested =
          &reinterpret_cast<const struct PerfettoTeHlExtraNestedTracks&>(extra);
      track = nested;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP) {
      custom_timestamp =
          &reinterpret_cast<const struct PerfettoTeHlExtraTimestamp&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DYNAMIC_CATEGORY) {
      dynamic_cat =
          reinterpret_cast<const struct PerfettoTeHlExtraDynamicCategory&>(
              extra)
              .desc;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64) {
      int_counter =
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE) {
      double_counter =
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NO_INTERN) {
      use_interning = false;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_FLUSH) {
      flush = true;
    }
  }

  perfetto::TraceTimestamp ts;
  if (custom_timestamp) {
    ts.clock_id = custom_timestamp->timestamp.clock_id;
    ts.value = custom_timestamp->timestamp.value;
  } else {
    ts = TrackEventInternal::GetTraceTime();
  }

  if (PERFETTO_UNLIKELY(dynamic_cat)) {
    AdvanceToFirstEnabledDynamicCategory(ii, tls_state, cat, *dynamic_cat);
    if (!ii->instance) {
      return;
    }
  }

  perfetto::TraceWriterBase* trace_writer = ii->instance->trace_writer.get();

  const auto& track_event_tls =
      *static_cast<perfetto::shlib::TrackEventTlsState*>(
          ii->instance->data_source_custom_tls.get());

  auto* incr_state = static_cast<perfetto::shlib::TrackEventIncrementalState*>(
      ds->GetIncrementalState(ii->instance, ii->i));
  ResetIncrementalStateIfRequired(trace_writer, incr_state, track_event_tls,
                                  ts);

  if (std::holds_alternative<const PerfettoTeRegisteredTrackImpl*>(track)) {
    auto* registered_track =
        std::get<const PerfettoTeRegisteredTrackImpl*>(track);
    track_uuid =
        EmitRegisteredTrack(registered_track, incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraNamedTrack*>(
                 track)) {
    auto* named_track = std::get<const PerfettoTeHlExtraNamedTrack*>(track);
    track_uuid = EmitNamedTrack(named_track->parent_uuid, named_track->name,
                                named_track->id, incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraProtoTrack*>(
                 track)) {
    auto* proto_track = std::get<const PerfettoTeHlExtraProtoTrack*>(track);
    track_uuid = EmitProtoTrack(proto_track->uuid, proto_track->fields,
                                incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraNestedTracks*>(
                 track)) {
    auto* nested = std::get<const PerfettoTeHlExtraNestedTracks*>(track);

    uint64_t uuid = 0;

    for (PerfettoTeHlNestedTrack* const* tp = nested->tracks; *tp != nullptr;
         tp++) {
      auto track_type =
          static_cast<enum PerfettoTeHlNestedTrackType>((*tp)->type);

      switch (track_type) {
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_NAMED: {
          auto* named_track =
              reinterpret_cast<PerfettoTeHlNestedTrackNamed*>(*tp);
          uuid = EmitNamedTrack(uuid, named_track->name, named_track->id,
                                incr_state, trace_writer);
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROCESS: {
          uuid = perfetto_te_process_track_uuid;
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_THREAD: {
          uuid = perfetto_te_process_track_uuid ^
                 static_cast<uint64_t>(perfetto::base::GetThreadId());
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROTO: {
          auto* proto_track =
              reinterpret_cast<PerfettoTeHlNestedTrackProto*>(*tp);
          uuid = EmitProtoTrackWithParentUuid(proto_track->id ^ uuid, uuid,
                                              proto_track->fields, incr_state,
                                              trace_writer);
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_REGISTERED: {
          auto* registered_track =
              reinterpret_cast<PerfettoTeHlNestedTrackRegistered*>(*tp);
          uuid = EmitRegisteredTrack(registered_track->track, incr_state,
                                     trace_writer);
        } break;
      }
    }
    track_uuid = uuid;
  }

  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, track_event_tls, ts,
        perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track_event = packet->set_track_event();
    WriteTrackEvent(incr_state, track_event, cat, type, name, extra_data,
                    track_uuid, dynamic_cat, use_interning);
    track_event->Finalize();

    if (!incr_state->serialized_interned_data.empty()) {
      auto ranges = incr_state->serialized_interned_data.GetRanges();
      packet->AppendScatteredBytes(
          perfetto::protos::pbzero::TracePacket::kInternedDataFieldNumber,
          ranges.data(), ranges.size());
      incr_state->serialized_interned_data.Reset();
    }
  }

  if (PERFETTO_UNLIKELY(flush)) {
    trace_writer->Flush();
  }
}

void PerfettoTeHlEmitImpl(struct PerfettoTeCategoryImpl* cat,
                          int32_t type,
                          const char* name,
                          struct PerfettoTeHlExtra* const* extra_data) {
  uint32_t cached_instances =
      perfetto::shlib::TracePointTraits::GetActiveInstances({cat})->load(
          std::memory_order_relaxed);
  if (!cached_instances) {
    return;
  }

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  perfetto::internal::DataSourceThreadLocalState*& tls_state =
      *perfetto::shlib::TrackEvent::GetTlsState();

  if (!ds->TracePrologue<perfetto::shlib::TrackEventDataSourceTraits,
                         perfetto::shlib::TracePointTraits>(
          &tls_state, &cached_instances, {cat})) {
    return;
  }

  for (perfetto::internal::DataSourceType::InstancesIterator ii =
           ds->BeginIteration<perfetto::shlib::TracePointTraits>(
               cached_instances, tls_state, {cat});
       ii.instance;
       ds->NextIteration</*Traits=*/perfetto::shlib::TracePointTraits>(
           &ii, tls_state, {cat})) {
    InstanceOp(ds, &ii, tls_state, cat, EventType(type), name, extra_data);
  }
  ds->TraceEpilogue(tls_state);
}

static void FillIterator(
    const perfetto::internal::DataSourceType::InstancesIterator* ii,
    struct PerfettoTeTimestamp ts,
    struct PerfettoTeLlImplIterator* iterator) {
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  auto& track_event_tls = *static_cast<perfetto::shlib::TrackEventTlsState*>(
      ii->instance->data_source_custom_tls.get());

  auto* incr_state = static_cast<perfetto::shlib::TrackEventIncrementalState*>(
      ds->GetIncrementalState(ii->instance, ii->i));
  perfetto::TraceTimestamp tts;
  tts.clock_id = ts.clock_id;
  tts.value = ts.value;
  ResetIncrementalStateIfRequired(ii->instance->trace_writer.get(), incr_state,
                                  track_event_tls, tts);

  iterator->incr = reinterpret_cast<struct PerfettoTeLlImplIncr*>(incr_state);
  iterator->tls =
      reinterpret_cast<struct PerfettoTeLlImplTls*>(&track_event_tls);
}

struct PerfettoTeLlImplIterator PerfettoTeLlImplBegin(
    struct PerfettoTeCategoryImpl* cat,
    struct PerfettoTeTimestamp ts) {
  struct PerfettoTeLlImplIterator ret = {};
  uint32_t cached_instances =
      perfetto::shlib::TracePointTraits::GetActiveInstances({cat})->load(
          std::memory_order_relaxed);
  if (!cached_instances) {
    return ret;
  }

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  perfetto::internal::DataSourceThreadLocalState*& tls_state =
      *perfetto::shlib::TrackEvent::GetTlsState();

  if (!ds->TracePrologue<perfetto::shlib::TrackEventDataSourceTraits,
                         perfetto::shlib::TracePointTraits>(
          &tls_state, &cached_instances, {cat})) {
    return ret;
  }

  perfetto::internal::DataSourceType::InstancesIterator ii =
      ds->BeginIteration<perfetto::shlib::TracePointTraits>(cached_instances,
                                                            tls_state, {cat});

  ret.ds.inst_id = ii.i;
  tls_state->root_tls->cached_instances = ii.cached_instances;
  ret.ds.tracer = reinterpret_cast<struct PerfettoDsTracerImpl*>(ii.instance);
  if (!ret.ds.tracer) {
    ds->TraceEpilogue(tls_state);
    return ret;
  }

  FillIterator(&ii, ts, &ret);

  ret.ds.tls = reinterpret_cast<struct PerfettoDsTlsImpl*>(tls_state);
  return ret;
}

void PerfettoTeLlImplNext(struct PerfettoTeCategoryImpl* cat,
                          struct PerfettoTeTimestamp ts,
                          struct PerfettoTeLlImplIterator* iterator) {
  auto* tls = reinterpret_cast<perfetto::internal::DataSourceThreadLocalState*>(
      iterator->ds.tls);

  perfetto::internal::DataSourceType::InstancesIterator ii;
  ii.i = iterator->ds.inst_id;
  ii.cached_instances = tls->root_tls->cached_instances;
  ii.instance =
      reinterpret_cast<perfetto::internal::DataSourceInstanceThreadLocalState*>(
          iterator->ds.tracer);

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  ds->NextIteration</*Traits=*/perfetto::shlib::TracePointTraits>(&ii, tls,
                                                                  {cat});

  iterator->ds.inst_id = ii.i;
  tls->root_tls->cached_instances = ii.cached_instances;
  iterator->ds.tracer =
      reinterpret_cast<struct PerfettoDsTracerImpl*>(ii.instance);

  if (!iterator->ds.tracer) {
    ds->TraceEpilogue(tls);
    return;
  }

  FillIterator(&ii, ts, iterator);
}

void PerfettoTeLlImplBreak(struct PerfettoTeCategoryImpl*,
                           struct PerfettoTeLlImplIterator* iterator) {
  auto* tls = reinterpret_cast<perfetto::internal::DataSourceThreadLocalState*>(
      iterator->ds.tls);

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  ds->TraceEpilogue(tls);
}

bool PerfettoTeLlImplDynCatEnabled(
    struct PerfettoDsTracerImpl* tracer,
    PerfettoDsInstanceIndex inst_id,
    const struct PerfettoTeCategoryDescriptor* dyn_cat) {
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  auto* tls_inst =
      reinterpret_cast<perfetto::internal::DataSourceInstanceThreadLocalState*>(
          tracer);

  auto* incr_state = static_cast<perfetto::shlib::TrackEventIncrementalState*>(
      ds->GetIncrementalState(tls_inst, inst_id));

  return IsDynamicCategoryEnabled(inst_id, incr_state, *dyn_cat);
}

bool PerfettoTeLlImplTrackSeen(struct PerfettoTeLlImplIncr* incr,
                               uint64_t uuid) {
  auto* incr_state =
      reinterpret_cast<perfetto::shlib::TrackEventIncrementalState*>(incr);

  return !incr_state->seen_track_uuids.insert(uuid).second;
}

uint64_t PerfettoTeLlImplIntern(struct PerfettoTeLlImplIncr* incr,
                                int32_t type,
                                const void* data,
                                size_t data_size,
                                bool* seen) {
  auto* incr_state =
      reinterpret_cast<perfetto::shlib::TrackEventIncrementalState*>(incr);

  auto res = incr_state->iids.FindOrAssign(type, data, data_size);
  *seen = !res.newly_assigned;
  return res.iid;
}
