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

#include <fcntl.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <list>
#include <mutex>
#include <regex>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

// We also want to test legacy trace events.
#define PERFETTO_ENABLE_LEGACY_TRACE_EVENTS 1

#include "perfetto/tracing.h"
#include "test/gtest_and_gmock.h"
#include "test/integrationtest_initializer.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>  // For CreateFile().
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

// Deliberately not pulling any non-public perfetto header to spot accidental
// header public -> non-public dependency while building this file.

// These two are the only headers allowed here, see comments in
// api_test_support.h.
#include "src/tracing/test/api_test_support.h"
#include "src/tracing/test/tracing_module.h"

#include "perfetto/base/time.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/trace_config.h"

// xxx.pbzero.h includes are for the writing path (the code that pretends to be
// production code).
// yyy.gen.h includes are for the test readback path (the code in the test that
// checks that the results are valid).
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/common/interceptor_descriptor.gen.h"
#include "protos/perfetto/common/trace_stats.gen.h"
#include "protos/perfetto/common/tracing_service_state.gen.h"
#include "protos/perfetto/common/track_event_descriptor.gen.h"
#include "protos/perfetto/common/track_event_descriptor.pbzero.h"
#include "protos/perfetto/config/interceptor_config.gen.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "protos/perfetto/trace/clock_snapshot.gen.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_render_stage_event.gen.h"
#include "protos/perfetto/trace/gpu/gpu_render_stage_event.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.gen.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.gen.h"
#include "protos/perfetto/trace/test_event.gen.h"
#include "protos/perfetto/trace/test_event.pbzero.h"
#include "protos/perfetto/trace/test_extensions.pbzero.h"
#include "protos/perfetto/trace/trace.gen.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.gen.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.gen.h"
#include "protos/perfetto/trace/track_event/chrome_process_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/chrome_process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/counter_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/debug_annotation.gen.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/log_message.gen.h"
#include "protos/perfetto/trace/track_event/log_message.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/source_location.gen.h"
#include "protos/perfetto/trace/track_event/source_location.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/track_event.gen.h"
#include "protos/perfetto/trace/trigger.gen.h"

// Events in categories starting with "dynamic" will use dynamic category
// lookup.
PERFETTO_DEFINE_TEST_CATEGORY_PREFIXES("dynamic");

// Trace categories used in the tests.
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("test")
        .SetDescription("This is a test category")
        .SetTags("tag"),
    perfetto::Category("test.verbose")
        .SetDescription("This is a debug test category")
        .SetTags("tag", "debug"),
    perfetto::Category("foo"),
    perfetto::Category("bar"),
    perfetto::Category("cat").SetTags("slow"),
    perfetto::Category("cat.verbose").SetTags("debug"),
    perfetto::Category("cat-with-dashes"),
    perfetto::Category("slow_category").SetTags("slow"),
    perfetto::Category::Group("foo,bar"),
    perfetto::Category::Group("baz,bar,quux"),
    perfetto::Category::Group("red,green,blue,foo"),
    perfetto::Category::Group("red,green,blue,yellow"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cat")));
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

// Test declaring an extra set of categories in a namespace in addition to the
// default one.
PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE(other_ns,
                                        perfetto::Category("other_ns"));
PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE(other_ns);

// For testing interning of complex objects.
using SourceLocation = std::tuple<const char* /* file_name */,
                                  const char* /* function_name */,
                                  uint32_t /* line_number */>;

template <>
struct std::hash<SourceLocation> {
  size_t operator()(const SourceLocation& value) const {
    auto hasher = hash<size_t>();
    return hasher(reinterpret_cast<size_t>(get<0>(value))) ^
           hasher(reinterpret_cast<size_t>(get<1>(value))) ^
           hasher(get<2>(value));
  }
};

static void WriteFile(const std::string& file_name,
                      const char* content,
                      size_t len) {
  std::ofstream output;
  output.open(file_name.c_str(), std::ios::out | std::ios::binary);
  output.write(content, static_cast<std::streamsize>(len));
  output.close();
}

// Unused in merged code, but very handy for debugging when trace generated in
// a test needs to be exported, to understand it further with other tools.
PERFETTO_UNUSED static void WriteFile(const std::string& file_name,
                                      const std::vector<char>& data) {
  return WriteFile(file_name, data.data(), data.size());
}

// Returns true if the |key| is present in |container|.
template <typename ContainerType, class KeyType>
bool ContainsKey(const ContainerType& container, const KeyType& key) {
  return container.find(key) != container.end();
}

// Represents an opaque (from Perfetto's point of view) thread identifier (e.g.,
// base::PlatformThreadId in Chromium).
struct MyThreadId {
  explicit MyThreadId(int tid_) : tid(tid_) {}

  const int tid = 0;
};

// Represents an opaque timestamp (e.g., base::TimeTicks in Chromium).
class MyTimestamp {
 public:
  explicit MyTimestamp(uint64_t ts_) : ts(ts_) {}

  const uint64_t ts;
};

namespace perfetto {
namespace legacy {

template <>
ThreadTrack ConvertThreadId(const MyThreadId& thread) {
  return perfetto::ThreadTrack::ForThread(
      static_cast<base::PlatformThreadId>(thread.tid));
}

}  // namespace legacy

template <>
struct TraceTimestampTraits<MyTimestamp> {
  static TraceTimestamp ConvertTimestampToTraceTimeNs(
      const MyTimestamp& timestamp) {
    return {static_cast<uint32_t>(TrackEvent::GetTraceClockId()), timestamp.ts};
  }
};

}  // namespace perfetto

namespace {

using perfetto::TracingInitArgs;
using perfetto::internal::TrackEventIncrementalState;
using perfetto::internal::TrackEventInternal;
using ::perfetto::test::DataSourceInternalForTest;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::InvokeWithoutArgs;
using ::testing::IsEmpty;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Property;
using ::testing::StrEq;

// ------------------------------
// Declarations of helper classes
// ------------------------------

class WaitableTestEvent {
 public:
  bool notified() {
    std::unique_lock<std::mutex> lock(mutex_);
    return notified_;
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    // TSAN gets confused by wait_for, which we would use here in a perfect
    // world.
    cv_.wait(lock, [this] { return notified_; });
  }

  void Notify() {
    std::lock_guard<std::mutex> lock(mutex_);
    notified_ = true;
    cv_.notify_one();
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    notified_ = false;
    cv_.notify_one();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool notified_ = false;
};

template <typename Func>
class Cleanup {
 public:
  explicit Cleanup(Func f) : f_(std::move(f)) {}
  ~Cleanup() { f_(); }
  Cleanup(Cleanup&&) noexcept = default;
  Cleanup& operator=(Cleanup&&) noexcept = default;
  Cleanup(const Cleanup&) = delete;
  Cleanup& operator=(const Cleanup&) = delete;

 private:
  Func f_;
};
template <typename Func>
Cleanup<Func> MakeCleanup(Func f) {
  return Cleanup<Func>(std::move(f));
}

class CustomDataSource : public perfetto::DataSource<CustomDataSource> {};

class MockDataSource;

// We can't easily use gmock here because instances of data sources are lazily
// created by the service and are not owned by the test fixture.
struct TestDataSourceHandle {
  WaitableTestEvent on_create;
  WaitableTestEvent on_setup;
  WaitableTestEvent on_start;
  WaitableTestEvent on_stop;
  WaitableTestEvent on_flush;
  MockDataSource* instance;
  perfetto::DataSourceConfig config;
  bool is_datasource_started = false;
  bool handle_stop_asynchronously = false;
  bool handle_flush_asynchronously = false;
  std::function<void()> on_start_callback;
  std::function<void()> on_stop_callback;
  std::function<void(perfetto::FlushFlags)> on_flush_callback;
  std::function<void()> async_stop_closure;
  std::function<void()> async_flush_closure;
};

class MockDataSource : public perfetto::DataSource<MockDataSource> {
 public:
  void OnSetup(const SetupArgs&) override;
  void OnStart(const StartArgs&) override;
  void OnStop(const StopArgs&) override;
  void OnFlush(const FlushArgs&) override;
  TestDataSourceHandle* handle_ = nullptr;
};

constexpr int kTestDataSourceArg = 123;

class MockDataSource2 : public perfetto::DataSource<MockDataSource2> {
 public:
  MockDataSource2(int arg) { EXPECT_EQ(arg, kTestDataSourceArg); }
  void OnSetup(const SetupArgs&) override {}
  void OnStart(const StartArgs&) override {}
  void OnStop(const StopArgs&) override {}
};

// Used to verify that track event data sources in different namespaces register
// themselves correctly in the muxer.
class MockTracingMuxer : public perfetto::internal::TracingMuxer {
 public:
  struct DataSource {
    perfetto::DataSourceDescriptor dsd;
    perfetto::internal::DataSourceStaticState* static_state;
  };

  MockTracingMuxer() : TracingMuxer(nullptr), prev_instance_(instance_) {
    instance_ = this;
  }
  ~MockTracingMuxer() override { instance_ = prev_instance_; }

  bool RegisterDataSource(
      const perfetto::DataSourceDescriptor& dsd,
      DataSourceFactory,
      perfetto::internal::DataSourceParams,
      bool,
      perfetto::internal::DataSourceStaticState* static_state) override {
    data_sources.emplace_back(DataSource{dsd, static_state});
    return true;
  }

  void UpdateDataSourceDescriptor(
      const perfetto::DataSourceDescriptor& dsd,
      const perfetto::internal::DataSourceStaticState* static_state) override {
    for (auto& rds : data_sources) {
      if (rds.static_state == static_state) {
        rds.dsd = dsd;
        return;
      }
    }
  }

  std::unique_ptr<perfetto::TraceWriterBase> CreateTraceWriter(
      perfetto::internal::DataSourceStaticState*,
      uint32_t,
      perfetto::internal::DataSourceState*,
      perfetto::BufferExhaustedPolicy) override {
    return nullptr;
  }

  void DestroyStoppedTraceWritersForCurrentThread() override {}
  void RegisterInterceptor(
      const perfetto::InterceptorDescriptor&,
      InterceptorFactory,
      perfetto::InterceptorBase::TLSFactory,
      perfetto::InterceptorBase::TracePacketCallback) override {}

  void ActivateTriggers(const std::vector<std::string>&, uint32_t) override {}

  std::vector<DataSource> data_sources;

 private:
  TracingMuxer* prev_instance_;
};

struct TestIncrementalState {
  TestIncrementalState() { constructed = true; }
  // Note: a virtual destructor is not required for incremental state.
  ~TestIncrementalState() { destroyed = true; }

  int count = 100;
  bool flag = false;
  static bool constructed;
  static bool destroyed;
};

bool TestIncrementalState::constructed;
bool TestIncrementalState::destroyed;

struct TestIncrementalDataSourceTraits
    : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = TestIncrementalState;
  using CustomTlsState = void;
};

class TestIncrementalDataSource
    : public perfetto::DataSource<TestIncrementalDataSource,
                                  TestIncrementalDataSourceTraits> {
 public:
  void OnSetup(const SetupArgs&) override {}
  void OnStart(const StartArgs&) override {}
  void OnStop(const StopArgs&) override {}
  void WillClearIncrementalState(
      const ClearIncrementalStateArgs& args) override {
    if (will_clear_incremental_state) {
      (*will_clear_incremental_state)(args);
    }
  }

  static void SetWillClearIncrementalStateCallback(
      std::function<void(const DataSourceBase::ClearIncrementalStateArgs&)>
          cb) {
    if (will_clear_incremental_state) {
      delete will_clear_incremental_state;
      will_clear_incremental_state = nullptr;
    }
    if (cb) {
      will_clear_incremental_state = new decltype(cb)(cb);
    }
  }

 private:
  static std::function<void(const ClearIncrementalStateArgs&)>*
      will_clear_incremental_state;
};

std::function<void(const perfetto::DataSourceBase::ClearIncrementalStateArgs&)>*
    TestIncrementalDataSource::will_clear_incremental_state;

// A convenience wrapper around TracingSession that allows to do block on
//
struct TestTracingSessionHandle {
  perfetto::TracingSession* get() { return session.get(); }
  std::unique_ptr<perfetto::TracingSession> session;
  WaitableTestEvent on_stop;
};

class MyDebugAnnotation : public perfetto::DebugAnnotation {
 public:
  ~MyDebugAnnotation() override = default;

  void Add(
      perfetto::protos::pbzero::DebugAnnotation* annotation) const override {
    annotation->set_legacy_json_value(R"({"key": 123})");
  }
};

class TestTracingPolicy : public perfetto::TracingPolicy {
 public:
  void ShouldAllowConsumerSession(
      const ShouldAllowConsumerSessionArgs& args) override {
    EXPECT_NE(args.backend_type, perfetto::BackendType::kUnspecifiedBackend);
    args.result_callback(should_allow_consumer_connection);
  }

  bool should_allow_consumer_connection = true;
};

TestTracingPolicy* g_test_tracing_policy = new TestTracingPolicy();  // Leaked.

class ParsedIncrementalState {
 public:
  void ClearIfNeeded(const perfetto::protos::gen::TracePacket& packet) {
    if (packet.sequence_flags() &
        perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED) {
      incremental_state_was_cleared_ = true;
      categories_.clear();
      event_names_.clear();
      debug_annotation_names_.clear();
      seen_tracks_.clear();
    }
  }

  void Parse(const perfetto::protos::gen::TracePacket& packet) {
    // Update incremental state.
    if (packet.has_interned_data()) {
      const auto& interned_data = packet.interned_data();
      for (const auto& it : interned_data.event_categories()) {
        EXPECT_EQ(categories_.find(it.iid()), categories_.end());
        categories_[it.iid()] = it.name();
      }
      for (const auto& it : interned_data.event_names()) {
        EXPECT_EQ(event_names_.find(it.iid()), event_names_.end());
        event_names_[it.iid()] = it.name();
      }
      for (const auto& it : interned_data.debug_annotation_names()) {
        EXPECT_EQ(debug_annotation_names_.find(it.iid()),
                  debug_annotation_names_.end());
        debug_annotation_names_[it.iid()] = it.name();
      }
    }
  }

  bool HasSeenTrack(uint64_t uuid) const {
    return seen_tracks_.count(uuid) != 0;
  }

  void InsertTrack(uint64_t uuid) { seen_tracks_.insert(uuid); }

  std::string GetCategory(uint64_t iid) { return categories_[iid]; }

  std::string GetEventName(const perfetto::protos::gen::TrackEvent& event) {
    if (event.has_name_iid())
      return event_names_[event.name_iid()];
    return event.name();
  }

  std::string GetDebugAnnotationName(uint64_t iid) {
    return debug_annotation_names_[iid];
  }

  bool WasCleared() const { return incremental_state_was_cleared_; }

 private:
  bool incremental_state_was_cleared_ = false;
  std::map<uint64_t, std::string> categories_;
  std::map<uint64_t, std::string> event_names_;
  std::map<uint64_t, std::string> debug_annotation_names_;
  std::set<uint64_t> seen_tracks_;
};

std::vector<std::string> ReadSlicesFromTrace(
    const perfetto::protos::gen::Trace& parsed_trace,
    bool expect_incremental_state_cleared = true) {
  // Read back the trace, maintaining interning tables as we go.
  std::vector<std::string> slices;
  if (parsed_trace.packet().size() == 0)
    return slices;
  ParsedIncrementalState incremental_state;

  uint32_t sequence_id = 0;
  for (const auto& packet : parsed_trace.packet()) {
    incremental_state.ClearIfNeeded(packet);

    if (packet.has_track_descriptor()) {
      // Make sure we haven't seen any events on this track before the
      // descriptor was written.
      EXPECT_FALSE(
          incremental_state.HasSeenTrack(packet.track_descriptor().uuid()));
    }

    if (!packet.has_track_event())
      continue;

    // Make sure we only see track events on one sequence.
    if (packet.trusted_packet_sequence_id()) {
      if (!sequence_id)
        sequence_id = packet.trusted_packet_sequence_id();
      EXPECT_EQ(sequence_id, packet.trusted_packet_sequence_id());
    }

    incremental_state.Parse(packet);

    const auto& track_event = packet.track_event();
    std::string slice;

    if (track_event.has_track_uuid()) {
      incremental_state.InsertTrack(track_event.track_uuid());
      std::stringstream track;
      track << "[track=" << track_event.track_uuid() << "]";
      slice += track.str();
    }

    switch (track_event.type()) {
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN:
        slice += "B";
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_END:
        slice += "E";
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_INSTANT:
        slice += "I";
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_UNSPECIFIED: {
        EXPECT_TRUE(track_event.has_legacy_event());
        EXPECT_FALSE(track_event.type());
        auto legacy_event = track_event.legacy_event();
        slice +=
            "Legacy_" + std::string(1, static_cast<char>(legacy_event.phase()));
        break;
      }
      case perfetto::protos::gen::TrackEvent::TYPE_COUNTER:
        slice += "C";
        break;
      default:
        ADD_FAILURE();
    }
    if (track_event.has_legacy_event()) {
      auto legacy_event = track_event.legacy_event();
      std::stringstream id;
      if (legacy_event.has_unscoped_id()) {
        id << "(unscoped_id=" << legacy_event.unscoped_id() << ")";
      } else if (legacy_event.has_local_id()) {
        id << "(local_id=" << legacy_event.local_id() << ")";
      } else if (legacy_event.has_global_id()) {
        id << "(global_id=" << legacy_event.global_id() << ")";
      } else if (legacy_event.has_bind_id()) {
        id << "(bind_id=" << legacy_event.bind_id() << ")";
      }
      if (legacy_event.has_id_scope())
        id << "(id_scope=\"" << legacy_event.id_scope() << "\")";
      if (legacy_event.use_async_tts())
        id << "(use_async_tts)";
      if (legacy_event.bind_to_enclosing())
        id << "(bind_to_enclosing)";
      if (legacy_event.has_flow_direction())
        id << "(flow_direction=" << legacy_event.flow_direction() << ")";
      if (legacy_event.has_pid_override())
        id << "(pid_override=" << legacy_event.pid_override() << ")";
      if (legacy_event.has_tid_override())
        id << "(tid_override=" << legacy_event.tid_override() << ")";
      slice += id.str();
    }
    size_t category_count = 0;
    for (const auto& it : track_event.category_iids())
      slice +=
          (category_count++ ? "," : ":") + incremental_state.GetCategory(it);
    for (const auto& it : track_event.categories())
      slice += (category_count++ ? ",$" : ":$") + it;
    if (track_event.has_name() || track_event.has_name_iid())
      slice += "." + incremental_state.GetEventName(track_event);

    if (track_event.debug_annotations_size()) {
      slice += "(";
      bool first_annotation = true;
      for (const auto& it : track_event.debug_annotations()) {
        if (!first_annotation) {
          slice += ",";
        }
        if (it.has_name_iid()) {
          slice += incremental_state.GetDebugAnnotationName(it.name_iid());
        } else {
          slice += it.name();
        }
        slice += "=";
        std::stringstream value;
        if (it.has_bool_value()) {
          value << "(bool)" << it.bool_value();
        } else if (it.has_uint_value()) {
          value << "(uint)" << it.uint_value();
        } else if (it.has_int_value()) {
          value << "(int)" << it.int_value();
        } else if (it.has_double_value()) {
          value << "(double)" << it.double_value();
        } else if (it.has_string_value()) {
          value << "(string)" << it.string_value();
        } else if (it.has_pointer_value()) {
          value << "(pointer)" << std::hex << it.pointer_value();
        } else if (it.has_legacy_json_value()) {
          value << "(json)" << it.legacy_json_value();
        } else if (it.has_nested_value()) {
          value << "(nested)" << it.nested_value().string_value();
        }
        slice += value.str();
        first_annotation = false;
      }
      slice += ")";
    }

    if (track_event.flow_ids_old_size()) {
      slice += "(flow_ids_old=";
      std::stringstream value;
      bool first_annotation = true;
      for (uint64_t id : track_event.flow_ids_old()) {
        if (!first_annotation) {
          value << ",";
        }
        first_annotation = false;
        value << id;
      }
      slice += value.str() + ")";
    }

    if (track_event.flow_ids_size()) {
      slice += "(flow_ids=";
      std::stringstream value;
      bool first_annotation = true;
      for (uint64_t id : track_event.flow_ids()) {
        if (!first_annotation) {
          value << ",";
        }
        first_annotation = false;
        value << id;
      }
      slice += value.str() + ")";
    }

    if (track_event.terminating_flow_ids_old_size()) {
      slice += "(terminating_flow_ids_old=";
      std::stringstream value;
      bool first_annotation = true;
      for (uint64_t id : track_event.terminating_flow_ids_old()) {
        if (!first_annotation) {
          value << ",";
        }
        value << id;
        first_annotation = false;
      }
      slice += value.str() + ")";
    }

    if (track_event.terminating_flow_ids_size()) {
      slice += "(terminating_flow_ids=";
      std::stringstream value;
      bool first_annotation = true;
      for (uint64_t id : track_event.terminating_flow_ids()) {
        if (!first_annotation) {
          value << ",";
        }
        value << id;
        first_annotation = false;
      }
      slice += value.str() + ")";
    }

    slices.push_back(slice);
  }
  if (expect_incremental_state_cleared) {
    EXPECT_TRUE(incremental_state.WasCleared());
  }
  return slices;
}

std::vector<std::string> ReadSlicesFromTrace(
    const std::vector<char>& raw_trace,
    bool expect_incremental_state_cleared = true) {
  EXPECT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace parsed_trace;
  EXPECT_TRUE(parsed_trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  return ReadSlicesFromTrace(parsed_trace, expect_incremental_state_cleared);
}

bool WaitForOneProducerConnected(perfetto::TracingSession* session) {
  for (size_t i = 0; i < 100; i++) {
    // Blocking read.
    auto result = session->QueryServiceStateBlocking();
    perfetto::protos::gen::TracingServiceState state;
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                     result.service_state_data.size()));
    // The producer has connected to the new restarted system service.
    if (state.producers().size() == 1) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ADD_FAILURE() << "Producer not connected";
  return false;
}

// -------------------------
// Declaration of test class
// -------------------------
class PerfettoApiTest : public ::testing::TestWithParam<perfetto::BackendType> {
 public:
  static PerfettoApiTest* instance;

  void SetUp() override {
    instance = this;
    g_test_tracing_policy->should_allow_consumer_connection = true;

    // Start a fresh system service for this test, tearing down any previous
    // service that was running.
    if (GetParam() == perfetto::kSystemBackend) {
      system_service_ = perfetto::test::SystemService::Start();
      // If the system backend isn't supported, skip all system backend tests.
      if (!system_service_.valid()) {
        GTEST_SKIP();
      }
    }

    EXPECT_FALSE(perfetto::Tracing::IsInitialized());
    TracingInitArgs args;
    args.backends = GetParam();
    args.tracing_policy = g_test_tracing_policy;
    perfetto::Tracing::Initialize(args);
    RegisterDataSource<MockDataSource>("my_data_source");
    {
      perfetto::DataSourceDescriptor dsd;
      dsd.set_name("CustomDataSource");
      CustomDataSource::Register(dsd);
    }
    perfetto::TrackEvent::Register();

    // Make sure our data source always has a valid handle.
    data_sources_["my_data_source"];

    // If this wasn't the first test to run in this process, any producers
    // connected to the old system service will have been disconnected by the
    // service restarting above. Wait for all producers to connect again before
    // proceeding with the test.
    perfetto::test::SyncProducers();

    perfetto::test::DisableReconnectLimit();
  }

  void TearDown() override {
    instance = nullptr;
    sessions_.clear();
    perfetto::test::TracingMuxerImplInternalsForTest::
        ClearDataSourceTlsStateOnReset<MockDataSource>();
    perfetto::test::TracingMuxerImplInternalsForTest::
        ClearDataSourceTlsStateOnReset<CustomDataSource>();
    perfetto::test::TracingMuxerImplInternalsForTest::
        ClearDataSourceTlsStateOnReset<
            perfetto::internal::TrackEventDataSource>();
    perfetto::Tracing::ResetForTesting();
  }

  template <typename DerivedDataSource>
  TestDataSourceHandle* RegisterDataSource(std::string name) {
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name(name);
    return RegisterDataSource<DerivedDataSource>(dsd);
  }

  template <typename DerivedDataSource>
  TestDataSourceHandle* RegisterDataSource(
      const perfetto::DataSourceDescriptor& dsd) {
    EXPECT_EQ(data_sources_.count(dsd.name()), 0u);
    TestDataSourceHandle* handle = &data_sources_[dsd.name()];
    DerivedDataSource::Register(dsd);
    return handle;
  }

  template <typename DerivedDataSource>
  TestDataSourceHandle* UpdateDataSource(
      const perfetto::DataSourceDescriptor& dsd) {
    EXPECT_EQ(data_sources_.count(dsd.name()), 1u);
    TestDataSourceHandle* handle = &data_sources_[dsd.name()];
    DerivedDataSource::UpdateDescriptor(dsd);
    return handle;
  }

  TestTracingSessionHandle* NewTrace(const perfetto::TraceConfig& cfg,
                                     int fd = -1) {
    return NewTrace(cfg, /*backend_type=*/GetParam(), fd);
  }

  TestTracingSessionHandle* NewTrace(const perfetto::TraceConfig& cfg,
                                     perfetto::BackendType backend_type,
                                     int fd = -1) {
    sessions_.emplace_back();
    TestTracingSessionHandle* handle = &sessions_.back();
    handle->session = perfetto::Tracing::NewTrace(backend_type);
    handle->session->SetOnStopCallback([handle] { handle->on_stop.Notify(); });
    handle->session->Setup(cfg, fd);
    return handle;
  }

  TestTracingSessionHandle* NewTraceWithCategories(
      std::vector<std::string> categories,
      perfetto::protos::gen::TrackEventConfig te_cfg = {},
      perfetto::TraceConfig cfg = {}) {
    cfg.set_duration_ms(500);
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");
    te_cfg.add_disabled_categories("*");
    for (const auto& category : categories)
      te_cfg.add_enabled_categories(category);
    ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

    return NewTrace(cfg);
  }

  std::vector<std::string> ReadLogMessagesFromTrace(
      perfetto::TracingSession* tracing_session) {
    std::vector<char> raw_trace = tracing_session->ReadTraceBlocking();
    EXPECT_GE(raw_trace.size(), 0u);

    // Read back the trace, maintaining interning tables as we go.
    std::vector<std::string> log_messages;
    std::map<uint64_t, std::string> log_message_bodies;
    std::map<uint64_t, perfetto::protos::gen::SourceLocation> source_locations;
    perfetto::protos::gen::Trace parsed_trace;
    EXPECT_TRUE(
        parsed_trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

    for (const auto& packet : parsed_trace.packet()) {
      if (!packet.has_track_event())
        continue;

      if (packet.has_interned_data()) {
        const auto& interned_data = packet.interned_data();
        for (const auto& it : interned_data.log_message_body()) {
          EXPECT_GE(it.iid(), 1u);
          EXPECT_EQ(log_message_bodies.find(it.iid()),
                    log_message_bodies.end());
          log_message_bodies[it.iid()] = it.body();
        }
        for (const auto& it : interned_data.source_locations()) {
          EXPECT_GE(it.iid(), 1u);
          EXPECT_EQ(source_locations.find(it.iid()), source_locations.end());
          source_locations[it.iid()] = it;
        }
      }
      const auto& track_event = packet.track_event();
      if (track_event.type() !=
          perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN)
        continue;

      EXPECT_TRUE(track_event.has_log_message());
      const auto& log = track_event.log_message();
      if (log.source_location_iid()) {
        std::stringstream msg;
        const auto& source_location =
            source_locations[log.source_location_iid()];
        msg << source_location.function_name() << "("
            << source_location.file_name() << ":"
            << source_location.line_number()
            << "): " << log_message_bodies[log.body_iid()];
        log_messages.emplace_back(msg.str());
      } else {
        log_messages.emplace_back(log_message_bodies[log.body_iid()]);
      }
    }
    return log_messages;
  }

  std::vector<std::string> ReadSlicesFromTraceSession(
      perfetto::TracingSession* tracing_session) {
    return ReadSlicesFromTrace(tracing_session->ReadTraceBlocking());
  }

  std::vector<std::string> StopSessionAndReadSlicesFromTrace(
      TestTracingSessionHandle* tracing_session) {
    return ReadSlicesFromTrace(StopSessionAndReturnBytes(tracing_session));
  }

  uint32_t GetMainThreadPacketSequenceId(
      const perfetto::protos::gen::Trace& trace) {
    for (const auto& packet : trace.packet()) {
      if (packet.has_track_descriptor() &&
          packet.track_descriptor().thread().tid() ==
              static_cast<int32_t>(perfetto::base::GetThreadId())) {
        return packet.trusted_packet_sequence_id();
      }
    }
    ADD_FAILURE() << "Main thread not found";
    return 0;
  }

  static std::vector<char> StopSessionAndReturnBytes(
      TestTracingSessionHandle* tracing_session) {
    perfetto::TrackEvent::Flush();
    tracing_session->get()->StopBlocking();
    return tracing_session->get()->ReadTraceBlocking();
  }

  static perfetto::protos::gen::Trace StopSessionAndReturnParsedTrace(
      TestTracingSessionHandle* tracing_session) {
    std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
    perfetto::protos::gen::Trace trace;
    if (trace.ParseFromArray(raw_trace.data(), raw_trace.size())) {
      return trace;
    } else {
      ADD_FAILURE() << "trace.ParseFromArray failed";
      return perfetto::protos::gen::Trace();
    }
  }

  perfetto::test::SystemService system_service_;
  std::map<std::string, TestDataSourceHandle> data_sources_;
  std::list<TestTracingSessionHandle> sessions_;  // Needs stable pointers.
};

// ---------------------------------------------
// Definitions for non-inlineable helper methods
// ---------------------------------------------
PerfettoApiTest* PerfettoApiTest::instance;

void MockDataSource::OnSetup(const SetupArgs& args) {
  EXPECT_EQ(handle_, nullptr);
  auto it = PerfettoApiTest::instance->data_sources_.find(args.config->name());

  // We should not see an OnSetup for a data source that we didn't register
  // before via PerfettoApiTest::RegisterDataSource().
  EXPECT_NE(it, PerfettoApiTest::instance->data_sources_.end());
  handle_ = &it->second;
  handle_->config = *args.config;  // Deliberate copy.
  handle_->on_setup.Notify();
}

void MockDataSource::OnStart(const StartArgs&) {
  EXPECT_NE(handle_, nullptr);
  EXPECT_FALSE(handle_->is_datasource_started);
  handle_->is_datasource_started = true;
  if (handle_->on_start_callback)
    handle_->on_start_callback();
  handle_->on_start.Notify();
}

void MockDataSource::OnStop(const StopArgs& args) {
  EXPECT_NE(handle_, nullptr);
  EXPECT_TRUE(handle_->is_datasource_started);
  handle_->is_datasource_started = false;
  if (handle_->handle_stop_asynchronously)
    handle_->async_stop_closure = args.HandleStopAsynchronously();
  if (handle_->on_stop_callback)
    handle_->on_stop_callback();
  handle_->on_stop.Notify();
}

void MockDataSource::OnFlush(const FlushArgs& args) {
  EXPECT_NE(handle_, nullptr);
  EXPECT_TRUE(handle_->is_datasource_started);
  if (handle_->handle_flush_asynchronously)
    handle_->async_flush_closure = args.HandleFlushAsynchronously();
  if (handle_->on_flush_callback) {
    handle_->on_flush_callback(args.flush_flags);
  }
  handle_->on_flush.Notify();
}

// -------------
// Test fixtures
// -------------

TEST_P(PerfettoApiTest, StartAndStopWithoutDataSources) {
  // Create a new trace session without any data sources configured.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* tracing_session = NewTrace(cfg);
  // This should not timeout.
  tracing_session->get()->StartBlocking();
  tracing_session->get()->StopBlocking();
}

// Disabled by default because it leaks tracing sessions into subsequent tests,
// which can result in the per-uid tracing session limit (5) to be hit in later
// tests.
// TODO(b/261493947): fix or remove.
TEST_P(PerfettoApiTest, DISABLED_TrackEventStartStopAndDestroy) {
  // This test used to cause a use after free as the tracing session got
  // destroyed. It needed to be run approximately 2000 times to catch it so test
  // with --gtest_repeat=3000 (less if running under GDB).

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Create five new trace sessions.
  std::vector<std::unique_ptr<perfetto::TracingSession>> sessions;
  for (size_t i = 0; i < 5; ++i) {
    sessions.push_back(perfetto::Tracing::NewTrace(/*BackendType=*/GetParam()));
    sessions[i]->Setup(cfg);
    sessions[i]->Start();
    sessions[i]->Stop();
  }
}

TEST_P(PerfettoApiTest, TrackEventStartStopAndStopBlocking) {
  // This test used to cause a deadlock (due to StopBlocking() after the session
  // already stopped). This usually occurred within 1 or 2 runs of the test so
  // use --gtest_repeat=10

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Create five new trace sessions.
  std::vector<std::unique_ptr<perfetto::TracingSession>> sessions;
  for (size_t i = 0; i < 5; ++i) {
    sessions.push_back(perfetto::Tracing::NewTrace(/*BackendType=*/GetParam()));
    sessions[i]->Setup(cfg);
    sessions[i]->Start();
    sessions[i]->Stop();
  }
  for (auto& session : sessions) {
    session->StopBlocking();
  }
}

TEST_P(PerfettoApiTest, ChangeTraceConfiguration) {
  // Setup the trace config.
  perfetto::TraceConfig trace_config;
  trace_config.set_duration_ms(2000);
  trace_config.add_buffers()->set_size_kb(1024);
  auto* data_source = trace_config.add_data_sources();

  // Configure track events with category "foo".
  auto* ds_cfg = data_source->mutable_config();
  ds_cfg->set_name("track_event");
  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_disabled_categories("*");
  te_cfg.add_enabled_categories("foo");
  ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

  // Initially, exclude all producers (the client library's producer is named
  // after current process's name, which will not match
  // "all_producers_excluded").
  data_source->add_producer_name_filter("all_producers_excluded");

  auto* tracing_session = NewTrace(trace_config);

  tracing_session->get()->StartBlocking();

  // Emit a first trace event, this one should be filtered out due
  // to the mismatching producer name filter.
  TRACE_EVENT_BEGIN("foo", "EventFilteredOut");
  TRACE_EVENT_END("foo");

  // Remove the producer name filter by changing configs.
  data_source->clear_producer_name_filter();
  tracing_session->get()->ChangeTraceConfig(trace_config);

  // We don't have a blocking version of ChangeTraceConfig, because there is
  // currently no response to it from producers or the service. Instead, we sync
  // the consumer and producer IPC streams for this test, to ensure that the
  // producer_name_filter change has propagated.
  tracing_session->get()->GetTraceStatsBlocking();  // sync consumer stream.
  perfetto::test::SyncProducers();                  // sync producer stream.

  // Emit a second trace event, this one should be included because
  // the producer name filter was cleared.
  TRACE_EVENT_BEGIN("foo", "EventIncluded");
  TRACE_EVENT_END("foo");

  // Verify that only the second event is in the trace data.
  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::string trace(raw_trace.data(), raw_trace.size());
  EXPECT_THAT(trace, Not(HasSubstr("EventFilteredOut")));
  EXPECT_THAT(trace, HasSubstr("EventIncluded"));
}

// This is a build-only regression test that checks you can have a track event
// inside a template.
template <typename T>
void TestTrackEventInsideTemplate(T) {
  TRACE_EVENT_BEGIN("cat", "Name");
}

// This is a build-only regression test that checks you can specify the tracing
// category as a template argument.
constexpr const char kTestCategory[] = "foo";
template <const char* category>
void TestCategoryAsTemplateParameter() {
  TRACE_EVENT_BEGIN(category, "Name");
}

// Sleep for |nano_seconds| in a way that this duration is counted in
// thread_time. i.e. sleep without using OS's sleep method, which blocks the
// thread and OS doesn't schedule it until expected wake-up-time.
void SpinForThreadTimeNanos(int64_t nano_seconds) {
  auto time_now = perfetto::base::GetThreadCPUTimeNs().count();
  auto goal_time = time_now + nano_seconds;
  while (perfetto::base::GetThreadCPUTimeNs().count() < goal_time) {
  }
}

TEST_P(PerfettoApiTest, TrackEventTimestampUnitAbsolute) {
  for (auto unit_multiplier : {1u, 1000u}) {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_disable_incremental_timestamps(true);
    te_cfg.set_timestamp_unit_multiplier(unit_multiplier);
    auto* tracing_session = NewTraceWithCategories({"foo"}, te_cfg);
    tracing_session->get()->StartBlocking();
    int64_t t_before = static_cast<int64_t>(TrackEventInternal::GetTimeNs());
    TRACE_EVENT_BEGIN("foo", "Event1");
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event2");
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event3");
    int64_t t_after = static_cast<int64_t>(TrackEventInternal::GetTimeNs());
    auto trace = StopSessionAndReturnParsedTrace(tracing_session);
    std::unordered_map<std::string, int64_t> event_map;
    bool found_absolute_clock = false;
    for (const auto& packet : trace.packet()) {
      if (packet.has_interned_data()) {
        if (packet.interned_data().event_names().size() == 1) {
          auto& event_name = packet.interned_data().event_names()[0].name();
          event_map[event_name] = static_cast<int64_t>(packet.timestamp());
        }
      }
      if (packet.has_trace_packet_defaults()) {
        auto clock_id = packet.trace_packet_defaults().timestamp_clock_id();
        EXPECT_EQ(unit_multiplier == 1
                      ? static_cast<uint32_t>(TrackEventInternal::GetClockId())
                      : TrackEventIncrementalState::kClockIdAbsolute,
                  clock_id);
        if (packet.has_clock_snapshot()) {
          for (auto& clock : packet.clock_snapshot().clocks()) {
            if (clock.clock_id() ==
                TrackEventIncrementalState::kClockIdAbsolute) {
              found_absolute_clock = true;
              EXPECT_EQ(unit_multiplier, clock.unit_multiplier_ns());
              EXPECT_FALSE(clock.is_incremental());
            }
          }
        }
      }
    }

    EXPECT_EQ((unit_multiplier == 1000), found_absolute_clock);

    auto e1_t = event_map.at("Event1");
    auto e2_t = event_map.at("Event2");
    auto e3_t = event_map.at("Event3");

    int64_t min_delta = 1000000 / unit_multiplier;
    int64_t max_delta = (t_after - t_before) / unit_multiplier;

    EXPECT_LE(t_before / unit_multiplier, e1_t);
    EXPECT_LE(e3_t, t_after / unit_multiplier);

    EXPECT_GE(e2_t - e1_t, min_delta);
    EXPECT_GE(e3_t - e2_t, min_delta);

    EXPECT_LE(e2_t - e1_t, max_delta);
    EXPECT_LE(e3_t - e2_t, max_delta);
  }
}

TEST_P(PerfettoApiTest, TrackEventTimestampUnitIncremental) {
  for (auto unit_multiplier : {1u, 1000u}) {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_enable_thread_time_sampling(true);
    te_cfg.set_timestamp_unit_multiplier(unit_multiplier);
    auto* tracing_session = NewTraceWithCategories({"foo"}, te_cfg);
    tracing_session->get()->StartBlocking();
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event1");
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event2");
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event3");
    auto trace = StopSessionAndReturnParsedTrace(tracing_session);
    struct TimeInfo {
      int64_t timestamp;
      int64_t thread_time;
    };
    std::unordered_map<std::string, TimeInfo> event_map;
    for (const auto& packet : trace.packet()) {
      if (packet.has_interned_data()) {
        if (packet.interned_data().event_names().size() == 1) {
          auto& event_name = packet.interned_data().event_names()[0].name();
          if (packet.has_track_event() &&
              packet.track_event().extra_counter_values().size() > 0) {
            auto thread_time = packet.track_event().extra_counter_values()[0];
            event_map[event_name] = {static_cast<int64_t>(packet.timestamp()),
                                     thread_time};
          }
        }
      }
    }
    int min_delta = 1000 * (unit_multiplier == 1 ? 1000 : 1);

    EXPECT_EQ(0, event_map.at("Event1").timestamp);
    EXPECT_GE(event_map.at("Event2").timestamp, min_delta);
    EXPECT_GE(event_map.at("Event3").timestamp, min_delta);

    EXPECT_GE(event_map.at("Event2").thread_time, min_delta);
    EXPECT_GE(event_map.at("Event3").thread_time, min_delta);
  }
}

TEST_P(PerfettoApiTest, TrackEventThreadTimeSubsampling) {
  for (auto subsampling : {0u, 1000u, 1000000u}) {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_enable_thread_time_sampling(true);
    te_cfg.set_thread_time_subsampling_ns(subsampling);
    auto* tracing_session = NewTraceWithCategories({"foo"}, te_cfg);
    tracing_session->get()->StartBlocking();
    SpinForThreadTimeNanos(1000000);
    TRACE_EVENT_BEGIN("foo", "Event1");
    SpinForThreadTimeNanos(10);
    TRACE_EVENT_BEGIN("foo", "Event2");
    SpinForThreadTimeNanos(10 * 1000 * 1000);
    TRACE_EVENT_BEGIN("foo", "Event3");
    auto trace = StopSessionAndReturnParsedTrace(tracing_session);
    struct TimeInfo {
      int64_t timestamp;
      int64_t thread_time;
    };
    std::unordered_map<std::string, TimeInfo> event_map;
    for (const auto& packet : trace.packet()) {
      if (packet.has_interned_data()) {
        if (packet.interned_data().event_names().size() == 1) {
          auto& event_name = packet.interned_data().event_names()[0].name();
          if (packet.has_track_event() &&
              packet.track_event().extra_counter_values().size() > 0) {
            auto thread_time = packet.track_event().extra_counter_values()[0];
            event_map[event_name] = {static_cast<int64_t>(packet.timestamp()),
                                     thread_time};
          }
        }
      }
    }

    EXPECT_EQ(0, event_map.at("Event1").timestamp);
    EXPECT_GT(event_map.at("Event2").timestamp, 10);
    EXPECT_GT(event_map.at("Event3").timestamp, 1000 * 1000 * 10);

    if (event_map.at("Event2").timestamp < subsampling) {
      EXPECT_EQ(event_map.at("Event2").thread_time, 0);
    } else {
      EXPECT_GT(event_map.at("Event2").thread_time, 10);
    }
    EXPECT_GT(event_map.at("Event3").thread_time, 1000 * 1000 * 10);
  }
}

// Tests that we don't accumulate error when using incremental timestamps with
// timestamp unit multiplier.
TEST_P(PerfettoApiTest, TrackEventTimestampIncrementalAccumulatedError) {
  constexpr uint64_t kUnitMultiplier = 100000;
  constexpr uint64_t kNumberOfEvents = 1000;
  constexpr uint64_t kTimeBetweenEventsNs = 50000;

  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.set_timestamp_unit_multiplier(kUnitMultiplier);
  auto* tracing_session = NewTraceWithCategories({"foo"}, te_cfg);
  tracing_session->get()->StartBlocking();
  auto start = perfetto::TrackEvent::GetTraceTimeNs();
  TRACE_EVENT_BEGIN("foo", "Start");
  for (uint64_t i = 0; i < kNumberOfEvents; ++i) {
    SpinForThreadTimeNanos(kTimeBetweenEventsNs);
    TRACE_EVENT_BEGIN("foo", "Event");
  }
  auto end = perfetto::TrackEvent::GetTraceTimeNs();
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  uint64_t accumulated_timestamp = 0;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_event()) {
      accumulated_timestamp += packet.timestamp() * kUnitMultiplier;
    }
  }

  EXPECT_GE(accumulated_timestamp, kNumberOfEvents * kTimeBetweenEventsNs);
  EXPECT_LE(accumulated_timestamp, end - start);
}

TEST_P(PerfettoApiTest, TrackEvent) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  // Emit one complete track event.
  TRACE_EVENT_BEGIN("test", "TestEvent");
  TRACE_EVENT_END("test");
  perfetto::TrackEvent::Flush();

  tracing_session->on_stop.Wait();
  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  // Read back the trace, maintaining interning tables as we go.
  perfetto::protos::gen::Trace trace;
  std::map<uint64_t, std::string> categories;
  std::map<uint64_t, std::string> event_names;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

  auto now = perfetto::TrackEvent::GetTraceTimeNs();
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  auto clock_id = perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
#else
  auto clock_id = perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC;
#endif
  EXPECT_EQ(clock_id, perfetto::TrackEvent::GetTraceClockId());

  bool incremental_state_was_cleared = false;
  bool begin_found = false;
  bool end_found = false;
  bool process_descriptor_found = false;
  uint32_t sequence_id = 0;
  int32_t cur_pid = perfetto::test::GetCurrentProcessId();
  uint64_t recent_absolute_time_ns = 0;
  bool found_incremental_clock = false;
  constexpr auto kClockIdIncremental =
      TrackEventIncrementalState::kClockIdIncremental;

  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor()) {
      const auto& desc = packet.track_descriptor();
      if (desc.has_process()) {
        EXPECT_FALSE(process_descriptor_found);
        const auto& pd = desc.process();
        EXPECT_EQ(cur_pid, pd.pid());
        process_descriptor_found = true;
      }
    }
    if (packet.sequence_flags() &
        perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED) {
      EXPECT_TRUE(packet.has_trace_packet_defaults());
      incremental_state_was_cleared = true;
      categories.clear();
      event_names.clear();
      EXPECT_EQ(kClockIdIncremental,
                packet.trace_packet_defaults().timestamp_clock_id());
    }
    if (packet.has_clock_snapshot()) {
      for (auto& clock : packet.clock_snapshot().clocks()) {
        if (clock.is_incremental()) {
          found_incremental_clock = true;
          recent_absolute_time_ns = clock.timestamp();
          EXPECT_EQ(kClockIdIncremental, clock.clock_id());
        }
      }
    }

    if (!packet.has_track_event())
      continue;
    EXPECT_TRUE(
        packet.sequence_flags() &
        (perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED |
         perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE));
    const auto& track_event = packet.track_event();

    // Make sure we only see track events on one sequence.
    if (packet.trusted_packet_sequence_id()) {
      if (!sequence_id)
        sequence_id = packet.trusted_packet_sequence_id();
      EXPECT_EQ(sequence_id, packet.trusted_packet_sequence_id());
    }

    // Update incremental state.
    if (packet.has_interned_data()) {
      const auto& interned_data = packet.interned_data();
      for (const auto& it : interned_data.event_categories()) {
        EXPECT_EQ(categories.find(it.iid()), categories.end());
        categories[it.iid()] = it.name();
      }
      for (const auto& it : interned_data.event_names()) {
        EXPECT_EQ(event_names.find(it.iid()), event_names.end());
        event_names[it.iid()] = it.name();
      }
    }
    EXPECT_TRUE(found_incremental_clock);
    uint64_t absolute_timestamp = packet.timestamp() + recent_absolute_time_ns;
    recent_absolute_time_ns = absolute_timestamp;
    EXPECT_GT(absolute_timestamp, 0u);
    EXPECT_LE(absolute_timestamp, now);
    // Packet uses default (incremental) clock.
    EXPECT_FALSE(packet.has_timestamp_clock_id());
    if (track_event.type() ==
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN) {
      EXPECT_FALSE(begin_found);
      EXPECT_EQ(track_event.category_iids().size(), 1u);
      EXPECT_GE(track_event.category_iids()[0], 1u);
      EXPECT_EQ("test", categories[track_event.category_iids()[0]]);
      EXPECT_EQ("TestEvent", event_names[track_event.name_iid()]);
      begin_found = true;
    } else if (track_event.type() ==
               perfetto::protos::gen::TrackEvent::TYPE_SLICE_END) {
      EXPECT_FALSE(end_found);
      EXPECT_EQ(track_event.category_iids().size(), 0u);
      EXPECT_EQ(0u, track_event.name_iid());
      end_found = true;
    }
  }
  EXPECT_TRUE(incremental_state_was_cleared);
  EXPECT_TRUE(process_descriptor_found);
  EXPECT_TRUE(begin_found);
  EXPECT_TRUE(end_found);

  // Dummy instantiation of test templates.
  TestTrackEventInsideTemplate(true);
  TestCategoryAsTemplateParameter<kTestCategory>();
}

TEST_P(PerfettoApiTest, TrackEventWithIncrementalTimestamp) {
  for (auto disable_incremental_timestamps : {false, true}) {
    // Create a new trace session.
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_disable_incremental_timestamps(disable_incremental_timestamps);
    auto* tracing_session = NewTraceWithCategories({"bar"}, te_cfg);
    constexpr auto kClockIdIncremental =
        TrackEventIncrementalState::kClockIdIncremental;
    tracing_session->get()->StartBlocking();

    std::map<uint64_t, std::string> event_names;

    auto empty_lambda = [](perfetto::EventContext) {};

    constexpr uint64_t kInstantEvent1Time = 92718891479583;
    TRACE_EVENT_INSTANT(
        "bar", "InstantEvent1",
        perfetto::TraceTimestamp{kClockIdIncremental, kInstantEvent1Time},
        empty_lambda);

    constexpr uint64_t kInstantEvent2Time = 92718891618959;
    TRACE_EVENT_INSTANT(
        "bar", "InstantEvent2",
        perfetto::TraceTimestamp{kClockIdIncremental, kInstantEvent2Time},
        empty_lambda);

    auto trace = StopSessionAndReturnParsedTrace(tracing_session);
    uint64_t absolute_timestamp = 0;
    uint64_t prv_timestamp = 0;
    int event_count = 0;
    // Go through the packets and add the timestamps of those packets that use
    // the incremental clock - in order to get the absolute timestamps of the
    // track events.

    uint64_t default_clock_id = 0;
    bool is_incremental = false;

    for (const auto& packet : trace.packet()) {
      if (!packet.has_track_event() && !packet.has_clock_snapshot())
        continue;
      if (packet.has_trace_packet_defaults()) {
        auto& defaults = packet.trace_packet_defaults();
        if (defaults.has_timestamp_clock_id()) {
          default_clock_id = defaults.timestamp_clock_id();
        }
      }
      if (packet.has_clock_snapshot()) {
        for (auto& clock : packet.clock_snapshot().clocks()) {
          if (clock.is_incremental()) {
            is_incremental = true;
            absolute_timestamp = clock.timestamp();
            EXPECT_EQ(clock.clock_id(), kClockIdIncremental);
            EXPECT_FALSE(disable_incremental_timestamps);
          }
        }
      } else {
        auto clock_id = packet.has_timestamp_clock_id()
                            ? packet.timestamp_clock_id()
                            : default_clock_id;
        if (clock_id == kClockIdIncremental) {
          absolute_timestamp = prv_timestamp + packet.timestamp();
          EXPECT_FALSE(disable_incremental_timestamps);
        } else {
          absolute_timestamp = packet.timestamp();
          EXPECT_TRUE(disable_incremental_timestamps);
        }
      }
      prv_timestamp = absolute_timestamp;

      if (packet.sequence_flags() & perfetto::protos::pbzero::TracePacket::
                                        SEQ_INCREMENTAL_STATE_CLEARED) {
        event_names.clear();
      }

      // Update incremental state.
      if (packet.has_interned_data()) {
        const auto& interned_data = packet.interned_data();
        for (const auto& it : interned_data.event_names()) {
          EXPECT_EQ(event_names.find(it.iid()), event_names.end());
          event_names[it.iid()] = it.name();
        }
      }

      if (event_names[packet.track_event().name_iid()] == "InstantEvent1") {
        event_count++;
        EXPECT_EQ(absolute_timestamp, kInstantEvent1Time);
      } else if (event_names[packet.track_event().name_iid()] ==
                 "InstantEvent2") {
        event_count++;
        EXPECT_EQ(absolute_timestamp, kInstantEvent2Time);
      }
    }
    EXPECT_NE(is_incremental, disable_incremental_timestamps);
    EXPECT_EQ(event_count, 2);
  }
}

TEST_P(PerfettoApiTest, TrackEventCategories) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Emit some track events.
  TRACE_EVENT_BEGIN("foo", "NotEnabled");
  TRACE_EVENT_END("foo");
  TRACE_EVENT_BEGIN("bar", "Enabled");
  TRACE_EVENT_END("bar");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::string trace(raw_trace.data(), raw_trace.size());
  // TODO(skyostil): Come up with a nicer way to verify trace contents.
  EXPECT_THAT(trace, HasSubstr("Enabled"));
  EXPECT_THAT(trace, Not(HasSubstr("NotEnabled")));
}

TEST_P(PerfettoApiTest, ClearIncrementalState) {
  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("incr_data_source");
  TestIncrementalDataSource::Register(dsd);
  perfetto::test::SyncProducers();

  // Setup the trace config with an incremental state clearing period.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("incr_data_source");
  auto* is_cfg = cfg.mutable_incremental_state_config();
  is_cfg->set_clear_period_ms(10);

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // Observe at least 5 incremental state resets.
  constexpr size_t kMaxLoops = 100;
  size_t loops = 0;
  size_t times_cleared = 0;
  while (times_cleared < 5) {
    ASSERT_LT(loops++, kMaxLoops);
    TestIncrementalDataSource::Trace(
        [&](TestIncrementalDataSource::TraceContext ctx) {
          auto* incr_state = ctx.GetIncrementalState();
          if (!incr_state->flag) {
            incr_state->flag = true;
            times_cleared++;
          }
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  tracing_session->get()->StopBlocking();
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<TestIncrementalDataSource>();
}

TEST_P(PerfettoApiTest, ClearIncrementalStateMultipleInstances) {
  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("incr_data_source");
  TestIncrementalDataSource::Register(dsd);
  perfetto::test::SyncProducers();

  // Setup the trace config with an incremental state clearing period.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("incr_data_source");

  WaitableTestEvent cleared;
  NiceMock<MockFunction<void(
      const perfetto::DataSourceBase::ClearIncrementalStateArgs&)>>
      cb;
  ON_CALL(cb, Call).WillByDefault([&] { cleared.Notify(); });
  TestIncrementalDataSource::SetWillClearIncrementalStateCallback(
      cb.AsStdFunction());
  auto cleanup = MakeCleanup([&] {
    TestIncrementalDataSource::SetWillClearIncrementalStateCallback({});
  });

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  auto* is_cfg = cfg.mutable_incremental_state_config();
  is_cfg->set_clear_period_ms(10);

  // Create another tracing session that clear the incremental state
  // periodically.
  auto* tracing_session2 = NewTrace(cfg);
  tracing_session2->get()->StartBlocking();

  size_t count_instances = 0;
  TestIncrementalDataSource::Trace(
      [&](TestIncrementalDataSource::TraceContext ctx) {
        count_instances++;
        auto* incr_state = ctx.GetIncrementalState();
        if (!incr_state->flag) {
          incr_state->flag = true;
        }
      });
  ASSERT_EQ(count_instances, 2u);

  // Wait for two incremental state reset.
  cleared.Reset();
  cleared.Wait();
  cleared.Reset();
  cleared.Wait();

  std::vector<bool> instances_incremental_states;
  TestIncrementalDataSource::Trace(
      [&](TestIncrementalDataSource::TraceContext ctx) {
        auto* incr_state = ctx.GetIncrementalState();
        instances_incremental_states.push_back(incr_state->flag);
      });

  // There are two instances.
  EXPECT_EQ(instances_incremental_states.size(), 2u);
  // One was cleared.
  EXPECT_THAT(instances_incremental_states, Contains(false));
  // The other one wasn't.
  EXPECT_THAT(instances_incremental_states, Contains(true));

  tracing_session->get()->StopBlocking();
  tracing_session2->get()->StopBlocking();

  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<TestIncrementalDataSource>();
}

TEST_P(PerfettoApiTest, TrackEventRegistrationWithModule) {
  perfetto::internal::TrackEventDataSource::ResetForTesting();
  MockTracingMuxer muxer;

  // Each track event namespace registers its own data source.
  perfetto::TrackEvent::Register();
  EXPECT_EQ(1u, muxer.data_sources.size());

  tracing_module::InitializeCategories();
  EXPECT_EQ(1u, muxer.data_sources.size());

  // Both data sources have the same name but distinct static data (i.e.,
  // individual instance states).
  EXPECT_EQ("track_event", muxer.data_sources[0].dsd.name());
}

TEST_P(PerfettoApiTest, TrackEventDescriptor) {
  perfetto::internal::TrackEventDataSource::ResetForTesting();
  MockTracingMuxer muxer;

  perfetto::TrackEvent::Register();
  EXPECT_EQ(1u, muxer.data_sources.size());
  EXPECT_EQ("track_event", muxer.data_sources[0].dsd.name());

  perfetto::protos::gen::TrackEventDescriptor desc;
  auto desc_raw = muxer.data_sources[0].dsd.track_event_descriptor_raw();
  EXPECT_TRUE(desc.ParseFromArray(desc_raw.data(), desc_raw.size()));

  // Check that the advertised categories match PERFETTO_DEFINE_CATEGORIES (see
  // above).
  EXPECT_EQ(9, desc.available_categories_size());
  EXPECT_EQ("test", desc.available_categories()[0].name());
  EXPECT_EQ("This is a test category",
            desc.available_categories()[0].description());
  EXPECT_EQ("tag", desc.available_categories()[0].tags()[0]);
  EXPECT_EQ("test.verbose", desc.available_categories()[1].name());
  EXPECT_EQ("foo", desc.available_categories()[2].name());
  EXPECT_EQ("bar", desc.available_categories()[3].name());
  EXPECT_EQ("cat", desc.available_categories()[4].name());
  EXPECT_EQ("slow", desc.available_categories()[4].tags()[0]);
  EXPECT_EQ("cat.verbose", desc.available_categories()[5].name());
  EXPECT_EQ("debug", desc.available_categories()[5].tags()[0]);
  EXPECT_EQ("cat-with-dashes", desc.available_categories()[6].name());
  EXPECT_EQ("slow_category", desc.available_categories()[7].name());
  EXPECT_EQ("slow", desc.available_categories()[7].tags()[0]);
  EXPECT_EQ("disabled-by-default-cat", desc.available_categories()[8].name());
}

TEST_P(PerfettoApiTest, TrackEventSharedIncrementalState) {
  tracing_module::InitializeCategories();

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  perfetto::internal::TrackEventIncrementalState* main_state = nullptr;
  perfetto::TrackEvent::Trace(
      [&main_state](perfetto::TrackEvent::TraceContext ctx) {
        main_state = ctx.GetIncrementalState();
      });
  perfetto::internal::TrackEventIncrementalState* module_state =
      tracing_module::GetIncrementalState();

  // Both track event data sources should use the same incremental state (thanks
  // to sharing TLS).
  EXPECT_NE(nullptr, main_state);
  EXPECT_EQ(main_state, module_state);
  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, TrackEventCategoriesWithModule) {
  // Check that categories defined in two different category registries are
  // enabled and disabled correctly.
  tracing_module::InitializeCategories();

  // Create a new trace session. Only the "foo" category is enabled. It also
  // exists both locally and in the existing module.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  // Emit some track events locally and from the test module.
  TRACE_EVENT_BEGIN("foo", "FooEventFromMain");
  TRACE_EVENT_END("foo");
  tracing_module::EmitTrackEvents();
  tracing_module::EmitTrackEvents2();
  TRACE_EVENT_BEGIN("bar", "DisabledEventFromMain");
  TRACE_EVENT_END("bar");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::string trace(raw_trace.data(), raw_trace.size());
  EXPECT_THAT(trace, HasSubstr("FooEventFromMain"));
  EXPECT_THAT(trace, Not(HasSubstr("DisabledEventFromMain")));
  EXPECT_THAT(trace, HasSubstr("FooEventFromModule"));
  EXPECT_THAT(trace, Not(HasSubstr("DisabledEventFromModule")));
  EXPECT_THAT(trace, HasSubstr("FooEventFromModule2"));
  EXPECT_THAT(trace, Not(HasSubstr("DisabledEventFromModule2")));

  perfetto::protos::gen::Trace parsed_trace;
  ASSERT_TRUE(parsed_trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

  uint32_t sequence_id = 0;
  for (const auto& packet : parsed_trace.packet()) {
    if (!packet.has_track_event())
      continue;

    // Make sure we only see track events on one sequence. This means all track
    // event modules are sharing the same trace writer (by using the same TLS
    // index).
    if (packet.trusted_packet_sequence_id()) {
      if (!sequence_id)
        sequence_id = packet.trusted_packet_sequence_id();
      EXPECT_EQ(sequence_id, packet.trusted_packet_sequence_id());
    }
  }
}

TEST_P(PerfettoApiTest, TrackEventNamespaces) {
  perfetto::TrackEvent::Register();
  other_ns::TrackEvent::Register();
  tracing_module::InitializeCategories();

  auto* tracing_session =
      NewTraceWithCategories({"test", "cat1", "extra", "other_ns"});
  tracing_session->get()->StartBlocking();

  // Default namespace.
  TRACE_EVENT_INSTANT("test", "MainNamespaceEvent");
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("test"));

  // Other namespace in a block scope.
  {
    PERFETTO_USE_CATEGORIES_FROM_NAMESPACE_SCOPED(other_ns);
    TRACE_EVENT_INSTANT("other_ns", "OtherNamespaceEvent");
    EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("other_ns"));
  }

  // Back to the default namespace.
  TRACE_EVENT_INSTANT("test", "MainNamespaceEvent2");

  // More namespaces defined in another module.
  tracing_module::EmitTrackEventsFromAllNamespaces();

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre("I:test.MainNamespaceEvent", "I:other_ns.OtherNamespaceEvent",
                  "I:test.MainNamespaceEvent2",
                  "B:cat1.DefaultNamespaceFromModule",
                  "B:extra.ExtraNamespaceFromModule",
                  "B:extra.OverrideNamespaceFromModule",
                  "B:extra.DefaultNamespace", "B:cat1.DefaultNamespace"));
}

TEST_P(PerfettoApiTest, TrackEventNamespacesRegisterAfterStart) {
  perfetto::TrackEvent::Register();
  tracing_module::InitializeCategories();

  auto* tracing_session = NewTraceWithCategories({"test", "other_ns"});
  tracing_session->get()->StartBlocking();

  // Default namespace.
  TRACE_EVENT_INSTANT("test", "MainNamespaceEvent1");
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("test"));

  // Other namespace in a block scope.
  {
    PERFETTO_USE_CATEGORIES_FROM_NAMESPACE_SCOPED(other_ns);
    TRACE_EVENT_INSTANT("other_ns", "OtherNamespaceEvent1");
    EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("other_ns"));
  }

  other_ns::TrackEvent::Register();

  // Default namespace.
  TRACE_EVENT_INSTANT("test", "MainNamespaceEvent2");
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("test"));

  // Other namespace in a block scope.
  {
    PERFETTO_USE_CATEGORIES_FROM_NAMESPACE_SCOPED(other_ns);
    TRACE_EVENT_INSTANT("other_ns", "OtherNamespaceEvent2");
    EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("other_ns"));
  }

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("I:test.MainNamespaceEvent1",
                                  "I:test.MainNamespaceEvent2",
                                  "I:other_ns.OtherNamespaceEvent2"));
}

TEST_P(PerfettoApiTest, TrackEventDynamicCategories) {
  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Session #1 enabled the "dynamic" category.
  auto* tracing_session = NewTraceWithCategories({"dynamic"});
  tracing_session->get()->StartBlocking();

  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("dynamic"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic_2"));

  // Session #2 enables "dynamic_2".
  auto* tracing_session2 = NewTraceWithCategories({"dynamic_2"});
  tracing_session2->get()->StartBlocking();

  // Test naming dynamic categories with std::string.
  perfetto::DynamicCategory dynamic{"dynamic"};
  TRACE_EVENT_BEGIN(dynamic, "EventInDynamicCategory");
  perfetto::DynamicCategory dynamic_disabled{"dynamic_disabled"};
  TRACE_EVENT_BEGIN(dynamic_disabled, "EventInDisabledDynamicCategory");

  // Test naming dynamic categories statically.
  TRACE_EVENT_BEGIN("dynamic", "EventInStaticallyNamedDynamicCategory");

  perfetto::DynamicCategory dynamic_2{"dynamic_2"};
  TRACE_EVENT_BEGIN(dynamic_2, "EventInSecondDynamicCategory");
  TRACE_EVENT_BEGIN("dynamic_2", "EventInSecondStaticallyNamedDynamicCategory");

  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dynamic));
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dynamic_2));

  std::thread thread([] {
    // Make sure the category name can actually be computed at runtime.
    std::string name = "dyn";
    if (perfetto::base::GetThreadId())
      name += "amic";
    perfetto::DynamicCategory cat{name};
    TRACE_EVENT_BEGIN(cat, "DynamicFromOtherThread");
    perfetto::DynamicCategory cat2{"dynamic_disabled"};
    TRACE_EVENT_BEGIN(cat2, "EventInDisabledDynamicCategory");
  });
  thread.join();

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::string trace(raw_trace.data(), raw_trace.size());
  EXPECT_THAT(trace, HasSubstr("EventInDynamicCategory"));
  EXPECT_THAT(trace, Not(HasSubstr("EventInDisabledDynamicCategory")));
  EXPECT_THAT(trace, HasSubstr("DynamicFromOtherThread"));
  EXPECT_THAT(trace, Not(HasSubstr("EventInSecondDynamicCategory")));
  EXPECT_THAT(trace, HasSubstr("EventInStaticallyNamedDynamicCategory"));
  EXPECT_THAT(trace,
              Not(HasSubstr("EventInSecondStaticallyNamedDynamicCategory")));

  raw_trace = StopSessionAndReturnBytes(tracing_session2);
  trace = std::string(raw_trace.data(), raw_trace.size());
  EXPECT_THAT(trace, Not(HasSubstr("EventInDynamicCategory")));
  EXPECT_THAT(trace, Not(HasSubstr("EventInDisabledDynamicCategory")));
  EXPECT_THAT(trace, Not(HasSubstr("DynamicFromOtherThread")));
  EXPECT_THAT(trace, HasSubstr("EventInSecondDynamicCategory"));
  EXPECT_THAT(trace, Not(HasSubstr("EventInStaticallyNamedDynamicCategory")));
  EXPECT_THAT(trace, HasSubstr("EventInSecondStaticallyNamedDynamicCategory"));
}

TEST_P(PerfettoApiTest, TrackEventConcurrentSessions) {
  // Check that categories that are enabled and disabled in two parallel tracing
  // sessions don't interfere.

  // Session #1 enables the "foo" category.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  // Session #2 enables the "bar" category.
  auto* tracing_session2 = NewTraceWithCategories({"bar"});
  tracing_session2->get()->StartBlocking();

  // Emit some track events under both categories.
  TRACE_EVENT_BEGIN("foo", "Session1_First");
  TRACE_EVENT_END("foo");
  TRACE_EVENT_BEGIN("bar", "Session2_First");
  TRACE_EVENT_END("bar");

  tracing_session->get()->StopBlocking();
  TRACE_EVENT_BEGIN("foo", "Session1_Second");
  TRACE_EVENT_END("foo");
  TRACE_EVENT_BEGIN("bar", "Session2_Second");
  TRACE_EVENT_END("bar");

  tracing_session2->get()->StopBlocking();
  TRACE_EVENT_BEGIN("foo", "Session1_Third");
  TRACE_EVENT_END("foo");
  TRACE_EVENT_BEGIN("bar", "Session2_Third");
  TRACE_EVENT_END("bar");

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  std::string trace(raw_trace.data(), raw_trace.size());
  EXPECT_THAT(trace, HasSubstr("Session1_First"));
  EXPECT_THAT(trace, Not(HasSubstr("Session1_Second")));
  EXPECT_THAT(trace, Not(HasSubstr("Session1_Third")));
  EXPECT_THAT(trace, Not(HasSubstr("Session2_First")));
  EXPECT_THAT(trace, Not(HasSubstr("Session2_Second")));
  EXPECT_THAT(trace, Not(HasSubstr("Session2_Third")));

  std::vector<char> raw_trace2 = tracing_session2->get()->ReadTraceBlocking();
  std::string trace2(raw_trace2.data(), raw_trace2.size());
  EXPECT_THAT(trace2, Not(HasSubstr("Session1_First")));
  EXPECT_THAT(trace2, Not(HasSubstr("Session1_Second")));
  EXPECT_THAT(trace2, Not(HasSubstr("Session1_Third")));
  EXPECT_THAT(trace2, HasSubstr("Session2_First"));
  EXPECT_THAT(trace2, HasSubstr("Session2_Second"));
  EXPECT_THAT(trace2, Not(HasSubstr("Session2_Third")));
}

TEST_P(PerfettoApiTest, TrackEventProcessAndThreadDescriptors) {
  // Thread and process descriptors can be set before tracing is enabled.
  {
    auto track = perfetto::ProcessTrack::Current();
    auto desc = track.Serialize();
    desc.set_name("hello.exe");
    desc.mutable_chrome_process()->set_process_priority(1);
    perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
  }

  // Erased tracks shouldn't show up anywhere.
  {
    perfetto::Track erased(1234u);
    auto desc = erased.Serialize();
    desc.set_name("ErasedTrack");
    perfetto::TrackEvent::SetTrackDescriptor(erased, std::move(desc));
    perfetto::TrackEvent::EraseTrackDescriptor(erased);
  }

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_INSTANT("test", "MainThreadEvent");

  std::thread thread([&] {
    auto track = perfetto::ThreadTrack::Current();
    auto desc = track.Serialize();
    desc.set_name("TestThread");
    perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
    TRACE_EVENT_INSTANT("test", "ThreadEvent");
  });
  thread.join();

  // Update the process descriptor while tracing is enabled. It should be
  // immediately reflected in the trace.
  {
    auto track = perfetto::ProcessTrack::Current();
    auto desc = track.Serialize();
    desc.set_name("goodbye.exe");
    perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
    perfetto::TrackEvent::Flush();
  }

  tracing_session->get()->StopBlocking();

  // After tracing ends, setting the descriptor has no immediate effect.
  {
    auto track = perfetto::ProcessTrack::Current();
    auto desc = track.Serialize();
    desc.set_name("noop.exe");
    perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
  }

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

  std::vector<perfetto::protos::gen::TrackDescriptor> descs;
  std::vector<perfetto::protos::gen::TrackDescriptor> thread_descs;
  uint32_t main_thread_sequence = GetMainThreadPacketSequenceId(trace);
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor()) {
      if (packet.trusted_packet_sequence_id() == main_thread_sequence) {
        descs.push_back(packet.track_descriptor());
      } else if (packet.track_descriptor().has_thread()) {
        thread_descs.push_back(packet.track_descriptor());
      }
    }
  }

  // The main thread records the initial process name as well as the one that's
  // set during tracing. Additionally it records a thread descriptor for the
  // main thread.

  EXPECT_EQ(3u, descs.size());

  // Default track for the main thread.
  EXPECT_EQ(0, descs[0].process().pid());
  EXPECT_NE(0, descs[0].thread().pid());

  // First process descriptor.
  EXPECT_NE(0, descs[1].process().pid());
  EXPECT_EQ("hello.exe", descs[1].name());

  // Second process descriptor.
  EXPECT_NE(0, descs[2].process().pid());
  EXPECT_EQ("goodbye.exe", descs[2].name());

  // The child thread records only its own thread descriptor (twice, since it
  // was mutated).
  ASSERT_EQ(2u, thread_descs.size());
  EXPECT_EQ("TestThread", thread_descs[0].name());
  EXPECT_NE(0, thread_descs[0].thread().pid());
  EXPECT_NE(0, thread_descs[0].thread().tid());
  EXPECT_EQ("TestThread", thread_descs[1].name());
  EXPECT_NE(0, thread_descs[1].thread().pid());
  EXPECT_NE(0, thread_descs[1].thread().tid());
  EXPECT_NE(0, descs[2].process().pid());
  EXPECT_EQ("goodbye.exe", descs[2].name());
}

TEST_P(PerfettoApiTest, CustomTrackDescriptor) {
  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  auto track = perfetto::ProcessTrack::Current();
  auto desc = track.Serialize();
  desc.mutable_process()->set_process_name("testing.exe");
  desc.mutable_thread()->set_tid(
      static_cast<int32_t>(perfetto::base::GetThreadId()));
  desc.mutable_chrome_process()->set_process_priority(123);
  perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  uint32_t main_thread_sequence = GetMainThreadPacketSequenceId(trace);
  bool found_desc = false;
  for (const auto& packet : trace.packet()) {
    if (packet.trusted_packet_sequence_id() != main_thread_sequence)
      continue;
    if (packet.has_track_descriptor()) {
      auto td = packet.track_descriptor();
      if (!td.has_process())
        continue;
      EXPECT_NE(0, td.process().pid());
      EXPECT_TRUE(td.has_chrome_process());
      EXPECT_EQ("testing.exe", td.process().process_name());
      EXPECT_EQ(123, td.chrome_process().process_priority());
      found_desc = true;
    }
  }
  EXPECT_TRUE(found_desc);
}

TEST_P(PerfettoApiTest, TrackEventCustomTrack) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Declare a custom track and give it a name.
  uint64_t async_id = 123;

  {
    auto track = perfetto::Track(async_id);
    auto desc = track.Serialize();
    desc.set_name("MyCustomTrack");
    perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
  }

  // Start events on one thread and end them on another.
  TRACE_EVENT_BEGIN("bar", "AsyncEvent", perfetto::Track(async_id), "debug_arg",
                    123);

  TRACE_EVENT_BEGIN("bar", "SubEvent", perfetto::Track(async_id),
                    [](perfetto::EventContext) {});
  const auto main_thread_track =
      perfetto::Track(async_id, perfetto::ThreadTrack::Current());
  std::thread thread([&] {
    TRACE_EVENT_END("bar", perfetto::Track(async_id));
    TRACE_EVENT_END("bar", perfetto::Track(async_id), "arg1", false, "arg2",
                    true);
    const auto thread_track =
        perfetto::Track(async_id, perfetto::ThreadTrack::Current());
    // Thread-scoped tracks will have different uuids on different thread even
    // if the id matches.
    ASSERT_NE(main_thread_track.uuid, thread_track.uuid);
  });
  thread.join();

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that the track uuids match on the begin and end events.
  const auto track = perfetto::Track(async_id);
  uint32_t main_thread_sequence = GetMainThreadPacketSequenceId(trace);
  int event_count = 0;
  bool found_descriptor = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        !packet.track_descriptor().has_process() &&
        !packet.track_descriptor().has_thread()) {
      auto td = packet.track_descriptor();
      EXPECT_EQ("MyCustomTrack", td.name());
      EXPECT_EQ(track.uuid, td.uuid());
      EXPECT_EQ(perfetto::ProcessTrack::Current().uuid, td.parent_uuid());
      found_descriptor = true;
      continue;
    }

    if (!packet.has_track_event())
      continue;
    auto track_event = packet.track_event();
    if (track_event.type() ==
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN) {
      EXPECT_EQ(main_thread_sequence, packet.trusted_packet_sequence_id());
      EXPECT_EQ(track.uuid, track_event.track_uuid());
    } else {
      EXPECT_NE(main_thread_sequence, packet.trusted_packet_sequence_id());
      EXPECT_EQ(track.uuid, track_event.track_uuid());
    }
    event_count++;
  }
  EXPECT_TRUE(found_descriptor);
  EXPECT_EQ(4, event_count);
  perfetto::TrackEvent::EraseTrackDescriptor(track);
}

TEST_P(PerfettoApiTest, TrackEventCustomNamedTrack) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Declare a custom track and give it a name.
  uint64_t async_id = 123;

  // Start events on one thread and end them on another.
  TRACE_EVENT_BEGIN("bar", "AsyncEvent",
                    perfetto::NamedTrack("MyCustomTrack", async_id),
                    "debug_arg", 123);

  TRACE_EVENT_BEGIN("bar", "SubEvent",
                    perfetto::NamedTrack("MyCustomTrack", async_id),
                    [](perfetto::EventContext) {});
  const auto main_thread_track =
      perfetto::NamedTrack::ThreadScoped("MyCustomTrack", async_id);
  std::thread thread([&] {
    TRACE_EVENT_END("bar", perfetto::NamedTrack("MyCustomTrack", async_id));
    TRACE_EVENT_END("bar", perfetto::NamedTrack("MyCustomTrack", async_id),
                    "arg1", false, "arg2", true);
    const auto thread_track =
        perfetto::NamedTrack::ThreadScoped("MyCustomTrack", async_id);
    // Thread-scoped tracks will have different uuids on different thread even
    // if the id matches.
    ASSERT_NE(main_thread_track.uuid, thread_track.uuid);
  });
  thread.join();

  const auto global_track = perfetto::NamedTrack::Global("GlobalTrack");
  const auto named_track_with_id = perfetto::NamedTrack("MyCustomTrack", 1);
  // These two global tracks should have different non-trivial uuids.
  ASSERT_NE(global_track.uuid, named_track_with_id.uuid);
  ASSERT_NE(global_track.uuid, 0u);
  ASSERT_NE(named_track_with_id.uuid, 0u);

  TRACE_EVENT_INSTANT("bar", "InstantEvent", global_track);
  TRACE_EVENT_INSTANT("bar", "InstantEvent2", named_track_with_id);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that the track uuids match on the begin and end events.
  const auto track = perfetto::NamedTrack("MyCustomTrack", async_id);
  uint32_t main_thread_sequence = GetMainThreadPacketSequenceId(trace);
  std::vector<std::string> collected_events;

  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        !packet.track_descriptor().has_process() &&
        !packet.track_descriptor().has_thread()) {
      auto td = packet.track_descriptor();
      collected_events.push_back(
          "TrackDescriptor name=" + td.static_name() +
          " uuid=" + std::to_string(td.uuid()) +
          " parent_uuid=" + std::to_string(td.parent_uuid()));
      continue;
    }

    if (!packet.has_track_event())
      continue;
    auto track_event = packet.track_event();
    if (track_event.type() ==
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN) {
      collected_events.push_back(
          "SliceBegin track_uuid=" + std::to_string(track_event.track_uuid()) +
          " main_thread=" +
          std::to_string(packet.trusted_packet_sequence_id() ==
                         main_thread_sequence));
    } else if (track_event.type() ==
               perfetto::protos::gen::TrackEvent::TYPE_SLICE_END) {
      collected_events.push_back(
          "SliceEnd track_uuid=" + std::to_string(track_event.track_uuid()) +
          " main_thread=" +
          std::to_string(packet.trusted_packet_sequence_id() ==
                         main_thread_sequence));
    } else if (track_event.type() ==
               perfetto::protos::gen::TrackEvent::TYPE_INSTANT) {
      collected_events.push_back("Instant track_uuid=" +
                                 std::to_string(track_event.track_uuid()));
    }
  }

  std::string process_uuid_str =
      std::to_string(perfetto::ProcessTrack::Current().uuid);

  EXPECT_THAT(
      collected_events,
      ::testing::ContainerEq(std::vector<std::string>{
          "TrackDescriptor name=MyCustomTrack uuid=" +
              std::to_string(track.uuid) + " parent_uuid=" + process_uuid_str,
          "SliceBegin track_uuid=" + std::to_string(track.uuid) +
              " main_thread=1",
          "SliceBegin track_uuid=" + std::to_string(track.uuid) +
              " main_thread=1",
          "TrackDescriptor name=GlobalTrack uuid=" +
              std::to_string(global_track.uuid) + " parent_uuid=0",
          "Instant track_uuid=" + std::to_string(global_track.uuid),
          "TrackDescriptor name=MyCustomTrack uuid=" +
              std::to_string(named_track_with_id.uuid) +
              " parent_uuid=" + process_uuid_str,
          "Instant track_uuid=" + std::to_string(named_track_with_id.uuid),
          "TrackDescriptor name=MyCustomTrack uuid=" +
              std::to_string(track.uuid) + " parent_uuid=" + process_uuid_str,
          "SliceEnd track_uuid=" + std::to_string(track.uuid) +
              " main_thread=0",
          "SliceEnd track_uuid=" + std::to_string(track.uuid) +
              " main_thread=0"}));
}

TEST_P(PerfettoApiTest, TrackEventPtrNamedTrack) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // SetTrackDescriptor before starting the tracing session.
  auto track = perfetto::NamedTrack::FromPointer("MyCustomTrack", this);
  TRACE_EVENT_INSTANT("bar", "Event", track);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_descriptor = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        !packet.track_descriptor().has_process() &&
        !packet.track_descriptor().has_thread()) {
      auto td = packet.track_descriptor();
      EXPECT_EQ("MyCustomTrack", td.static_name());
      EXPECT_EQ(track.uuid, td.uuid());
      EXPECT_EQ(perfetto::ProcessTrack::Current().uuid, td.parent_uuid());
      found_descriptor = true;
      continue;
    }
  }
  EXPECT_TRUE(found_descriptor);
}

TEST_P(PerfettoApiTest, SiblingMergeBehavior) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Declare a custom track and give it a name.
  uint64_t async_id = 123;

  // SetTrackDescriptor before starting the tracing session.
  auto track_with_key = perfetto::NamedTrack("TrackWithSiblingKey", async_id)
                            .set_sibling_merge_key("key");
  TRACE_EVENT_INSTANT("bar", "Event", track_with_key);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_descriptor = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        !packet.track_descriptor().has_process() &&
        !packet.track_descriptor().has_thread()) {
      auto td = packet.track_descriptor();
      EXPECT_EQ("TrackWithSiblingKey", td.static_name());
      EXPECT_EQ(perfetto::protos::gen::TrackDescriptor::
                    SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY,
                td.sibling_merge_behavior());
      EXPECT_EQ("key", td.sibling_merge_key());
      found_descriptor = true;
      continue;
    }
  }
  EXPECT_TRUE(found_descriptor);
}

TEST_P(PerfettoApiTest, CustomTrackDescriptorForParent) {
  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // SetTrackDescriptor before starting the tracing session.
  auto parent_track = perfetto::NamedTrack("MyCustomParent");
  auto desc = parent_track.Serialize();
  perfetto::TrackEvent::SetTrackDescriptor(parent_track, std::move(desc));

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT("bar", "AsyncEvent",
                      perfetto::NamedTrack("MyCustomChild", 123, parent_track));

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_parent_desc = false;
  bool found_child_desc = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor()) {
      const auto& td = packet.track_descriptor();

      if (td.static_name() == "MyCustomParent") {
        found_parent_desc = true;
      } else if (td.static_name() == "MyCustomChild") {
        found_child_desc = true;
      }
    }
  }
  // SetTrackDescriptor for the parent happened before the tracing session was
  // running, but when emitting the child, the parent should be emitted as well.
  EXPECT_TRUE(found_parent_desc);
  EXPECT_TRUE(found_child_desc);
}

TEST_P(PerfettoApiTest, TrackEventCustomTimestampClock) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  static constexpr perfetto::protos::pbzero::BuiltinClock kMyClockId =
      static_cast<perfetto::protos::pbzero::BuiltinClock>(700);
  static constexpr uint64_t kTimestamp = 12345678;

  // First emit a clock snapshot that maps our custom clock to regular trace
  // time. Note that the clock snapshot should come before any events
  // referencing that clock.
  perfetto::TrackEvent::Trace([](perfetto::TrackEvent::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_timestamp_clock_id(
        static_cast<uint32_t>(perfetto::TrackEvent::GetTraceClockId()));
    packet->set_timestamp(perfetto::TrackEvent::GetTraceTimeNs());
    auto* clock_snapshot = packet->set_clock_snapshot();
    // First set the reference clock, i.e., the default trace clock in this
    // case.
    auto* clock = clock_snapshot->add_clocks();
    clock->set_clock_id(
        static_cast<uint32_t>(perfetto::TrackEvent::GetTraceClockId()));
    clock->set_timestamp(perfetto::TrackEvent::GetTraceTimeNs());
    // Then set the value of our reference clock at the same point in time. We
    // pretend our clock is one second behind trace time.
    clock = clock_snapshot->add_clocks();
    clock->set_clock_id(kMyClockId);
    clock->set_timestamp(kTimestamp + 1000000000ull);
  });

  // Next emit a trace event with a custom timestamp and a custom clock.
  TRACE_EVENT_INSTANT("foo", "EventWithCustomTime",
                      perfetto::TraceTimestamp{kMyClockId, kTimestamp});
  TRACE_EVENT_INSTANT("foo", "EventWithNormalTime");

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that both the clock id and the timestamp got written together with
  // the packet. Note that we don't check the actual clock sync behavior here
  // since that happens in the Trace Processor instead.
  bool found_clock_snapshot = false;
  bool found_event = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_clock_snapshot())
      found_clock_snapshot = true;
    if (!packet.has_track_event() || packet.timestamp() != kTimestamp)
      continue;
    found_event = true;
    EXPECT_EQ(static_cast<uint32_t>(kMyClockId), packet.timestamp_clock_id());
    EXPECT_EQ(kTimestamp, packet.timestamp());
  }
  EXPECT_TRUE(found_clock_snapshot);
  EXPECT_TRUE(found_event);
}

// Only synchronous phases are supported for other threads. Hence disabled this
// test.
// TODO(b/261493947): fix or remove.
TEST_P(PerfettoApiTest, DISABLED_LegacyEventWithThreadOverride) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN_WITH_ID_TID_AND_TIMESTAMP0("cat", "Name", 1,
                                               MyThreadId(456), MyTimestamp{0});
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that we wrote a track descriptor for the custom thread track, and
  // that the event was associated with that track.
  const auto track = perfetto::ThreadTrack::ForThread(456);
  bool found_descriptor = false;
  bool found_event = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        packet.track_descriptor().has_thread()) {
      auto td = packet.track_descriptor().thread();
      if (td.tid() == 456) {
        EXPECT_EQ(track.uuid, packet.track_descriptor().uuid());
        found_descriptor = true;
      }
    }

    if (!packet.has_track_event())
      continue;
    auto track_event = packet.track_event();
    if (track_event.legacy_event().phase() == TRACE_EVENT_PHASE_ASYNC_BEGIN) {
      EXPECT_EQ(0u, track_event.track_uuid());
      found_event = true;
    }
  }
  EXPECT_TRUE(found_descriptor);
  EXPECT_TRUE(found_event);
  perfetto::TrackEvent::EraseTrackDescriptor(track);
}

// Only synchronous phases are supported for other threads. Hence disabled this
// test.
TEST_P(PerfettoApiTest, DISABLED_LegacyEventWithProcessOverride) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  // Note: there's no direct entrypoint for adding trace events for another
  // process, so we're using the internal support macro here.
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(
      TRACE_EVENT_PHASE_INSTANT, "cat", "Name", 0, MyThreadId{789},
      MyTimestamp{0}, TRACE_EVENT_FLAG_HAS_PROCESS_ID);
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that the event has a pid_override matching MyThread above.
  bool found_event = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;
    auto track_event = packet.track_event();
    if (track_event.type() == perfetto::protos::gen::TrackEvent::TYPE_INSTANT) {
      EXPECT_EQ(789, track_event.legacy_event().pid_override());
      EXPECT_EQ(-1, track_event.legacy_event().tid_override());
      found_event = true;
    }
  }
  EXPECT_TRUE(found_event);
}

TEST_P(PerfettoApiTest, TrackDescriptorWrittenBeforeEvent) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Emit an event on a custom track.
  TRACE_EVENT_INSTANT("bar", "Event", perfetto::Track(8086));
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that the descriptor was written before the event.
  std::set<uint64_t> seen_descriptors;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor())
      seen_descriptors.insert(packet.track_descriptor().uuid());

    if (!packet.has_track_event())
      continue;
    auto track_event = packet.track_event();
    EXPECT_TRUE(seen_descriptors.find(track_event.track_uuid()) !=
                seen_descriptors.end());
  }
}

TEST_P(PerfettoApiTest, TrackEventCustomTrackAndTimestamp) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Custom track.
  perfetto::Track track(789);

  auto empty_lambda = [](perfetto::EventContext) {};
  constexpr uint64_t kBeginEventTime = 10;
  const MyTimestamp kEndEventTime{15};
  TRACE_EVENT_BEGIN("bar", "Event", track, kBeginEventTime, empty_lambda);
  TRACE_EVENT_END("bar", track, kEndEventTime, empty_lambda);

  constexpr uint64_t kInstantEventTime = 1;
  TRACE_EVENT_INSTANT("bar", "InstantEvent", track, kInstantEventTime,
                      empty_lambda);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  int event_count = 0;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;

    EXPECT_EQ(packet.timestamp_clock_id(),
              static_cast<uint32_t>(perfetto::TrackEvent::GetTraceClockId()));
    event_count++;
    switch (packet.track_event().type()) {
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN:
        EXPECT_EQ(packet.timestamp(), kBeginEventTime);
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_END:
        EXPECT_EQ(packet.timestamp(), kEndEventTime.ts);
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_INSTANT:
        EXPECT_EQ(packet.timestamp(), kInstantEventTime);
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_COUNTER:
      case perfetto::protos::gen::TrackEvent::TYPE_UNSPECIFIED:
        ADD_FAILURE();
    }
  }
  EXPECT_EQ(event_count, 3);
  perfetto::TrackEvent::EraseTrackDescriptor(track);
}

TEST_P(PerfettoApiTest, TrackEventCustomTrackAndTimestampNoLambda) {
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  perfetto::Track track(789);

  constexpr uint64_t kBeginEventTime = 10;
  constexpr uint64_t kEndEventTime = 15;
  TRACE_EVENT_BEGIN("bar", "Event", track, kBeginEventTime);
  TRACE_EVENT_END("bar", track, kEndEventTime);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  int event_count = 0;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;
    event_count++;
    switch (packet.track_event().type()) {
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN:
        EXPECT_EQ(packet.timestamp(), kBeginEventTime);
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_SLICE_END:
        EXPECT_EQ(packet.timestamp(), kEndEventTime);
        break;
      case perfetto::protos::gen::TrackEvent::TYPE_INSTANT:
      case perfetto::protos::gen::TrackEvent::TYPE_COUNTER:
      case perfetto::protos::gen::TrackEvent::TYPE_UNSPECIFIED:
        ADD_FAILURE();
    }
  }

  EXPECT_EQ(event_count, 2);
}

TEST_P(PerfettoApiTest, TrackEventAnonymousCustomTrack) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"bar"});
  tracing_session->get()->StartBlocking();

  // Emit an async event without giving it an explicit descriptor.
  uint64_t async_id = 4004;
  auto track = perfetto::Track(async_id, perfetto::ThreadTrack::Current());
  TRACE_EVENT_BEGIN("bar", "AsyncEvent", track);
  std::thread thread([&] { TRACE_EVENT_END("bar", track); });
  thread.join();

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that a descriptor for the track was emitted.
  bool found_descriptor = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor() &&
        !packet.track_descriptor().has_process() &&
        !packet.track_descriptor().has_thread() &&
        packet.track_descriptor().uuid() !=
            perfetto::ThreadTrack::Current().uuid) {
      auto td = packet.track_descriptor();
      EXPECT_EQ(track.uuid, td.uuid());
      EXPECT_EQ(perfetto::ThreadTrack::Current().uuid, td.parent_uuid());
      found_descriptor = true;
    }
  }
  EXPECT_TRUE(found_descriptor);
}

TEST_P(PerfettoApiTest, TrackEventTypedArgs) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  auto random_value = rand();
  TRACE_EVENT_BEGIN("foo", "EventWithTypedArg",
                    [random_value](perfetto::EventContext ctx) {
                      auto* log = ctx.event()->set_log_message();
                      log->set_source_location_iid(1);
                      log->set_body_iid(2);
                      auto* dbg = ctx.event()->add_debug_annotations();
                      dbg->set_name("random");
                      dbg->set_int_value(random_value);
                    });
  TRACE_EVENT_END("foo");

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_args = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;
    const auto& track_event = packet.track_event();
    if (track_event.type() !=
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN)
      continue;

    EXPECT_TRUE(track_event.has_log_message());
    const auto& log = track_event.log_message();
    EXPECT_EQ(1u, log.source_location_iid());
    EXPECT_EQ(2u, log.body_iid());

    const auto& dbg = track_event.debug_annotations()[0];
    EXPECT_EQ("random", dbg.name());
    EXPECT_EQ(random_value, dbg.int_value());

    found_args = true;
  }
  EXPECT_TRUE(found_args);
}

TEST_P(PerfettoApiTest, InlineTrackEventTypedArgs_SimpleRepeated) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  std::vector<uint64_t> flow_ids_old{1, 2, 3};
  std::vector<uint64_t> flow_ids{4, 5, 6};
  TRACE_EVENT_BEGIN("foo", "EventWithTypedArg",
                    perfetto::protos::pbzero::TrackEvent::kFlowIdsOld,
                    flow_ids_old,
                    perfetto::protos::pbzero::TrackEvent::kFlowIds, flow_ids);
  TRACE_EVENT_END("foo");

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_args = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;
    const auto& track_event = packet.track_event();
    if (track_event.type() !=
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN) {
      continue;
    }

    EXPECT_THAT(track_event.flow_ids_old(), testing::ElementsAre(1u, 2u, 3u));
    EXPECT_THAT(track_event.flow_ids(), testing::ElementsAre(4u, 5u, 6u));
    found_args = true;
  }
  EXPECT_TRUE(found_args);
}

namespace {

struct LogMessage {
  void WriteIntoTrace(
      perfetto::TracedProto<perfetto::protos::pbzero::LogMessage> context)
      const {
    context->set_source_location_iid(1);
    context->set_body_iid(2);
  }
};

auto GetWriteLogMessageRefLambda = []() {
  return [](perfetto::EventContext& ctx) {
    auto* log = ctx.event()->set_log_message();
    log->set_source_location_iid(1);
    log->set_body_iid(2);
  };
};

void CheckTypedArguments(
    const std::vector<char>& raw_trace,
    const char* event_name,
    perfetto::protos::gen::TrackEvent::Type type,
    std::function<void(const perfetto::protos::gen::TrackEvent&)> checker) {
  perfetto::protos::gen::Trace parsed_trace;
  ASSERT_TRUE(parsed_trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

  bool found_slice = false;
  ParsedIncrementalState incremental_state;

  for (const auto& packet : parsed_trace.packet()) {
    incremental_state.ClearIfNeeded(packet);
    incremental_state.Parse(packet);

    if (!packet.has_track_event())
      continue;
    const auto& track_event = packet.track_event();
    if (track_event.type() != type) {
      continue;
    }
    if (event_name &&
        incremental_state.GetEventName(track_event) != event_name) {
      continue;
    }

    checker(track_event);
    found_slice = true;
  }
  EXPECT_TRUE(found_slice);
}

void CheckLogMessagePresent(const std::vector<char>& raw_trace) {
  CheckTypedArguments(raw_trace, nullptr,
                      perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN,
                      [](const perfetto::protos::gen::TrackEvent& track_event) {
                        EXPECT_TRUE(track_event.has_log_message());
                        const auto& log = track_event.log_message();
                        EXPECT_EQ(1u, log.source_location_iid());
                        EXPECT_EQ(2u, log.body_iid());
                      });
}

}  // namespace

TEST_P(PerfettoApiTest, InlineTrackEventTypedArgs_NestedSingle) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "EventWithTypedArg",
                    perfetto::protos::pbzero::TrackEvent::kLogMessage,
                    LogMessage());
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
}

TEST_P(PerfettoApiTest, TrackEventThreadTime) {
  for (auto enable_thread_time : {true, false}) {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_enable_thread_time_sampling(enable_thread_time);
    auto* tracing_session = NewTraceWithCategories({"foo"}, te_cfg);

    tracing_session->get()->StartBlocking();

    perfetto::Track custom_track(1);

    TRACE_EVENT_BEGIN("foo", "event1");
    TRACE_EVENT_BEGIN("foo", "event2");
    TRACE_EVENT_BEGIN("foo", "event3");
    TRACE_EVENT_BEGIN("foo", "event4", custom_track);
    TRACE_EVENT_END("foo");
    TRACE_EVENT_END("foo");
    TRACE_EVENT_END("foo");
    TRACE_EVENT_END("foo");

    auto trace = StopSessionAndReturnParsedTrace(tracing_session);

    bool found_counter_track_descriptor = false;
    uint64_t thread_time_counter_uuid = 0;
    uint64_t default_counter_uuid = 0;
    std::unordered_set<std::string> event_names;
    for (const auto& packet : trace.packet()) {
      if (packet.has_track_descriptor() &&
          packet.track_descriptor().has_counter()) {
        EXPECT_FALSE(found_counter_track_descriptor);
        found_counter_track_descriptor = true;
        thread_time_counter_uuid = packet.track_descriptor().uuid();
        EXPECT_EQ("thread_time", packet.track_descriptor().static_name());
        auto counter = packet.track_descriptor().counter();
        EXPECT_EQ(
            perfetto::protos::gen::
                CounterDescriptor_BuiltinCounterType_COUNTER_THREAD_TIME_NS,
            counter.type());
        EXPECT_TRUE(counter.is_incremental());
      }
      if (packet.has_trace_packet_defaults()) {
        auto& defaults = packet.trace_packet_defaults().track_event_defaults();
        ASSERT_EQ(enable_thread_time ? 1u : 0u,
                  defaults.extra_counter_track_uuids().size());
        if (enable_thread_time) {
          default_counter_uuid = defaults.extra_counter_track_uuids().at(0);
        }
      }
      if (packet.has_track_event()) {
        std::string event_name;
        if (packet.has_interned_data()) {
          auto& event_names_info = packet.interned_data().event_names();
          if (event_names_info.size() > 0) {
            event_name = event_names_info[0].name();
          }
        }
        event_names.insert(event_name);
        EXPECT_EQ((enable_thread_time && event_name != "event4") ? 1u : 0u,
                  packet.track_event().extra_counter_values().size());
      }
    }
    EXPECT_TRUE(ContainsKey(event_names, "event1"));
    EXPECT_TRUE(ContainsKey(event_names, "event2"));
    EXPECT_TRUE(ContainsKey(event_names, "event3"));
    EXPECT_TRUE(ContainsKey(event_names, "event4"));
    EXPECT_EQ(enable_thread_time, found_counter_track_descriptor);
    EXPECT_EQ(default_counter_uuid, thread_time_counter_uuid);
    if (enable_thread_time) {
      EXPECT_GT(thread_time_counter_uuid, 0u);
    } else {
      EXPECT_EQ(thread_time_counter_uuid, 0u);
    }
  }
}

TEST_P(PerfettoApiTest, TrackEventArgs_TypedAndUntyped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E",
                    perfetto::protos::pbzero::TrackEvent::kLogMessage,
                    LogMessage(), "arg", "value");
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::string trace(raw_trace.data(), raw_trace.size());

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_UntypedAndTyped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E", "arg", "value",
                    perfetto::protos::pbzero::TrackEvent::kLogMessage,
                    LogMessage());
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_UntypedAndRefLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E", "arg", "value", GetWriteLogMessageRefLambda());
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_RefLambdaAndUntyped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E", GetWriteLogMessageRefLambda(), "arg", "value");
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_RefLambdaAndTyped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN(
      "foo", "E",
      [](perfetto::EventContext& ctx) {
        ctx.AddDebugAnnotation("arg", "value");
      },
      perfetto::protos::pbzero::TrackEvent::kLogMessage, LogMessage());
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_TypedAndRefLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E",
                    perfetto::protos::pbzero::TrackEvent::kLogMessage,
                    LogMessage(), [](perfetto::EventContext& ctx) {
                      ctx.AddDebugAnnotation("arg", "value");
                    });
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckLogMessagePresent(raw_trace);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_RefLambdaAndRefLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN(
      "foo", "E",
      [](perfetto::EventContext& ctx) {
        ctx.AddDebugAnnotation("arg1", "value1");
      },
      [](perfetto::EventContext& ctx) {
        ctx.AddDebugAnnotation("arg2", "value2");
      });
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find untyped arguments.
  EXPECT_THAT(
      ReadSlicesFromTrace(raw_trace),
      ElementsAre("B:foo.E(arg1=(string)value1,arg2=(string)value2)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_RefLambdaAndLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN(
      "foo", "E",
      [](perfetto::EventContext& ctx) {
        ctx.AddDebugAnnotation("arg1", "value1");
      },
      [](perfetto::EventContext ctx) {
        ctx.AddDebugAnnotation("arg2", "value2");
      });
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find untyped arguments.
  EXPECT_THAT(
      ReadSlicesFromTrace(raw_trace),
      ElementsAre("B:foo.E(arg1=(string)value1,arg2=(string)value2)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_RefLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E", [](perfetto::EventContext& ctx) {
    ctx.AddDebugAnnotation("arg", "value");
  });
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find untyped argument.
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E(arg=(string)value)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_Flow_Global) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT("foo", "E1", perfetto::Flow::Global(42));
  TRACE_EVENT_INSTANT("foo", "E2", perfetto::TerminatingFlow::Global(42));

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed argument.
  CheckTypedArguments(
      raw_trace, "E1", perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
      [](const perfetto::protos::gen::TrackEvent& track_event) {
        EXPECT_TRUE(track_event.flow_ids_old().empty());
        EXPECT_THAT(track_event.flow_ids(), testing::ElementsAre(42u));
      });
}

TEST_P(PerfettoApiTest, TrackEventArgs_Lambda_Multisession) {
  // Create two simultaneous tracing sessions.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  auto* tracing_session2 = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();
  tracing_session2->get()->StartBlocking();

  // Emit an event in both sessions using an argument function.
  auto make_arg = []() -> std::function<void(perfetto::EventContext&)> {
    return [](perfetto::EventContext& ctx) {
      ctx.event()->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT);
      ctx.event()->add_flow_ids(42);
    };
  };
  TRACE_EVENT_INSTANT("foo", "E1", make_arg());

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  std::vector<char> raw_trace2 = StopSessionAndReturnBytes(tracing_session2);

  // Check that the event was output twice.
  CheckTypedArguments(
      raw_trace, "E1", perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
      [](const perfetto::protos::gen::TrackEvent& track_event) {
        EXPECT_TRUE(track_event.flow_ids_old().empty());
        EXPECT_THAT(track_event.flow_ids(), testing::ElementsAre(42u));
      });
  CheckTypedArguments(
      raw_trace2, "E1", perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
      [](const perfetto::protos::gen::TrackEvent& track_event) {
        EXPECT_TRUE(track_event.flow_ids_old().empty());
        EXPECT_THAT(track_event.flow_ids(), testing::ElementsAre(42u));
      });
}

TEST_P(PerfettoApiTest, TrackEventArgs_MultipleFlows) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  {
    TRACE_EVENT("foo", "E1", perfetto::Flow::Global(1),
                perfetto::Flow::Global(2), perfetto::Flow::Global(3));
  }
  {
    TRACE_EVENT("foo", "E2", perfetto::Flow::Global(1),
                perfetto::TerminatingFlow::Global(2));
  }
  {
    TRACE_EVENT("foo", "E3", perfetto::TerminatingFlow::Global(3));
  }

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E1(flow_ids=1,2,3)", "E",
                          "B:foo.E2(flow_ids=1)(terminating_flow_ids=2)", "E",
                          "B:foo.E3(terminating_flow_ids=3)", "E"));
}

TEST_P(PerfettoApiTest, TrackEventArgs_Flow_ProcessScoped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT("foo", "E1", perfetto::Flow::ProcessScoped(1));
  TRACE_EVENT_INSTANT("foo", "E2", perfetto::TerminatingFlow::ProcessScoped(1));
  TRACE_EVENT_INSTANT("foo", "Flush");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed arguments.
  CheckTypedArguments(raw_trace, "E1",
                      perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
                      [](const perfetto::protos::gen::TrackEvent& track_event) {
                        EXPECT_EQ(track_event.flow_ids_old_size(), 0);
                        EXPECT_EQ(track_event.flow_ids_size(), 1);
                      });
  CheckTypedArguments(
      raw_trace, "E2", perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
      [](const perfetto::protos::gen::TrackEvent& track_event) {
        EXPECT_EQ(track_event.terminating_flow_ids_old_size(), 0);
        EXPECT_EQ(track_event.terminating_flow_ids_size(), 1);
      });
}

TEST_P(PerfettoApiTest, TrackEventArgs_Flow_FromPointer) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  int a;
  int* ptr = &a;
  TRACE_EVENT_INSTANT("foo", "E1", perfetto::Flow::FromPointer(ptr));
  TRACE_EVENT_INSTANT("foo", "E2", perfetto::TerminatingFlow::FromPointer(ptr));
  TRACE_EVENT_INSTANT("foo", "Flush");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  // Find typed arguments.
  CheckTypedArguments(raw_trace, "E1",
                      perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
                      [](const perfetto::protos::gen::TrackEvent& track_event) {
                        EXPECT_EQ(track_event.flow_ids_old_size(), 0);
                        EXPECT_EQ(track_event.flow_ids_size(), 1);
                      });
  CheckTypedArguments(
      raw_trace, "E2", perfetto::protos::gen::TrackEvent::TYPE_INSTANT,
      [](const perfetto::protos::gen::TrackEvent& track_event) {
        EXPECT_EQ(track_event.terminating_flow_ids_old_size(), 0);
        EXPECT_EQ(track_event.terminating_flow_ids_size(), 1);
      });
}

struct InternedLogMessageBody
    : public perfetto::TrackEventInternedDataIndex<
          InternedLogMessageBody,
          perfetto::protos::pbzero::InternedData::kLogMessageBodyFieldNumber,
          std::string> {
  static void Add(perfetto::protos::pbzero::InternedData* interned_data,
                  size_t iid,
                  const std::string& value) {
    auto l = interned_data->add_log_message_body();
    l->set_iid(iid);
    l->set_body(value.data(), value.size());
    commit_count++;
  }

  static int commit_count;
};

int InternedLogMessageBody::commit_count = 0;

TEST_P(PerfettoApiTest, TrackEventTypedArgsWithInterning) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  std::stringstream large_message;
  for (size_t i = 0; i < 512; i++)
    large_message << i << ". Something wicked this way comes. ";

  size_t body_iid;
  InternedLogMessageBody::commit_count = 0;
  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    EXPECT_EQ(0, InternedLogMessageBody::commit_count);
    body_iid = InternedLogMessageBody::Get(&ctx, "Alas, poor Yorick!");
    auto log = ctx.event()->set_log_message();
    log->set_body_iid(body_iid);
    EXPECT_EQ(1, InternedLogMessageBody::commit_count);

    auto body_iid2 = InternedLogMessageBody::Get(&ctx, "Alas, poor Yorick!");
    EXPECT_EQ(body_iid, body_iid2);
    EXPECT_EQ(1, InternedLogMessageBody::commit_count);
  });
  TRACE_EVENT_END("foo");

  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    // Check that very large amounts of interned data works.
    auto log = ctx.event()->set_log_message();
    log->set_body_iid(InternedLogMessageBody::Get(&ctx, large_message.str()));
    EXPECT_EQ(2, InternedLogMessageBody::commit_count);
  });
  TRACE_EVENT_END("foo");

  // Make sure interned data persists across trace points.
  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    auto body_iid2 = InternedLogMessageBody::Get(&ctx, "Alas, poor Yorick!");
    EXPECT_EQ(body_iid, body_iid2);

    auto body_iid3 = InternedLogMessageBody::Get(&ctx, "I knew him, Horatio");
    EXPECT_NE(body_iid, body_iid3);
    auto log = ctx.event()->set_log_message();
    log->set_body_iid(body_iid3);
    EXPECT_EQ(3, InternedLogMessageBody::commit_count);
  });
  TRACE_EVENT_END("foo");

  tracing_session->get()->StopBlocking();
  auto log_messages = ReadLogMessagesFromTrace(tracing_session->get());
  EXPECT_THAT(log_messages,
              ElementsAre("Alas, poor Yorick!", large_message.str(),
                          "I knew him, Horatio"));
}

struct InternedLogMessageBodySmall
    : public perfetto::TrackEventInternedDataIndex<
          InternedLogMessageBodySmall,
          perfetto::protos::pbzero::InternedData::kLogMessageBodyFieldNumber,
          const char*,
          perfetto::SmallInternedDataTraits> {
  static void Add(perfetto::protos::pbzero::InternedData* interned_data,
                  size_t iid,
                  const char* value) {
    auto l = interned_data->add_log_message_body();
    l->set_iid(iid);
    l->set_body(value);
  }
};

TEST_P(PerfettoApiTest, TrackEventTypedArgsWithInterningByValue) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  size_t body_iid;
  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    body_iid = InternedLogMessageBodySmall::Get(&ctx, "This above all:");
    auto log = ctx.event()->set_log_message();
    log->set_body_iid(body_iid);

    auto body_iid2 = InternedLogMessageBodySmall::Get(&ctx, "This above all:");
    EXPECT_EQ(body_iid, body_iid2);

    auto body_iid3 =
        InternedLogMessageBodySmall::Get(&ctx, "to thine own self be true");
    EXPECT_NE(body_iid, body_iid3);
  });
  TRACE_EVENT_END("foo");

  tracing_session->get()->StopBlocking();
  auto log_messages = ReadLogMessagesFromTrace(tracing_session->get());
  EXPECT_THAT(log_messages, ElementsAre("This above all:"));
}

struct InternedLogMessageBodyHashed
    : public perfetto::TrackEventInternedDataIndex<
          InternedLogMessageBodyHashed,
          perfetto::protos::pbzero::InternedData::kLogMessageBodyFieldNumber,
          std::string,
          perfetto::HashedInternedDataTraits> {
  static void Add(perfetto::protos::pbzero::InternedData* interned_data,
                  size_t iid,
                  const std::string& value) {
    auto l = interned_data->add_log_message_body();
    l->set_iid(iid);
    l->set_body(value.data(), value.size());
  }
};

TEST_P(PerfettoApiTest, TrackEventTypedArgsWithInterningByHashing) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  size_t body_iid;
  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    // Test using a dynamically created interned value.
    body_iid = InternedLogMessageBodyHashed::Get(
        &ctx, std::string("Though this ") + "be madness,");
    auto log = ctx.event()->set_log_message();
    log->set_body_iid(body_iid);

    auto body_iid2 =
        InternedLogMessageBodyHashed::Get(&ctx, "Though this be madness,");
    EXPECT_EQ(body_iid, body_iid2);

    auto body_iid3 =
        InternedLogMessageBodyHashed::Get(&ctx, "yet there is method in’t");
    EXPECT_NE(body_iid, body_iid3);
  });
  TRACE_EVENT_END("foo");

  tracing_session->get()->StopBlocking();
  auto log_messages = ReadLogMessagesFromTrace(tracing_session->get());
  EXPECT_THAT(log_messages, ElementsAre("Though this be madness,"));
}

struct InternedSourceLocation
    : public perfetto::TrackEventInternedDataIndex<
          InternedSourceLocation,
          perfetto::protos::pbzero::InternedData::kSourceLocationsFieldNumber,
          SourceLocation> {
  static void Add(perfetto::protos::pbzero::InternedData* interned_data,
                  size_t iid,
                  const SourceLocation& value) {
    auto l = interned_data->add_source_locations();
    auto file_name = std::get<0>(value);
    auto function_name = std::get<1>(value);
    auto line_number = std::get<2>(value);
    l->set_iid(iid);
    l->set_file_name(file_name);
    l->set_function_name(function_name);
    l->set_line_number(line_number);
  }
};

TEST_P(PerfettoApiTest, TrackEventTypedArgsWithInterningComplexValue) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "EventWithState", [&](perfetto::EventContext ctx) {
    const SourceLocation location{"file.cc", "SomeFunction", 123};
    auto location_iid = InternedSourceLocation::Get(&ctx, location);
    auto body_iid = InternedLogMessageBody::Get(&ctx, "To be, or not to be");
    auto log = ctx.event()->set_log_message();
    log->set_source_location_iid(location_iid);
    log->set_body_iid(body_iid);

    auto location_iid2 = InternedSourceLocation::Get(&ctx, location);
    EXPECT_EQ(location_iid, location_iid2);

    const SourceLocation location2{"file.cc", "SomeFunction", 456};
    auto location_iid3 = InternedSourceLocation::Get(&ctx, location2);
    EXPECT_NE(location_iid, location_iid3);
  });
  TRACE_EVENT_END("foo");

  tracing_session->get()->StopBlocking();
  auto log_messages = ReadLogMessagesFromTrace(tracing_session->get());
  EXPECT_THAT(log_messages,
              ElementsAre("SomeFunction(file.cc:123): To be, or not to be"));
}

TEST_P(PerfettoApiTest, TrackEventScoped) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  {
    uint64_t arg = 123;
    TRACE_EVENT("test", "TestEventWithArgs", [&](perfetto::EventContext ctx) {
      ctx.event()->set_log_message()->set_body_iid(arg);
    });
  }

  // Ensure a single line if statement counts as a valid scope for the macro.
  if (true)
    TRACE_EVENT("test", "SingleLineTestEvent");

  {
    // Make sure you can have multiple scoped events in the same scope.
    TRACE_EVENT("test", "TestEvent");
    TRACE_EVENT("test", "AnotherEvent");
    TRACE_EVENT("foo", "DisabledEvent");
  }
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre("B:test.TestEventWithArgs", "E", "B:test.SingleLineTestEvent",
                  "E", "B:test.TestEvent", "B:test.AnotherEvent", "E", "E"));
}

// A class similar to what Protozero generates for extended message.
class TestTrackEvent : public perfetto::protos::pbzero::TrackEvent {
 public:
  static const int field_number = 9901;

  void set_extension_value(int value) {
    // 9900-10000 is the range of extension field numbers reserved for testing.
    AppendTinyVarInt(field_number, value);
  }
};

TEST_P(PerfettoApiTest, ExtensionClass) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  {
    TRACE_EVENT("test", "TestEventWithExtensionArgs",
                [&](perfetto::EventContext ctx) {
                  ctx.event<perfetto::protos::pbzero::TestExtension>()
                      ->add_int_extension_for_testing(42);
                });
  }

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  EXPECT_GE(raw_trace.size(), 0u);

  bool found_extension = false;
  perfetto::protos::pbzero::Trace_Decoder trace(
      reinterpret_cast<uint8_t*>(raw_trace.data()), raw_trace.size());

  for (auto it = trace.packet(); it; ++it) {
    perfetto::protos::pbzero::TracePacket_Decoder packet(it->data(),
                                                         it->size());

    if (!packet.has_track_event())
      continue;

    auto track_event = packet.track_event();
    protozero::ProtoDecoder decoder(track_event.data, track_event.size);

    for (protozero::Field f = decoder.ReadField(); f.valid();
         f = decoder.ReadField()) {
      if (f.id() == perfetto::protos::pbzero::TestExtension::
                        FieldMetadata_IntExtensionForTesting::kFieldId) {
        found_extension = true;
      }
    }
  }

  EXPECT_TRUE(found_extension);
}

TEST_P(PerfettoApiTest, InlineTypedExtensionField) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  {
    TRACE_EVENT(
        "test", "TestEventWithExtensionArgs",
        perfetto::protos::pbzero::TestExtension::kIntExtensionForTesting,
        std::vector<int>{42},
        perfetto::protos::pbzero::TestExtension::kUintExtensionForTesting, 42u);
  }

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
  EXPECT_GE(raw_trace.size(), 0u);

  bool found_int_extension = false;
  bool found_uint_extension = false;
  perfetto::protos::pbzero::Trace_Decoder trace(
      reinterpret_cast<uint8_t*>(raw_trace.data()), raw_trace.size());

  for (auto it = trace.packet(); it; ++it) {
    perfetto::protos::pbzero::TracePacket_Decoder packet(it->data(),
                                                         it->size());

    if (!packet.has_track_event())
      continue;

    auto track_event = packet.track_event();
    protozero::ProtoDecoder decoder(track_event.data, track_event.size);

    for (protozero::Field f = decoder.ReadField(); f.valid();
         f = decoder.ReadField()) {
      if (f.id() == perfetto::protos::pbzero::TestExtension::
                        FieldMetadata_IntExtensionForTesting::kFieldId) {
        found_int_extension = true;
      } else if (f.id() ==
                 perfetto::protos::pbzero::TestExtension::
                     FieldMetadata_UintExtensionForTesting::kFieldId) {
        found_uint_extension = true;
      }
    }
  }

  EXPECT_TRUE(found_int_extension);
  EXPECT_TRUE(found_uint_extension);
}

TEST_P(PerfettoApiTest, TrackEventInstant) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT("test", "TestEvent");
  TRACE_EVENT_INSTANT("test", "AnotherEvent");
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("I:test.TestEvent", "I:test.AnotherEvent"));
}

TEST_P(PerfettoApiTest, TrackEventDefaultGlobalTrack) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT("test", "ThreadEvent");
  TRACE_EVENT_INSTANT("test", "GlobalEvent", perfetto::Track::Global(0u));
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("I:test.ThreadEvent", "[track=0]I:test.GlobalEvent"));
}

TEST_P(PerfettoApiTest, TrackEventTrackFromPointer) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  perfetto::Track parent_track(1);
  int* ptr = reinterpret_cast<int*>(2);
  TRACE_EVENT_INSTANT("test", "Event",
                      perfetto::Track::FromPointer(ptr, parent_track));

  perfetto::Track track(reinterpret_cast<uintptr_t>(ptr), parent_track);

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("[track=" + std::to_string(track.uuid) +
                                  "]I:test.Event"));
}

TEST_P(PerfettoApiTest, TrackEventTrackFromThreadScopedPointer) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  int num = 2;
  TRACE_EVENT_INSTANT("test", "Event0.1");
  TRACE_EVENT_INSTANT("test", "Event0.2");
  TRACE_EVENT_INSTANT("test", "Event1.1", perfetto::Track::ThreadScoped(&num));
  TRACE_EVENT_INSTANT("test", "Event1.2", perfetto::Track::ThreadScoped(&num));
  std::thread t1([&]() {
    TRACE_EVENT_INSTANT("test", "Event2.1",
                        perfetto::Track::ThreadScoped(&num));
    TRACE_EVENT_INSTANT("test", "Event2.2",
                        perfetto::Track::ThreadScoped(&num));
  });
  t1.join();
  std::thread t2([&]() {
    TRACE_EVENT_INSTANT("test", "Event3.1",
                        perfetto::Track::ThreadScoped(&num));
    TRACE_EVENT_INSTANT("test", "Event3.2",
                        perfetto::Track::ThreadScoped(&num));
  });
  t2.join();

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  std::unordered_map<std::string, uint64_t> track_uuid_map;
  for (auto packet : trace.packet()) {
    if (packet.has_interned_data()) {
      for (auto& ename : packet.interned_data().event_names()) {
        track_uuid_map.emplace(ename.name(), packet.track_event().track_uuid());
      }
    }
  }
  EXPECT_EQ(track_uuid_map.at("Event0.1"), track_uuid_map.at("Event0.2"));
  EXPECT_EQ(track_uuid_map.at("Event1.1"), track_uuid_map.at("Event1.2"));
  EXPECT_EQ(track_uuid_map.at("Event2.1"), track_uuid_map.at("Event2.2"));
  EXPECT_EQ(track_uuid_map.at("Event3.1"), track_uuid_map.at("Event3.2"));

  EXPECT_EQ(4u,
            (std::unordered_set<uint64_t>{
                 track_uuid_map.at("Event0.1"), track_uuid_map.at("Event1.1"),
                 track_uuid_map.at("Event2.1"), track_uuid_map.at("Event3.1")})
                .size());
}

TEST_P(PerfettoApiTest, FilterDebugAnnotations) {
  for (auto flag : {false, true}) {
    // Create a new trace session.
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_filter_debug_annotations(flag);
    auto* tracing_session = NewTraceWithCategories({"test"}, te_cfg);
    tracing_session->get()->StartBlocking();

    TRACE_EVENT_BEGIN("test", "Event1");
    TRACE_EVENT_BEGIN("test", "Event2", [&](perfetto::EventContext ctx) {
      ctx.AddDebugAnnotation("debug_name", "debug_value");
    });
    TRACE_EVENT_BEGIN("test", "Event3");
    auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
    ASSERT_EQ(3u, slices.size());
    if (flag) {
      EXPECT_EQ("B:test.Event2", slices[1]);
    } else {
      EXPECT_EQ("B:test.Event2(debug_name=(string)debug_value)", slices[1]);
    }
  }
}

TEST_P(PerfettoApiTest, TrackEventDebugAnnotations) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  enum MyEnum : uint32_t { ENUM_FOO, ENUM_BAR };
  enum MySignedEnum : int32_t { SIGNED_ENUM_FOO = -1, SIGNED_ENUM_BAR };
  enum class MyClassEnum { VALUE };

  TRACE_EVENT_BEGIN("test", "E", "bool_arg", false);
  TRACE_EVENT_BEGIN("test", "E", "int_arg", -123);
  TRACE_EVENT_BEGIN("test", "E", "uint_arg", 456u);
  TRACE_EVENT_BEGIN("test", "E", "float_arg", 3.14159262f);
  TRACE_EVENT_BEGIN("test", "E", "double_arg", 6.22);
  TRACE_EVENT_BEGIN("test", "E", "str_arg", "hello", "str_arg2",
                    std::string("tracing"), "str_arg3",
                    std::string_view("view"));
  TRACE_EVENT_BEGIN("test", "E", "ptr_arg",
                    reinterpret_cast<void*>(static_cast<intptr_t>(0xbaadf00d)));
  TRACE_EVENT_BEGIN("test", "E", "size_t_arg", size_t{42});
  TRACE_EVENT_BEGIN("test", "E", "ptrdiff_t_arg", ptrdiff_t{-7});
  TRACE_EVENT_BEGIN("test", "E", "enum_arg", ENUM_BAR);
  TRACE_EVENT_BEGIN("test", "E", "signed_enum_arg", SIGNED_ENUM_FOO);
  TRACE_EVENT_BEGIN("test", "E", "class_enum_arg", MyClassEnum::VALUE);
  TRACE_EVENT_BEGIN("test", "E", "traced_value",
                    [&](perfetto::TracedValue context) {
                      std::move(context).WriteInt64(42);
                    });
  TRACE_EVENT_BEGIN("test", "E", [&](perfetto::EventContext ctx) {
    ctx.AddDebugAnnotation("debug_annotation", "value");
  });
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre(
          "B:test.E(bool_arg=(bool)0)", "B:test.E(int_arg=(int)-123)",
          "B:test.E(uint_arg=(uint)456)", "B:test.E(float_arg=(double)3.14159)",
          "B:test.E(double_arg=(double)6.22)",
          "B:test.E(str_arg=(string)hello,str_arg2=(string)tracing,"
          "str_arg3=(string)view)",
          "B:test.E(ptr_arg=(pointer)baadf00d)",
          "B:test.E(size_t_arg=(uint)42)", "B:test.E(ptrdiff_t_arg=(int)-7)",
          "B:test.E(enum_arg=(uint)1)", "B:test.E(signed_enum_arg=(int)-1)",
          "B:test.E(class_enum_arg=(int)0)", "B:test.E(traced_value=(int)42)",
          "B:test.E(debug_annotation=(string)value)"));
}

TEST_P(PerfettoApiTest, TrackEventCustomDebugAnnotations) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  std::unique_ptr<MyDebugAnnotation> owned_annotation(new MyDebugAnnotation());

  TRACE_EVENT_BEGIN("test", "E", "custom_arg", MyDebugAnnotation());
  TRACE_EVENT_BEGIN("test", "E", "normal_arg", "x", "custom_arg",
                    std::move(owned_annotation));
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre(
          R"(B:test.E(custom_arg=(json){"key": 123}))",
          R"(B:test.E(normal_arg=(string)x,custom_arg=(json){"key": 123}))"));
}

TEST_P(PerfettoApiTest, TrackEventCustomRawDebugAnnotations) {
  // Note: this class is also testing a non-moveable and non-copiable argument.
  class MyRawDebugAnnotation : public perfetto::DebugAnnotation {
   public:
    MyRawDebugAnnotation() { msg_->set_string_value("nested_value"); }
    ~MyRawDebugAnnotation() = default;

    // |msg_| already deletes these implicitly, but let's be explicit for safety
    // against future changes.
    MyRawDebugAnnotation(const MyRawDebugAnnotation&) = delete;
    MyRawDebugAnnotation(MyRawDebugAnnotation&&) = delete;

    void Add(perfetto::protos::pbzero::DebugAnnotation* annotation) const {
      auto ranges = msg_.GetRanges();
      annotation->AppendScatteredBytes(
          perfetto::protos::pbzero::DebugAnnotation::kNestedValueFieldNumber,
          &ranges[0], ranges.size());
    }

   private:
    mutable protozero::HeapBuffered<
        perfetto::protos::pbzero::DebugAnnotation::NestedValue>
        msg_;
  };

  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "E", "raw_arg", MyRawDebugAnnotation());
  TRACE_EVENT_BEGIN("test", "E", "plain_arg", 42, "raw_arg",
                    MyRawDebugAnnotation());
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre("B:test.E(raw_arg=(nested)nested_value)",
                  "B:test.E(plain_arg=(int)42,raw_arg=(nested)nested_value)"));
}

TEST_P(PerfettoApiTest, ManyDebugAnnotations) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "E", "arg1", 1, "arg2", 2, "arg3", 3);
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("B:test.E(arg1=(int)1,arg2=(int)2,arg3=(int)3)"));
}

TEST_P(PerfettoApiTest, DebugAnnotationAndLambda) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  enum MyEnum : uint32_t { ENUM_FOO, ENUM_BAR };
  enum MySignedEnum : int32_t { SIGNED_ENUM_FOO = -1, SIGNED_ENUM_BAR };
  enum class MyClassEnum { VALUE };

  TRACE_EVENT_BEGIN(
      "test", "E", "key", "value", [](perfetto::EventContext ctx) {
        ctx.event()->set_log_message()->set_source_location_iid(42);
      });
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool found_args = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_track_event())
      continue;
    const auto& track_event = packet.track_event();
    if (track_event.type() !=
        perfetto::protos::gen::TrackEvent::TYPE_SLICE_BEGIN)
      continue;

    EXPECT_TRUE(track_event.has_log_message());
    const auto& log = track_event.log_message();
    EXPECT_EQ(42u, log.source_location_iid());

    const auto& dbg = track_event.debug_annotations()[0];
    EXPECT_EQ("value", dbg.string_value());

    found_args = true;
  }
  EXPECT_TRUE(found_args);
}

TEST_P(PerfettoApiTest, ProtoInsideDebugAnnotation) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_INSTANT(
      "test", "E", "key",
      [](perfetto::TracedProto<perfetto::protos::pbzero::LogMessage> ctx) {
        ctx->set_source_location_iid(42);
      });

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  std::vector<std::string> interned_debug_annotation_names;
  std::vector<std::string> interned_debug_annotation_proto_type_names;

  bool found_args = false;
  for (const auto& packet : trace.packet()) {
    if (packet.has_interned_data()) {
      for (const auto& interned_name :
           packet.interned_data().debug_annotation_names()) {
        interned_debug_annotation_names.push_back(interned_name.name());
      }
      for (const auto& interned_type_name :
           packet.interned_data().debug_annotation_value_type_names()) {
        interned_debug_annotation_proto_type_names.push_back(
            interned_type_name.name());
      }
    }

    if (!packet.has_track_event())
      continue;
    const auto& track_event = packet.track_event();
    if (track_event.type() != perfetto::protos::gen::TrackEvent::TYPE_INSTANT) {
      continue;
    }

    EXPECT_EQ(track_event.debug_annotations_size(), 1);
    found_args = true;
  }
  // TODO(altimin): Use DebugAnnotationParser here to parse the debug
  // annotations.
  EXPECT_TRUE(found_args);
  EXPECT_THAT(interned_debug_annotation_names,
              testing::UnorderedElementsAre("key"));
  EXPECT_THAT(interned_debug_annotation_proto_type_names,
              testing::UnorderedElementsAre(".perfetto.protos.LogMessage"));
}

TEST_P(PerfettoApiTest, TrackEventComputedName) {
  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // New macros require perfetto::StaticString{} annotation.
  for (int i = 0; i < 3; i++)
    TRACE_EVENT_BEGIN("test", perfetto::StaticString{i % 2 ? "Odd" : "Even"});

  // Legacy macros assume all arguments are static strings.
  for (int i = 0; i < 3; i++)
    TRACE_EVENT_BEGIN0("test", i % 2 ? "Odd" : "Even");

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("B:test.Even", "B:test.Odd", "B:test.Even",
                                  "B:test.Even", "B:test.Odd", "B:test.Even"));
}

TEST_P(PerfettoApiTest, TrackEventEventNameDynamicString) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_BEGIN("foo", perfetto::DynamicString{std::string("Event1")});
  TRACE_EVENT_BEGIN("foo", perfetto::DynamicString{std::string("Event2")});

  TRACE_EVENT0("foo", TRACE_STR_COPY(std::string("Event3")));
  const char* event4 = "Event4";
  TRACE_EVENT0("foo", event4);

  // Ensure that event-name is not emitted in case of `_END` events.
  PERFETTO_INTERNAL_TRACK_EVENT_WITH_METHOD(
      TraceForCategory, "foo", perfetto::DynamicString{std::string("Event5")},
      ::perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END);
  PERFETTO_INTERNAL_TRACK_EVENT_WITH_METHOD(
      TraceForCategory, "foo", perfetto::StaticString{"Event6"},
      ::perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END);

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  ASSERT_EQ(6u, slices.size());
  EXPECT_EQ("B:foo.Event1", slices[0]);
  EXPECT_EQ("B:foo.Event2", slices[1]);
  EXPECT_EQ("B:foo.Event3", slices[2]);
  EXPECT_EQ("B:foo.Event4", slices[3]);
  EXPECT_EQ("E", slices[4]);
  EXPECT_EQ("E", slices[5]);
}

TEST_P(PerfettoApiTest, TrackEventDynamicStringInDebugArgs) {
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT1("foo", "Event1", "arg1",
               TRACE_STR_COPY(std::string("arg1_value1")));
  const char* value2 = "arg1_value2";
  TRACE_EVENT1("foo", "Event2", "arg1", value2);
  const char* value4 = "arg1_value4";
  TRACE_EVENT1("foo", "Event3", "arg1",
               perfetto::DynamicString(std::string("arg1_value3")));
  TRACE_EVENT1("foo", "Event4", "arg1", perfetto::StaticString(value4));

  TRACE_EVENT_BEGIN("foo", "Event5", "arg1",
                    TRACE_STR_COPY(std::string("arg1_value5")));
  TRACE_EVENT_BEGIN("foo", "Event6", "arg1",
                    perfetto::DynamicString(std::string("arg1_value6")));
  const char* value7 = "arg1_value7";
  TRACE_EVENT_BEGIN("foo", "Event7", "arg1", perfetto::StaticString(value7));
  const char* arg_name = "new_arg1";
  TRACE_EVENT_BEGIN("foo", "Event8", perfetto::DynamicString{arg_name}, 5);

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  ASSERT_EQ(8u, slices.size());
  EXPECT_EQ("B:foo.Event1(arg1=(string)arg1_value1)", slices[0]);
  EXPECT_EQ("B:foo.Event2(arg1=(string)arg1_value2)", slices[1]);
  EXPECT_EQ("B:foo.Event3(arg1=(string)arg1_value3)", slices[2]);
  EXPECT_EQ("B:foo.Event4(arg1=(string)arg1_value4)", slices[3]);
  EXPECT_EQ("B:foo.Event5(arg1=(string)arg1_value5)", slices[4]);
  EXPECT_EQ("B:foo.Event6(arg1=(string)arg1_value6)", slices[5]);
  EXPECT_EQ("B:foo.Event7(arg1=(string)arg1_value7)", slices[6]);
  EXPECT_EQ("B:foo.Event8(new_arg1=(int)5)", slices[7]);
}

TEST_P(PerfettoApiTest, TrackEventLegacyNullStringInArgs) {
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  const char* null_str = nullptr;

  TRACE_EVENT1("foo", "Event1", "arg1", null_str);
  TRACE_EVENT1("foo", "Event2", "arg1", TRACE_STR_COPY(null_str));

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  ASSERT_EQ(2u, slices.size());
  EXPECT_EQ("B:foo.Event1(arg1=(string)NULL)", slices[0]);
  EXPECT_EQ("B:foo.Event2(arg1=(string)NULL)", slices[1]);
}

TEST_P(PerfettoApiTest, FilterDynamicEventName) {
  for (auto filter_dynamic_names : {false, true}) {
    // Create a new trace session.
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.set_filter_dynamic_event_names(filter_dynamic_names);
    auto* tracing_session = NewTraceWithCategories({"test"}, te_cfg);
    tracing_session->get()->StartBlocking();

    TRACE_EVENT_BEGIN("test", "Event1");
    TRACE_EVENT_BEGIN("test", perfetto::DynamicString("Event2"));
    const char* event3 = "Event3";
    TRACE_EVENT_BEGIN("test", perfetto::StaticString(event3));
    auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
    ASSERT_EQ(3u, slices.size());
    EXPECT_EQ("B:test.Event1", slices[0]);
    EXPECT_EQ(filter_dynamic_names ? "B:test.FILTERED" : "B:test.Event2",
              slices[1]);
    EXPECT_EQ("B:test.Event3", slices[2]);
  }
}

TEST_P(PerfettoApiTest, TrackEventArgumentsNotEvaluatedWhenDisabled) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  bool called = false;
  auto ArgumentFunction = [&] {
    called = true;
    return 123;
  };

  TRACE_EVENT_BEGIN("test", "DisabledEvent", "arg", ArgumentFunction());
  {
    TRACE_EVENT("test", "DisabledScopedEvent", "arg", ArgumentFunction());
  }
  perfetto::TrackEvent::Flush();

  tracing_session->get()->StopBlocking();
  EXPECT_FALSE(called);

  ArgumentFunction();
  EXPECT_TRUE(called);
}

TEST_P(PerfettoApiTest, TrackEventConfig) {
  auto run_config = [&](perfetto::protos::gen::TrackEventConfig te_cfg,
                        auto check_fcn) {
    perfetto::TraceConfig cfg;
    cfg.set_duration_ms(500);
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");
    ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

    auto* tracing_session = NewTrace(cfg);
    tracing_session->get()->StartBlocking();

    check_fcn();

    TRACE_EVENT_BEGIN("foo", "FooEvent");
    TRACE_EVENT_BEGIN("bar", "BarEvent");
    TRACE_EVENT_BEGIN("foo,bar", "MultiFooBar");
    TRACE_EVENT_BEGIN("baz,bar,quux", "MultiBar");
    TRACE_EVENT_BEGIN("red,green,blue,foo", "MultiFoo");
    TRACE_EVENT_BEGIN("red,green,blue,yellow", "MultiNone");
    TRACE_EVENT_BEGIN("cat", "SlowEvent");
    TRACE_EVENT_BEGIN("cat.verbose", "DebugEvent");
    TRACE_EVENT_BEGIN("test", "TagEvent");
    TRACE_EVENT_BEGIN("test.verbose", "VerboseTagEvent");
    TRACE_EVENT_BEGIN(TRACE_DISABLED_BY_DEFAULT("cat"), "NotDisabledEvent");
    perfetto::DynamicCategory dyn_foo{"dynamic,foo"};
    TRACE_EVENT_BEGIN(dyn_foo, "DynamicGroupFooEvent");
    perfetto::DynamicCategory dyn_bar{"dynamic,bar"};
    TRACE_EVENT_BEGIN(dyn_bar, "DynamicGroupBarEvent");

    auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
    tracing_session->session.reset();
    return slices;
  };

  // Empty config should enable all categories except slow ones.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo,bar"));
      perfetto::DynamicCategory dyn{"dynamic"};
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dyn));
      EXPECT_TRUE(
          TRACE_EVENT_CATEGORY_ENABLED(TRACE_DISABLED_BY_DEFAULT("cat")));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("cat.verbose"));
    });
    EXPECT_THAT(
        slices,
        ElementsAre("B:foo.FooEvent", "B:bar.BarEvent", "B:foo,bar.MultiFooBar",
                    "B:baz,bar,quux.MultiBar", "B:red,green,blue,foo.MultiFoo",
                    "B:red,green,blue,yellow.MultiNone", "B:test.TagEvent",
                    "B:disabled-by-default-cat.NotDisabledEvent",
                    "B:$dynamic,$foo.DynamicGroupFooEvent",
                    "B:$dynamic,$bar.DynamicGroupBarEvent"));
  }

  // Enable exactly one category.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("foo");
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
      perfetto::DynamicCategory dyn{"dynamic"};
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED(dyn));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo,bar"));
    });
    EXPECT_THAT(slices, ElementsAre("B:foo.FooEvent", "B:foo,bar.MultiFooBar",
                                    "B:red,green,blue,foo.MultiFoo",
                                    "B:$dynamic,$foo.DynamicGroupFooEvent"));
  }

  // Enable exactly one dynamic category.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("dynamic");
    auto slices = run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      perfetto::DynamicCategory dyn{"dynamic"};
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dyn));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo,bar"));
      perfetto::DynamicCategory dyn_bar{"dynamic,bar"};
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dyn_bar));
    });
    EXPECT_THAT(slices, ElementsAre("B:$dynamic,$foo.DynamicGroupFooEvent",
                                    "B:$dynamic,$bar.DynamicGroupBarEvent"));
  }

  // Enable two categories.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("foo");
    te_cfg.add_enabled_categories("baz");
    te_cfg.add_enabled_categories("bar");
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("test"));
      perfetto::DynamicCategory dyn{"dynamic"};
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED(dyn));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo,bar"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("baz,bar,quux"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("red,green,blue,foo"));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("red,green,blue,yellow"));
    });
    EXPECT_THAT(
        slices,
        ElementsAre("B:foo.FooEvent", "B:bar.BarEvent", "B:foo,bar.MultiFooBar",
                    "B:baz,bar,quux.MultiBar", "B:red,green,blue,foo.MultiFoo",
                    "B:$dynamic,$foo.DynamicGroupFooEvent",
                    "B:$dynamic,$bar.DynamicGroupBarEvent"));
  }

  // Enabling all categories with a pattern doesn't enable slow ones.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("*");
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(
          TRACE_EVENT_CATEGORY_ENABLED(TRACE_DISABLED_BY_DEFAULT("cat")));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("cat.verbose"));
    });
    EXPECT_THAT(
        slices,
        ElementsAre("B:foo.FooEvent", "B:bar.BarEvent", "B:foo,bar.MultiFooBar",
                    "B:baz,bar,quux.MultiBar", "B:red,green,blue,foo.MultiFoo",
                    "B:red,green,blue,yellow.MultiNone", "B:test.TagEvent",
                    "B:disabled-by-default-cat.NotDisabledEvent",
                    "B:$dynamic,$foo.DynamicGroupFooEvent",
                    "B:$dynamic,$bar.DynamicGroupBarEvent"));
  }

  // Enable with a pattern.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("fo*");
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo,bar"));
    });
    EXPECT_THAT(slices, ElementsAre("B:foo.FooEvent", "B:foo,bar.MultiFooBar",
                                    "B:red,green,blue,foo.MultiFoo",
                                    "B:$dynamic,$foo.DynamicGroupFooEvent"));
  }

  // Enable with a tag.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_tags("tag");
    auto slices = run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("test"));
    });
    EXPECT_THAT(slices, ElementsAre("B:test.TagEvent",
                                    "B:test.verbose.VerboseTagEvent"));
  }

  // Enable just slow categories.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_tags("slow");
    auto slices = run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("cat"));
    });
    EXPECT_THAT(slices, ElementsAre("B:cat.SlowEvent"));
  }

  // Enable all legacy disabled-by-default categories by a pattern
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("disabled-by-default-*");
    auto slices = run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(
          TRACE_EVENT_CATEGORY_ENABLED(TRACE_DISABLED_BY_DEFAULT("cat")));
    });
    EXPECT_THAT(slices,
                ElementsAre("B:disabled-by-default-cat.NotDisabledEvent"));
  }

  // Enable everything including slow/debug categories.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("*");
    te_cfg.add_enabled_tags("slow");
    te_cfg.add_enabled_tags("debug");
    auto slices = run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("cat"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("cat.verbose"));
      EXPECT_TRUE(
          TRACE_EVENT_CATEGORY_ENABLED(TRACE_DISABLED_BY_DEFAULT("cat")));
    });
    EXPECT_THAT(
        slices,
        ElementsAre("B:foo.FooEvent", "B:bar.BarEvent", "B:foo,bar.MultiFooBar",
                    "B:baz,bar,quux.MultiBar", "B:red,green,blue,foo.MultiFoo",
                    "B:red,green,blue,yellow.MultiNone", "B:cat.SlowEvent",
                    "B:cat.verbose.DebugEvent", "B:test.TagEvent",
                    "B:test.verbose.VerboseTagEvent",
                    "B:disabled-by-default-cat.NotDisabledEvent",
                    "B:$dynamic,$foo.DynamicGroupFooEvent",
                    "B:$dynamic,$bar.DynamicGroupBarEvent"));
  }

  // Disable explicit category.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("*");
    te_cfg.add_disabled_categories("foo");
    run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
    });
  }

  // Disable category with a pattern.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("*");
    te_cfg.add_disabled_categories("fo*");
    run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
    });
  }

  // Enable tag and disable category with a pattern.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("slow_*");
    te_cfg.add_disabled_tags("slow");
    run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("slow_category"));
    });
  }

  // Enable tag and disable category explicitly.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("slow_category");
    te_cfg.add_enabled_tags("slow");
    te_cfg.add_disabled_categories("*");
    run_config(te_cfg, []() {
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("slow_category"));
    });
  }

  // Enable tag and disable another.
  {
    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_tags("tag");
    te_cfg.add_disabled_tags("debug");
    te_cfg.add_disabled_categories("*");
    run_config(te_cfg, []() {
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("test"));
      EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("test.verbose"));
    });
  }
}

TEST_P(PerfettoApiTest, OneDataSourceOneEvent) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  ds_cfg->set_legacy_config("test config");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  MockDataSource::Trace([](MockDataSource::TraceContext) {
    FAIL() << "Should not be called because the trace was not started";
  });
  MockDataSource::CallIfEnabled([](uint32_t) {
    FAIL() << "Should not be called because the trace was not started";
  });

  tracing_session->get()->Start();
  data_source->on_setup.Wait();
  EXPECT_EQ(data_source->config.legacy_config(), "test config");
  data_source->on_start.Wait();

  // Emit one trace event.
  std::atomic<int> trace_lambda_calls{0};
  MockDataSource::Trace(
      [&trace_lambda_calls](MockDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(42);
        packet->set_for_testing()->set_str("event 1");
        trace_lambda_calls++;
        packet->Finalize();

        // The SMB scraping logic will skip the last packet because it cannot
        // guarantee it's finalized. Create an empty packet so we get the
        // previous one and this empty one is ignored.
        packet = ctx.NewTracePacket();
      });

  uint32_t active_instances = 0;
  MockDataSource::CallIfEnabled([&active_instances](uint32_t instances) {
    active_instances = instances;
  });
  EXPECT_EQ(1u, active_instances);

  data_source->on_stop.Wait();
  tracing_session->on_stop.Wait();
  EXPECT_EQ(trace_lambda_calls, 1);

  MockDataSource::Trace([](MockDataSource::TraceContext) {
    FAIL() << "Should not be called because the trace is now stopped";
  });
  MockDataSource::CallIfEnabled([](uint32_t) {
    FAIL() << "Should not be called because the trace is now stopped";
  });

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  bool test_packet_found = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_for_testing())
      continue;
    EXPECT_FALSE(test_packet_found);
    EXPECT_EQ(packet.timestamp(), 42U);
    EXPECT_EQ(packet.for_testing().str(), "event 1");
    test_packet_found = true;
  }
  EXPECT_TRUE(test_packet_found);
}

TEST_P(PerfettoApiTest, ReentrantTracing) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->Start();
  data_source->on_start.Wait();

  // Check that only one level of trace lambda calls is allowed.
  std::atomic<int> trace_lambda_calls{0};
  MockDataSource::Trace([&trace_lambda_calls](MockDataSource::TraceContext) {
    trace_lambda_calls++;
    MockDataSource::Trace([&trace_lambda_calls](MockDataSource::TraceContext) {
      trace_lambda_calls++;
    });
  });

  tracing_session->get()->StopBlocking();
  EXPECT_EQ(trace_lambda_calls, 1);
}

TEST_P(PerfettoApiTest, ConsumerFlush) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  ds_cfg->set_legacy_config("test config");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  tracing_session->get()->Start();
  data_source->on_start.Wait();

  MockDataSource::Trace([&](MockDataSource::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_timestamp(42);
    packet->set_for_testing()->set_str("flushed event");
    packet->Finalize();

    // The SMB scraping logic will skip the last packet because it cannot
    // guarantee it's finalized. Create an empty packet so we get the
    // previous one and this empty one is ignored.
    packet = ctx.NewTracePacket();
  });

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  bool test_packet_found = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_for_testing())
      continue;
    EXPECT_FALSE(test_packet_found);
    EXPECT_EQ(packet.timestamp(), 42U);
    EXPECT_EQ(packet.for_testing().str(), "flushed event");
    test_packet_found = true;
  }
  EXPECT_TRUE(test_packet_found);
}

TEST_P(PerfettoApiTest, WithBatching) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  ds_cfg->set_legacy_config("test config");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  tracing_session->get()->Start();
  data_source->on_setup.Wait();
  data_source->on_start.Wait();

  std::stringstream first_large_message;
  for (size_t i = 0; i < 512; i++)
    first_large_message << i << ". Something wicked this way comes. ";
  auto first_large_message_str = first_large_message.str();

  // Emit one trace event before we begin batching.
  MockDataSource::Trace(
      [&first_large_message_str](MockDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(42);
        packet->set_for_testing()->set_str(first_large_message_str);
        packet->Finalize();
      });

  // Simulate the start of a batching cycle by first setting the batching period
  // to a very large value and then force-flushing when we are done writing
  // data.
  ASSERT_TRUE(
      perfetto::test::EnableDirectSMBPatching(/*BackendType=*/GetParam()));
  perfetto::test::SetBatchCommitsDuration(UINT32_MAX,
                                          /*BackendType=*/GetParam());

  std::stringstream second_large_message;
  for (size_t i = 0; i < 512; i++)
    second_large_message << i << ". Something else wicked this way comes. ";
  auto second_large_message_str = second_large_message.str();

  // Emit another trace event.
  MockDataSource::Trace(
      [&second_large_message_str](MockDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(43);
        packet->set_for_testing()->set_str(second_large_message_str);
        packet->Finalize();

        // Simulate the end of the batching cycle.
        ctx.Flush();
      });

  data_source->on_stop.Wait();
  tracing_session->on_stop.Wait();

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  bool test_packet_1_found = false;
  bool test_packet_2_found = false;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_for_testing())
      continue;
    EXPECT_TRUE(packet.timestamp() == 42U || packet.timestamp() == 43U);
    if (packet.timestamp() == 42U) {
      EXPECT_FALSE(test_packet_1_found);
      EXPECT_EQ(packet.for_testing().str(), first_large_message_str);
      test_packet_1_found = true;
    } else {
      EXPECT_FALSE(test_packet_2_found);
      EXPECT_EQ(packet.for_testing().str(), second_large_message_str);
      test_packet_2_found = true;
    }
  }
  EXPECT_TRUE(test_packet_1_found && test_packet_2_found);
}

TEST_P(PerfettoApiTest, BlockingStartAndStop) {
  auto* data_source = &data_sources_["my_data_source"];

  // Register a second data source to get a bit more coverage.
  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("my_data_source2");
  MockDataSource2::Register(dsd, kTestDataSourceArg);
  perfetto::test::SyncProducers();

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source2");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  tracing_session->get()->StartBlocking();
  EXPECT_TRUE(data_source->on_setup.notified());
  EXPECT_TRUE(data_source->on_start.notified());

  tracing_session->get()->StopBlocking();
  EXPECT_TRUE(data_source->on_stop.notified());
  EXPECT_TRUE(tracing_session->on_stop.notified());
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<MockDataSource2>();
}

TEST_P(PerfettoApiTest, BlockingStartAndStopOnEmptySession) {
  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("non_existent_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  tracing_session->get()->StopBlocking();
  EXPECT_TRUE(tracing_session->on_stop.notified());
}

TEST_P(PerfettoApiTest, WriteEventsAfterDeferredStop) {
  auto* data_source = &data_sources_["my_data_source"];
  data_source->handle_stop_asynchronously = true;

  // Setup the trace config and start the tracing session.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // Stop and wait for the producer to have seen the stop event.
  WaitableTestEvent consumer_stop_signal;
  tracing_session->get()->SetOnStopCallback(
      [&consumer_stop_signal] { consumer_stop_signal.Notify(); });
  tracing_session->get()->Stop();
  data_source->on_stop.Wait();

  // At this point tracing should be still allowed because of the
  // HandleStopAsynchronously() call.
  bool lambda_called = false;

  // This usleep is here just to prevent that we accidentally pass the test
  // just by virtue of hitting some race. We should be able to trace up until
  // 5 seconds after seeing the stop when using the deferred stop mechanism.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  MockDataSource::Trace([&lambda_called](MockDataSource::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_for_testing()->set_str("event written after OnStop");
    packet->Finalize();
    ctx.Flush();
    lambda_called = true;
  });
  ASSERT_TRUE(lambda_called);

  // Now call the async stop closure. This acks the stop to the service and
  // disallows further Trace() calls.
  EXPECT_TRUE(data_source->async_stop_closure);
  data_source->async_stop_closure();

  // Wait that the stop is propagated to the consumer.
  consumer_stop_signal.Wait();

  MockDataSource::Trace([](MockDataSource::TraceContext) {
    FAIL() << "Should not be called after the stop is acked";
  });

  // Check the contents of the trace.
  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);
  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  int test_packet_found = 0;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_for_testing())
      continue;
    EXPECT_EQ(packet.for_testing().str(), "event written after OnStop");
    test_packet_found++;
  }
  EXPECT_EQ(test_packet_found, 1);
}

TEST_P(PerfettoApiTest, RepeatedStartAndStop) {
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  for (int i = 0; i < 5; i++) {
    auto* tracing_session = NewTrace(cfg);
    tracing_session->get()->Start();
    std::atomic<bool> stop_called{false};
    tracing_session->get()->SetOnStopCallback(
        [&stop_called] { stop_called = true; });
    tracing_session->get()->StopBlocking();
    EXPECT_TRUE(stop_called);
  }
}

TEST_P(PerfettoApiTest, SetupWithFile) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (GetParam() == perfetto::kSystemBackend)
    GTEST_SKIP() << "write_into_file + system mode is not supported on Windows";
#endif
  auto temp_file = perfetto::test::CreateTempFile();
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  // Write a trace into |fd|.
  auto* tracing_session = NewTrace(cfg, temp_file.fd);
  tracing_session->get()->StartBlocking();
  tracing_session->get()->StopBlocking();
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Check that |fd| didn't get closed.
  EXPECT_EQ(0, fcntl(temp_file.fd, F_GETFD, 0));
#endif
  // Check that the trace got written.
  EXPECT_GT(lseek(temp_file.fd, 0, SEEK_END), 0);
  EXPECT_EQ(0, close(temp_file.fd));
  // Clean up.
  EXPECT_EQ(0, remove(temp_file.path.c_str()));
}

TEST_P(PerfettoApiTest, MultipleRegistrations) {
  // Attempt to register the same data source again.
  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("my_data_source");
  EXPECT_TRUE(MockDataSource::Register(dsd));
  perfetto::test::SyncProducers();

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // Emit one trace event.
  std::atomic<int> trace_lambda_calls{0};
  MockDataSource::Trace([&trace_lambda_calls](MockDataSource::TraceContext) {
    trace_lambda_calls++;
  });

  // Make sure the data source got called only once.
  tracing_session->get()->StopBlocking();
  EXPECT_EQ(trace_lambda_calls, 1);
}

TEST_P(PerfettoApiTest, CustomIncrementalState) {
  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("incr_data_source");
  TestIncrementalDataSource::Register(dsd);
  perfetto::test::SyncProducers();

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("incr_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // First emit a no-op trace event that initializes the incremental state as a
  // side effect.
  TestIncrementalDataSource::Trace(
      [](TestIncrementalDataSource::TraceContext) {});
  EXPECT_TRUE(TestIncrementalState::constructed);

  // Check that the incremental state is carried across trace events.
  TestIncrementalDataSource::Trace(
      [](TestIncrementalDataSource::TraceContext ctx) {
        auto* state = ctx.GetIncrementalState();
        EXPECT_TRUE(state);
        EXPECT_EQ(100, state->count);
        state->count++;
      });

  TestIncrementalDataSource::Trace(
      [](TestIncrementalDataSource::TraceContext ctx) {
        auto* state = ctx.GetIncrementalState();
        EXPECT_EQ(101, state->count);
      });

  // Make sure the incremental state gets cleaned up between sessions.
  tracing_session->get()->StopBlocking();
  tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  TestIncrementalDataSource::Trace(
      [](TestIncrementalDataSource::TraceContext ctx) {
        auto* state = ctx.GetIncrementalState();
        EXPECT_TRUE(TestIncrementalState::destroyed);
        EXPECT_TRUE(state);
        EXPECT_EQ(100, state->count);
      });
  tracing_session->get()->StopBlocking();
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<TestIncrementalDataSource>();
}

const void* const kKey1 = &kKey1;
const void* const kKey2 = &kKey2;

TEST_P(PerfettoApiTest, TrackEventUserData) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();
  perfetto::TrackEventTlsStateUserData* data_1_ptr = nullptr;
  perfetto::TrackEventTlsStateUserData* data_2_ptr = nullptr;

  TRACE_EVENT_BEGIN(
      "foo", "E", [&data_1_ptr, &data_2_ptr](perfetto::EventContext& ctx) {
        EXPECT_EQ(nullptr, ctx.GetTlsUserData(kKey1));
        EXPECT_EQ(nullptr, ctx.GetTlsUserData(kKey2));
        std::unique_ptr<perfetto::TrackEventTlsStateUserData> data_1 =
            std::make_unique<perfetto::TrackEventTlsStateUserData>();
        data_1_ptr = data_1.get();
        std::unique_ptr<perfetto::TrackEventTlsStateUserData> data_2 =
            std::make_unique<perfetto::TrackEventTlsStateUserData>();
        data_2_ptr = data_2.get();
        ctx.SetTlsUserData(kKey1, std::move(data_1));
        ctx.SetTlsUserData(kKey2, std::move(data_2));
        EXPECT_EQ(data_1_ptr, ctx.GetTlsUserData(kKey1));
        EXPECT_EQ(data_2_ptr, ctx.GetTlsUserData(kKey2));
      });
  TRACE_EVENT_END("foo");
  TRACE_EVENT_BEGIN("foo", "F",
                    [&data_1_ptr, &data_2_ptr](perfetto::EventContext& ctx) {
                      EXPECT_EQ(data_1_ptr, ctx.GetTlsUserData(kKey1));
                      EXPECT_EQ(data_2_ptr, ctx.GetTlsUserData(kKey2));
                    });
  TRACE_EVENT_END("foo");

  std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);

  EXPECT_THAT(ReadSlicesFromTrace(raw_trace),
              ElementsAre("B:foo.E", "E", "B:foo.F", "E"));

  // Expect that the TLS User Data is cleared between tracing sessions.
  tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("foo", "E", [](perfetto::EventContext& ctx) {
    EXPECT_EQ(nullptr, ctx.GetTlsUserData(kKey1));
    EXPECT_EQ(nullptr, ctx.GetTlsUserData(kKey2));
  });
  TRACE_EVENT_END("foo");

  raw_trace = StopSessionAndReturnBytes(tracing_session);
  EXPECT_THAT(ReadSlicesFromTrace(raw_trace), ElementsAre("B:foo.E", "E"));
}

TEST_P(PerfettoApiTest, OnFlush) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  tracing_session->get()->StartBlocking();

  WaitableTestEvent producer_on_flush;
  WaitableTestEvent consumer_flush_done;

  data_source->on_flush_callback = [&](perfetto::FlushFlags flush_flags) {
    EXPECT_FALSE(consumer_flush_done.notified());
    EXPECT_EQ(flush_flags.initiator(),
              perfetto::FlushFlags::Initiator::kConsumerSdk);
    EXPECT_EQ(flush_flags.reason(), perfetto::FlushFlags::Reason::kExplicit);
    producer_on_flush.Notify();
    MockDataSource::Trace([](MockDataSource::TraceContext ctx) {
      ctx.NewTracePacket()->set_for_testing()->set_str("on-flush");
      ctx.Flush();
    });
  };

  tracing_session->get()->Flush([&](bool success) {
    EXPECT_TRUE(success);
    EXPECT_TRUE(producer_on_flush.notified());
    consumer_flush_done.Notify();
  });

  producer_on_flush.Wait();
  consumer_flush_done.Wait();

  tracing_session->get()->StopBlocking();

  data_source->on_stop.Wait();

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  EXPECT_THAT(
      trace.packet(),
      Contains(Property(
          &perfetto::protos::gen::TracePacket::for_testing,
          Property(&perfetto::protos::gen::TestEvent::str, "on-flush"))));
}

TEST_P(PerfettoApiTest, OnFlushAsync) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  tracing_session->get()->StartBlocking();

  WaitableTestEvent consumer_flush_done;

  data_source->handle_flush_asynchronously = true;
  data_source->on_flush_callback = [&](perfetto::FlushFlags) {
    EXPECT_FALSE(consumer_flush_done.notified());
  };

  tracing_session->get()->Flush([&](bool success) {
    EXPECT_TRUE(success);
    consumer_flush_done.Notify();
  });

  data_source->on_flush.Wait();
  perfetto::test::SyncProducers();
  EXPECT_FALSE(consumer_flush_done.notified());

  // Finish the flush asynchronously
  MockDataSource::Trace([](MockDataSource::TraceContext ctx) {
    ctx.NewTracePacket()->set_for_testing()->set_str("on-flush");
    ctx.Flush();
  });
  data_source->async_flush_closure();

  consumer_flush_done.Wait();

  tracing_session->get()->StopBlocking();

  data_source->on_stop.Wait();

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  EXPECT_THAT(
      trace.packet(),
      Contains(Property(
          &perfetto::protos::gen::TracePacket::for_testing,
          Property(&perfetto::protos::gen::TestEvent::str, "on-flush"))));
}

// Regression test for b/139110180. Checks that GetDataSourceLocked() can be
// called from OnStart() and OnStop() callbacks without deadlocking.
TEST_P(PerfettoApiTest, GetDataSourceLockedFromCallbacks) {
  auto* data_source = &data_sources_["my_data_source"];

  // Setup the trace config.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(1);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  // Create a new trace session.
  auto* tracing_session = NewTrace(cfg);

  data_source->on_start_callback = [] {
    MockDataSource::Trace([](MockDataSource::TraceContext ctx) {
      ctx.NewTracePacket()->set_for_testing()->set_str("on-start");
      auto ds = ctx.GetDataSourceLocked();
      ASSERT_TRUE(!!ds);
      ctx.NewTracePacket()->set_for_testing()->set_str("on-start-locked");
    });
  };

  data_source->on_stop_callback = [] {
    MockDataSource::Trace([](MockDataSource::TraceContext ctx) {
      ctx.NewTracePacket()->set_for_testing()->set_str("on-stop");
      auto ds = ctx.GetDataSourceLocked();
      ASSERT_TRUE(!!ds);
      ctx.NewTracePacket()->set_for_testing()->set_str("on-stop-locked");
      ctx.Flush();
    });
  };

  tracing_session->get()->Start();
  data_source->on_stop.Wait();
  tracing_session->on_stop.Wait();

  std::vector<char> raw_trace = tracing_session->get()->ReadTraceBlocking();
  ASSERT_GE(raw_trace.size(), 0u);

  perfetto::protos::gen::Trace trace;
  ASSERT_TRUE(trace.ParseFromArray(raw_trace.data(), raw_trace.size()));
  int packets_found = 0;
  for (const auto& packet : trace.packet()) {
    if (!packet.has_for_testing())
      continue;
    packets_found |= packet.for_testing().str() == "on-start" ? 1 : 0;
    packets_found |= packet.for_testing().str() == "on-start-locked" ? 2 : 0;
    packets_found |= packet.for_testing().str() == "on-stop" ? 4 : 0;
    packets_found |= packet.for_testing().str() == "on-stop-locked" ? 8 : 0;
  }
  EXPECT_EQ(packets_found, 1 | 2 | 4 | 8);
}

TEST_P(PerfettoApiTest, OnStartCallback) {
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(60000);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  auto* tracing_session = NewTrace(cfg);

  WaitableTestEvent got_start;
  tracing_session->get()->SetOnStartCallback([&] { got_start.Notify(); });
  tracing_session->get()->Start();
  got_start.Wait();

  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, OnErrorCallback) {
  perfetto::TraceConfig cfg;

  // Requesting too long |duration_ms| will cause EnableTracing() to fail.
  cfg.set_duration_ms(static_cast<uint32_t>(-1));
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  auto* tracing_session = NewTrace(cfg);

  WaitableTestEvent got_error;
  tracing_session->get()->SetOnErrorCallback([&](perfetto::TracingError error) {
    EXPECT_EQ(perfetto::TracingError::kTracingFailed, error.code);
    EXPECT_FALSE(error.message.empty());
    got_error.Notify();
  });

  tracing_session->get()->Start();
  got_error.Wait();

  // Registered error callback will be triggered also by OnDisconnect()
  // function. This may happen after exiting this test what would result in
  // system crash (|got_error| will not exist at that time). To prevent that
  // scenario, error callback has to be cleared.
  tracing_session->get()->SetOnErrorCallback(nullptr);
  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, UnsupportedBackend) {
  // Create a new trace session with an invalid backend type specified.
  // Specifically, the custom backend isn't initialized for these tests.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* tracing_session = NewTrace(cfg, perfetto::BackendType::kCustomBackend);

  // Creating the consumer should cause an asynchronous disconnect error.
  WaitableTestEvent got_error;
  tracing_session->get()->SetOnErrorCallback([&](perfetto::TracingError error) {
    EXPECT_EQ(perfetto::TracingError::kDisconnected, error.code);
    EXPECT_FALSE(error.message.empty());
    got_error.Notify();
  });
  got_error.Wait();

  // Clear the callback for test tear down.
  tracing_session->get()->SetOnErrorCallback(nullptr);
  // Synchronize the consumer channel to ensure the callback has propagated.
  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, ForbiddenConsumer) {
  g_test_tracing_policy->should_allow_consumer_connection = false;

  // Create a new trace session while consumer connections are forbidden.
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* tracing_session = NewTrace(cfg);

  // Creating the consumer should cause an asynchronous disconnect error.
  WaitableTestEvent got_error;
  tracing_session->get()->SetOnErrorCallback([&](perfetto::TracingError error) {
    EXPECT_EQ(perfetto::TracingError::kDisconnected, error.code);
    EXPECT_FALSE(error.message.empty());
    got_error.Notify();
  });
  got_error.Wait();

  // Clear the callback for test tear down.
  tracing_session->get()->SetOnErrorCallback(nullptr);
  // Synchronize the consumer channel to ensure the callback has propagated.
  tracing_session->get()->StopBlocking();

  g_test_tracing_policy->should_allow_consumer_connection = true;
}

TEST_P(PerfettoApiTest, GetTraceStats) {
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  // Asynchronous read.
  WaitableTestEvent got_stats;
  tracing_session->get()->GetTraceStats(
      [&got_stats](perfetto::TracingSession::GetTraceStatsCallbackArgs args) {
        perfetto::protos::gen::TraceStats trace_stats;
        EXPECT_TRUE(args.success);
        EXPECT_TRUE(trace_stats.ParseFromArray(args.trace_stats_data.data(),
                                               args.trace_stats_data.size()));
        EXPECT_EQ(1, trace_stats.buffer_stats_size());
        got_stats.Notify();
      });
  got_stats.Wait();

  // Blocking read.
  auto stats = tracing_session->get()->GetTraceStatsBlocking();
  perfetto::protos::gen::TraceStats trace_stats;
  EXPECT_TRUE(stats.success);
  EXPECT_TRUE(trace_stats.ParseFromArray(stats.trace_stats_data.data(),
                                         stats.trace_stats_data.size()));
  EXPECT_EQ(1, trace_stats.buffer_stats_size());

  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, CustomDataSource) {
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("CustomDataSource");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_timestamp(4200000);
    packet->set_for_testing()->set_str("Test String");
  });
  CustomDataSource::Trace(
      [](CustomDataSource::TraceContext ctx) { ctx.Flush(); });

  tracing_session->get()->StopBlocking();
  auto bytes = tracing_session->get()->ReadTraceBlocking();
  perfetto::protos::gen::Trace parsed_trace;
  EXPECT_TRUE(parsed_trace.ParseFromArray(bytes.data(), bytes.size()));
  bool found_for_testing = false;
  for (auto& packet : parsed_trace.packet()) {
    if (packet.has_for_testing()) {
      EXPECT_FALSE(found_for_testing);
      found_for_testing = true;
      EXPECT_EQ(4200000u, packet.timestamp());
      EXPECT_EQ("Test String", packet.for_testing().str());
    }
  }
  EXPECT_TRUE(found_for_testing);
}

TEST_P(PerfettoApiTest, QueryServiceState) {
  class QueryTestDataSource : public perfetto::DataSource<QueryTestDataSource> {
  };
  RegisterDataSource<QueryTestDataSource>("query_test_data_source");
  perfetto::test::SyncProducers();

  auto tracing_session =
      perfetto::Tracing::NewTrace(/*BackendType=*/GetParam());
  // Asynchronous read.
  WaitableTestEvent got_state;
  tracing_session->QueryServiceState(
      [&got_state](
          perfetto::TracingSession::QueryServiceStateCallbackArgs result) {
        perfetto::protos::gen::TracingServiceState state;
        EXPECT_TRUE(result.success);
        EXPECT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                         result.service_state_data.size()));
        EXPECT_EQ(1, state.producers_size());
        EXPECT_NE(std::string::npos,
                  state.producers()[0].name().find("integrationtest"));
        bool found_ds = false;
        for (const auto& ds : state.data_sources())
          found_ds |= ds.ds_descriptor().name() == "query_test_data_source";
        EXPECT_TRUE(found_ds);
        got_state.Notify();
      });
  got_state.Wait();

  // Blocking read.
  auto result = tracing_session->QueryServiceStateBlocking();
  perfetto::protos::gen::TracingServiceState state;
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                   result.service_state_data.size()));
  EXPECT_EQ(1, state.producers_size());
  EXPECT_NE(std::string::npos,
            state.producers()[0].name().find("integrationtest"));
  bool found_ds = false;
  for (const auto& ds : state.data_sources())
    found_ds |= ds.ds_descriptor().name() == "query_test_data_source";
  EXPECT_TRUE(found_ds);
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<QueryTestDataSource>();
}

TEST_P(PerfettoApiTest, UpdateDataSource) {
  class UpdateTestDataSource
      : public perfetto::DataSource<UpdateTestDataSource> {};

  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("update_test_data_source");

  RegisterDataSource<UpdateTestDataSource>(dsd);

  {
    protozero::HeapBuffered<perfetto::protos::pbzero::TrackEventDescriptor> ted;
    auto cat = ted->add_available_categories();
    cat->set_name("new_cat");
    dsd.set_track_event_descriptor_raw(ted.SerializeAsString());
  }

  UpdateDataSource<UpdateTestDataSource>(dsd);

  perfetto::test::SyncProducers();

  auto tracing_session =
      perfetto::Tracing::NewTrace(/*BackendType=*/GetParam());
  // Blocking read.
  auto result = tracing_session->QueryServiceStateBlocking();
  perfetto::protos::gen::TracingServiceState state;
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                   result.service_state_data.size()));
  EXPECT_EQ(1, state.producers_size());
  EXPECT_NE(std::string::npos,
            state.producers()[0].name().find("integrationtest"));
  bool found_ds = false;
  for (const auto& ds : state.data_sources()) {
    if (ds.ds_descriptor().name() == "update_test_data_source") {
      found_ds = true;
      perfetto::protos::gen::TrackEventDescriptor ted;
      auto desc_raw = ds.ds_descriptor().track_event_descriptor_raw();
      EXPECT_TRUE(ted.ParseFromArray(desc_raw.data(), desc_raw.size()));
      EXPECT_EQ(ted.available_categories_size(), 1);
      EXPECT_EQ(ted.available_categories()[0].name(), "new_cat");
    }
  }
  EXPECT_TRUE(found_ds);
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<UpdateTestDataSource>();
}

TEST_P(PerfettoApiTest, NoFlushFlag) {
  class NoFlushDataSource : public perfetto::DataSource<NoFlushDataSource> {};

  class FlushDataSource : public perfetto::DataSource<FlushDataSource> {
   public:
    void OnFlush(const FlushArgs&) override {}
  };

  perfetto::DataSourceDescriptor dsd_no_flush;
  dsd_no_flush.set_name("no_flush_data_source");
  RegisterDataSource<NoFlushDataSource>(dsd_no_flush);

  perfetto::DataSourceDescriptor dsd_flush;
  dsd_flush.set_name("flush_data_source");
  RegisterDataSource<FlushDataSource>(dsd_flush);

  auto cleanup = MakeCleanup([&] {
    perfetto::test::TracingMuxerImplInternalsForTest::
        ClearDataSourceTlsStateOnReset<FlushDataSource>();
    perfetto::test::TracingMuxerImplInternalsForTest::
        ClearDataSourceTlsStateOnReset<NoFlushDataSource>();
  });

  auto tracing_session = perfetto::Tracing::NewTrace(/*backend=*/GetParam());

  perfetto::test::SyncProducers();

  auto result = tracing_session->QueryServiceStateBlocking();
  perfetto::protos::gen::TracingServiceState state;
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                   result.service_state_data.size()));
  size_t ds_count_no_flush = 0;
  size_t ds_count_flush = 0;
  size_t ds_count_track_event = 0;
  for (const auto& ds : state.data_sources()) {
    if (ds.ds_descriptor().name() == "no_flush_data_source") {
      EXPECT_TRUE(ds.ds_descriptor().no_flush());
      ds_count_no_flush++;
    } else if (ds.ds_descriptor().name() == "flush_data_source") {
      EXPECT_FALSE(ds.ds_descriptor().no_flush());
      ds_count_flush++;
    } else if (ds.ds_descriptor().name() == "track_event") {
      EXPECT_TRUE(ds.ds_descriptor().no_flush());
      ds_count_track_event++;
    }
  }
  EXPECT_EQ(ds_count_no_flush, 1u);
  EXPECT_EQ(ds_count_flush, 1u);
  EXPECT_EQ(ds_count_track_event, 1u);

  dsd_no_flush.set_track_event_descriptor_raw("DESC_NO");
  UpdateDataSource<NoFlushDataSource>(dsd_no_flush);
  dsd_flush.set_track_event_descriptor_raw("DESC_");
  UpdateDataSource<FlushDataSource>(dsd_flush);

  perfetto::test::SyncProducers();

  result = tracing_session->QueryServiceStateBlocking();
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                   result.service_state_data.size()));
  ds_count_no_flush = 0;
  ds_count_flush = 0;
  for (const auto& ds : state.data_sources()) {
    if (ds.ds_descriptor().name() == "no_flush_data_source") {
      EXPECT_TRUE(ds.ds_descriptor().no_flush());
      EXPECT_EQ(ds.ds_descriptor().track_event_descriptor_raw(), "DESC_NO");
      ds_count_no_flush++;
    } else if (ds.ds_descriptor().name() == "flush_data_source") {
      EXPECT_FALSE(ds.ds_descriptor().no_flush());
      EXPECT_EQ(ds.ds_descriptor().track_event_descriptor_raw(), "DESC_");
      ds_count_flush++;
    }
  }
  EXPECT_EQ(ds_count_no_flush, 1u);
  EXPECT_EQ(ds_count_flush, 1u);
}

TEST_P(PerfettoApiTest, LegacyTraceEventsCopyDynamicString) {
  char ptr1[] = "A1";
  char ptr2[] = "B1";
  char arg_name1[] = "C1";
  char arg_name2[] = "D1";
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();
  {
    TRACE_EVENT_MARK_WITH_TIMESTAMP0("cat", ptr1, MyTimestamp{0});
    ptr1[1] = '3';
    // Old value of event name ("A1") is recorded here in trace.
    // The reason being, in legacy macros, event name was expected to be static
    // by default unless `_COPY` version of these macro is used.
    // Perfetto is caching pointer values and if a event-name-pointer matches an
    // existing pointer, it ASSUMES the string-value of new pointer is same as
    // string-value of the cached pointer when it was cached.
    // and hence it assign the same intern-id to second event.
    TRACE_EVENT_MARK_WITH_TIMESTAMP0("cat", ptr1, MyTimestamp{0});
  }
  {
    TRACE_EVENT_COPY_MARK_WITH_TIMESTAMP("cat", ptr2, MyTimestamp{0});
    ptr2[1] = '4';
    TRACE_EVENT_COPY_MARK_WITH_TIMESTAMP("cat", ptr2, MyTimestamp{0});
  }
  {
    TRACE_EVENT_INSTANT1("cat", "event_name", TRACE_EVENT_FLAG_NONE, arg_name1,
                         /*arg_value=*/5);
    arg_name1[1] = '5';
    // Since we don't use the _COPY version here, this event will record the old
    // value of arg_name1 (see earlier comment for full explanation).
    TRACE_EVENT_INSTANT1("cat", "event_name", TRACE_EVENT_FLAG_NONE, arg_name1,
                         /*arg_value=*/5);
  }
  {
    TRACE_EVENT_COPY_INSTANT1("cat", "event_name", TRACE_EVENT_FLAG_NONE,
                              arg_name2, /*arg_value=*/5);
    arg_name2[1] = '6';
    TRACE_EVENT_COPY_INSTANT1("cat", "event_name", TRACE_EVENT_FLAG_NONE,
                              arg_name2, /*arg_value=*/5);
  }
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre("[track=0]Legacy_R:cat.A1", "[track=0]Legacy_R:cat.A1",
                  "[track=0]Legacy_R:cat.B1", "[track=0]Legacy_R:cat.B4",
                  "[track=0]I:cat.event_name(C1=(int)5)",
                  "[track=0]I:cat.event_name(C1=(int)5)",
                  "[track=0]I:cat.event_name(D1=(int)5)",
                  "[track=0]I:cat.event_name(D6=(int)5)"));
}

TEST_P(PerfettoApiTest, LegacyTraceEvents) {
  auto is_new_session = [] {
    bool result;
    TRACE_EVENT_IS_NEW_TRACE(&result);
    return result;
  };

  // Create a new trace session.
  EXPECT_FALSE(is_new_session());
  auto* tracing_session =
      NewTraceWithCategories({"cat", TRACE_DISABLED_BY_DEFAULT("cat")});
  tracing_session->get()->StartBlocking();
  EXPECT_TRUE(is_new_session());
  EXPECT_FALSE(is_new_session());

  // Basic events.
  TRACE_EVENT_INSTANT0("cat", "LegacyEvent", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_BEGIN1("cat", "LegacyEvent", "arg", 123);
  TRACE_EVENT_END2("cat", "LegacyEvent", "arg", "string", "arg2", 0.123f);

  // Scoped event.
  {
    TRACE_EVENT0("cat", "ScopedLegacyEvent");
  }

  // Event with flow (and disabled category).
  TRACE_EVENT_WITH_FLOW0(TRACE_DISABLED_BY_DEFAULT("cat"), "LegacyFlowEvent",
                         0xdadacafe, TRACE_EVENT_FLAG_FLOW_IN);

  // Event with timestamp.
  TRACE_EVENT_INSTANT_WITH_TIMESTAMP0("cat", "LegacyInstantEvent",
                                      TRACE_EVENT_SCOPE_GLOBAL,
                                      MyTimestamp{123456789ul});

  // Event with id.
  TRACE_COUNTER1("cat", "LegacyCounter", 1234);
  TRACE_COUNTER_ID1("cat", "LegacyCounterWithId", 1234, 9000);

  // Metadata event.
  TRACE_EVENT_METADATA1("cat", "LegacyMetadata", "obsolete", true);

  // Async events.
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP_AND_FLAGS0(
      "cat", "LegacyAsync", 5678, MyTimestamp{4}, TRACE_EVENT_FLAG_NONE);
  TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP0("cat", "LegacyAsync", 5678,
                                                 MyTimestamp{5});
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_FLAGS0("cat", "LegacyAsync2", 9000,
                                               TRACE_EVENT_FLAG_NONE);
  TRACE_EVENT_NESTABLE_ASYNC_END_WITH_FLAGS0("cat", "LegacyAsync2", 9000,
                                             TRACE_EVENT_FLAG_NONE);
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_FLAGS0("cat", "LegacyAsync3", 9001,
                                               TRACE_EVENT_FLAG_NONE);
  TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP_AND_FLAGS0(
      "cat", "LegacyAsync3", 9001, MyTimestamp{6}, TRACE_EVENT_FLAG_NONE);

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(
      slices,
      ElementsAre(
          "[track=0]I:cat.LegacyEvent", "B:cat.LegacyEvent(arg=(int)123)",
          "E(arg=(string)string,arg2=(double)0.123)", "B:cat.ScopedLegacyEvent",
          "E",
          "B(bind_id=3671771902)(flow_direction=1):disabled-by-default-cat."
          "LegacyFlowEvent",
          "[track=0]I:cat.LegacyInstantEvent",
          "[track=0]Legacy_C:cat.LegacyCounter(value=(int)1234)",
          "[track=0]Legacy_C(unscoped_id=1234):cat.LegacyCounterWithId(value=("
          "int)9000)",
          "[track=0]Legacy_M:cat.LegacyMetadata",
          "[track=0]Legacy_b(unscoped_id=5678):cat.LegacyAsync",
          "[track=0]Legacy_e(unscoped_id=5678):cat.LegacyAsync",
          "[track=0]Legacy_b(unscoped_id=9000):cat.LegacyAsync2",
          "[track=0]Legacy_e(unscoped_id=9000):cat.LegacyAsync2",
          "[track=0]Legacy_b(unscoped_id=9001):cat.LegacyAsync3",
          "[track=0]Legacy_e(unscoped_id=9001):cat.LegacyAsync3"));
}

TEST_P(PerfettoApiTest, LegacyTraceEventsAndClockSnapshots) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  {
    TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("cat", "LegacyAsync", 5678);

    perfetto::test::TracingMuxerImplInternalsForTest::ClearIncrementalState();

    TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(
        "cat", "LegacyAsyncWithTimestamp", 5678, MyTimestamp{1});
    TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(
        "cat", "LegacyAsyncWithTimestamp", 5678, MyTimestamp{2});

    perfetto::test::TracingMuxerImplInternalsForTest::ClearIncrementalState();

    TRACE_EVENT_NESTABLE_ASYNC_END0("cat", "LegacyAsync", 5678);
  }

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  // Check that clock snapshots are monotonic (per sequence) and don't contain
  // timestamps from trace events with explicit timestamps.
  struct ClockPerSequence {
    uint64_t seq_id = 0;
    uint64_t clock_id = 0;
    bool operator<(const struct ClockPerSequence& other) const {
      return std::tie(seq_id, clock_id) <
             std::tie(other.seq_id, other.clock_id);
    }
  };
  std::map<ClockPerSequence, uint64_t> last_clock_ts;
  for (const auto& packet : trace.packet()) {
    if (packet.has_clock_snapshot()) {
      for (auto& clock : packet.clock_snapshot().clocks()) {
        if (!clock.is_incremental()) {
          uint64_t ts = clock.timestamp();
          ClockPerSequence c;
          c.seq_id = packet.trusted_packet_sequence_id();
          c.clock_id = clock.clock_id();

          uint64_t& last = last_clock_ts[c];

          EXPECT_LE(last, ts)
              << "This sequence:" << c.seq_id << " clock_id:" << c.clock_id;
          last = ts;
        }
      }

      // Events that don't use explicit timestamps should have exactly the same
      // timestamp as in the snapshot (i.e. the relative ts of 0).
      // Here we assume that timestamps are incremental by default.
      if (!packet.has_timestamp_clock_id()) {
        EXPECT_EQ(packet.timestamp(), 0u);
      }
    }
  }
}

TEST_P(PerfettoApiTest, LegacyTraceEventsWithCustomAnnotation) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  MyDebugAnnotation annotation;
  TRACE_EVENT_BEGIN1("cat", "LegacyEvent", "arg", annotation);

  std::unique_ptr<MyDebugAnnotation> owned_annotation(new MyDebugAnnotation());
  TRACE_EVENT_BEGIN1("cat", "LegacyEvent", "arg", std::move(owned_annotation));

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("B:cat.LegacyEvent(arg=(json){\"key\": 123})",
                          "B:cat.LegacyEvent(arg=(json){\"key\": 123})"));
}

TEST_P(PerfettoApiTest, LegacyTraceEventsWithConcurrentSessions) {
  // Make sure that a uniquely owned debug annotation can be written into
  // multiple concurrent tracing sessions.

  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  auto* tracing_session2 = NewTraceWithCategories({"cat"});
  tracing_session2->get()->StartBlocking();

  std::unique_ptr<MyDebugAnnotation> owned_annotation(new MyDebugAnnotation());
  TRACE_EVENT_BEGIN1("cat", "LegacyEvent", "arg", std::move(owned_annotation));

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("B:cat.LegacyEvent(arg=(json){\"key\": 123})"));

  slices = StopSessionAndReadSlicesFromTrace(tracing_session2);
  EXPECT_THAT(slices,
              ElementsAre("B:cat.LegacyEvent(arg=(json){\"key\": 123})"));
}

TEST_P(PerfettoApiTest, LegacyTraceEventsWithId) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_ASYNC_BEGIN0("cat", "UnscopedId", 0x1000);
  TRACE_EVENT_ASYNC_BEGIN0("cat", "LocalId", TRACE_ID_LOCAL(0x2000));
  TRACE_EVENT_ASYNC_BEGIN0("cat", "GlobalId", TRACE_ID_GLOBAL(0x3000));
  TRACE_EVENT_ASYNC_BEGIN0(
      "cat", "WithScope",
      TRACE_ID_WITH_SCOPE("scope string", TRACE_ID_GLOBAL(0x4000)));

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("[track=0]Legacy_S(unscoped_id=4096):cat.UnscopedId",
                          "[track=0]Legacy_S(local_id=8192):cat.LocalId",
                          "[track=0]Legacy_S(global_id=12288):cat.GlobalId",
                          "[track=0]Legacy_S(global_id=16384)(id_scope=\"scope "
                          "string\"):cat.WithScope"));
}

TEST_P(PerfettoApiTest, NestableAsyncTraceEvent) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("cat", "foo",
                                    TRACE_ID_WITH_SCOPE("foo", 1));
  // Same id, different scope.
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("cat", "bar",
                                    TRACE_ID_WITH_SCOPE("bar", 1));
  // Same scope, different id.
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("cat", "bar",
                                    TRACE_ID_WITH_SCOPE("bar", 2));

  TRACE_EVENT_NESTABLE_ASYNC_END0("cat", "bar", TRACE_ID_WITH_SCOPE("bar", 2));
  TRACE_EVENT_NESTABLE_ASYNC_END0("cat", "bar", TRACE_ID_WITH_SCOPE("bar", 1));
  TRACE_EVENT_NESTABLE_ASYNC_END0("cat", "foo", TRACE_ID_WITH_SCOPE("foo", 1));
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  using LegacyEvent = perfetto::protos::gen::TrackEvent::LegacyEvent;
  std::vector<const LegacyEvent*> legacy_events;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_event() && packet.track_event().has_legacy_event()) {
      legacy_events.push_back(&packet.track_event().legacy_event());
    }
  }
  ASSERT_EQ(6u, legacy_events.size());
  EXPECT_EQ("foo", legacy_events[0]->id_scope());
  EXPECT_EQ("bar", legacy_events[1]->id_scope());
  EXPECT_EQ("bar", legacy_events[2]->id_scope());
  EXPECT_EQ("bar", legacy_events[3]->id_scope());
  EXPECT_EQ("bar", legacy_events[4]->id_scope());
  EXPECT_EQ("foo", legacy_events[5]->id_scope());

  EXPECT_EQ(legacy_events[0]->unscoped_id(), legacy_events[5]->unscoped_id());
  EXPECT_EQ(legacy_events[1]->unscoped_id(), legacy_events[4]->unscoped_id());
  EXPECT_EQ(legacy_events[2]->unscoped_id(), legacy_events[3]->unscoped_id());

  EXPECT_NE(legacy_events[0]->unscoped_id(), legacy_events[1]->unscoped_id());
  EXPECT_NE(legacy_events[1]->unscoped_id(), legacy_events[2]->unscoped_id());
  EXPECT_NE(legacy_events[2]->unscoped_id(), legacy_events[0]->unscoped_id());
}

TEST_P(PerfettoApiTest, LegacyTraceEventsWithFlow) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  const uint64_t flow_id = 1234;
  {
    TRACE_EVENT_WITH_FLOW1("cat", "LatencyInfo.Flow", TRACE_ID_GLOBAL(flow_id),
                           TRACE_EVENT_FLAG_FLOW_OUT, "step", "Begin");
  }

  {
    TRACE_EVENT_WITH_FLOW2("cat", "LatencyInfo.Flow", TRACE_ID_GLOBAL(flow_id),
                           TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT,
                           "step", "Middle", "value", false);
  }

  {
    TRACE_EVENT_WITH_FLOW1("cat", "LatencyInfo.Flow", TRACE_ID_GLOBAL(flow_id),
                           TRACE_EVENT_FLAG_FLOW_IN, "step", "End");
  }

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("B(bind_id=1234)(flow_direction=2):cat.LatencyInfo."
                          "Flow(step=(string)Begin)",
                          "E",
                          "B(bind_id=1234)(flow_direction=3):cat.LatencyInfo."
                          "Flow(step=(string)Middle,value=(bool)0)",
                          "E",
                          "B(bind_id=1234)(flow_direction=1):cat.LatencyInfo."
                          "Flow(step=(string)End)",
                          "E"));
}

TEST_P(PerfettoApiTest, LegacyCategoryGroupEnabledState) {
  bool foo_status;
  bool bar_status;
  bool dynamic_status;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("foo", &foo_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("bar", &bar_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("dynamic", &dynamic_status);
  EXPECT_FALSE(foo_status);
  EXPECT_FALSE(bar_status);
  EXPECT_FALSE(dynamic_status);

  const uint8_t* foo_enabled =
      TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED("foo");
  const uint8_t* bar_enabled =
      TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED("bar");
  EXPECT_FALSE(*foo_enabled);
  EXPECT_FALSE(*bar_enabled);

  // The category group enabled pointer can also be retrieved with a
  // runtime-computed category name.
  std::string computed_cat("cat");
  const uint8_t* computed_enabled =
      TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(computed_cat.c_str());
  EXPECT_FALSE(*computed_enabled);

  // The enabled pointers can be converted back to category names.
  EXPECT_EQ("foo", TRACE_EVENT_API_GET_CATEGORY_GROUP_NAME(foo_enabled));
  EXPECT_EQ("bar", TRACE_EVENT_API_GET_CATEGORY_GROUP_NAME(bar_enabled));
  EXPECT_EQ("cat", TRACE_EVENT_API_GET_CATEGORY_GROUP_NAME(computed_enabled));

  auto* tracing_session = NewTraceWithCategories({"foo", "dynamic", "cat"});
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("foo", &foo_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("bar", &bar_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("dynamic", &dynamic_status);
  EXPECT_TRUE(foo_status);
  EXPECT_FALSE(bar_status);
  EXPECT_TRUE(dynamic_status);

  EXPECT_TRUE(*foo_enabled);
  EXPECT_FALSE(*bar_enabled);
  EXPECT_TRUE(*computed_enabled);

  tracing_session->get()->StopBlocking();
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("foo", &foo_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("bar", &bar_status);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("dynamic", &dynamic_status);
  EXPECT_FALSE(foo_status);
  EXPECT_FALSE(bar_status);
  EXPECT_FALSE(dynamic_status);
  EXPECT_FALSE(*foo_enabled);
  EXPECT_FALSE(*bar_enabled);
  EXPECT_FALSE(*computed_enabled);
}

TEST_P(PerfettoApiTest, CategoryEnabledState) {
  perfetto::DynamicCategory dynamic{"dynamic"};
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("red,green,blue,foo"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic_2"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED(dynamic));

  auto* tracing_session = NewTraceWithCategories({"foo", "dynamic"});
  tracing_session->get()->StartBlocking();
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("red,green,blue,foo"));
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("dynamic"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic_2"));
  EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED(dynamic));

  tracing_session->get()->StopBlocking();
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("bar"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("red,green,blue,foo"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED("dynamic_2"));
  EXPECT_FALSE(TRACE_EVENT_CATEGORY_ENABLED(dynamic));
}

class TestInterceptor : public perfetto::Interceptor<TestInterceptor> {
 public:
  static TestInterceptor* instance;

  struct ThreadLocalState : public perfetto::InterceptorBase::ThreadLocalState {
    ThreadLocalState(ThreadLocalStateArgs& args) {
      // Test accessing instance state from the TLS constructor.
      if (auto self = args.GetInterceptorLocked()) {
        self->tls_initialized = true;
      }
    }

    std::map<uint64_t, std::string> event_names;
  };

  TestInterceptor(const std::string& constructor_arg) {
    EXPECT_EQ(constructor_arg, "Constructor argument");
    // Note: some tests in this suite register multiple track event data
    // sources. We only track data for the first in this test.
    if (!instance)
      instance = this;
  }

  ~TestInterceptor() override {
    if (instance != this)
      return;
    instance = nullptr;
    EXPECT_TRUE(setup_called);
    EXPECT_TRUE(start_called);
    EXPECT_TRUE(stop_called);
    EXPECT_TRUE(tls_initialized);
  }

  void OnSetup(const SetupArgs&) override {
    EXPECT_FALSE(setup_called);
    EXPECT_FALSE(start_called);
    EXPECT_FALSE(stop_called);
    setup_called = true;
  }

  void OnStart(const StartArgs&) override {
    EXPECT_TRUE(setup_called);
    EXPECT_FALSE(start_called);
    EXPECT_FALSE(stop_called);
    start_called = true;
  }

  void OnStop(const StopArgs&) override {
    EXPECT_TRUE(setup_called);
    EXPECT_TRUE(start_called);
    EXPECT_FALSE(stop_called);
    stop_called = true;
  }

  static void OnTracePacket(InterceptorContext context) {
    perfetto::protos::pbzero::TracePacket::Decoder packet(
        context.packet_data.data, context.packet_data.size);
    EXPECT_TRUE(packet.trusted_packet_sequence_id() > 0);
    {
      auto self = context.GetInterceptorLocked();
      ASSERT_TRUE(self);
      EXPECT_TRUE(self->setup_called);
      EXPECT_TRUE(self->start_called);
      EXPECT_FALSE(self->stop_called);
      EXPECT_TRUE(self->tls_initialized);
    }

    auto& tls = context.GetThreadLocalState();
    if (packet.sequence_flags() &
        perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED) {
      tls.event_names.clear();
    }
    if (packet.has_interned_data()) {
      perfetto::protos::pbzero::InternedData::Decoder interned_data(
          packet.interned_data());
      for (auto it = interned_data.event_names(); it; it++) {
        perfetto::protos::pbzero::EventName::Decoder entry(*it);
        tls.event_names[entry.iid()] = entry.name().ToStdString();
      }
    }
    if (packet.has_track_event()) {
      perfetto::protos::pbzero::TrackEvent::Decoder track_event(
          packet.track_event());
      uint64_t name_iid = track_event.name_iid();
      auto self = context.GetInterceptorLocked();
      self->events.push_back(tls.event_names[name_iid].c_str());
    }
  }

  bool setup_called = false;
  bool start_called = false;
  bool stop_called = false;
  bool tls_initialized = false;
  std::vector<std::string> events;
};

TestInterceptor* TestInterceptor::instance;

TEST_P(PerfettoApiTest, TracePacketInterception) {
  perfetto::InterceptorDescriptor desc;
  desc.set_name("test_interceptor");
  TestInterceptor::Register(desc, std::string("Constructor argument"));

  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  ds_cfg->mutable_interceptor_config()->set_name("test_interceptor");

  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  EXPECT_EQ(0u, TestInterceptor::instance->events.size());

  // The interceptor should see an event immediately without any explicit
  // flushing.
  TRACE_EVENT_BEGIN("foo", "Hip");
  EXPECT_THAT(TestInterceptor::instance->events, ElementsAre("Hip"));

  // Emit another event with the same title to test interning.
  TRACE_EVENT_BEGIN("foo", "Hip");
  EXPECT_THAT(TestInterceptor::instance->events, ElementsAre("Hip", "Hip"));

  // Emit an event from another thread. It should still reach the same
  // interceptor instance.
  std::thread thread([] { TRACE_EVENT_BEGIN("foo", "Hooray"); });
  thread.join();
  EXPECT_THAT(TestInterceptor::instance->events,
              ElementsAre("Hip", "Hip", "Hooray"));

  // Emit a packet that spans multiple segments and must be stitched together.
  TestInterceptor::instance->events.clear();
  static char long_title[8192];
  memset(long_title, 'a', sizeof(long_title) - 1);
  long_title[sizeof(long_title) - 1] = 0;
  TRACE_EVENT_BEGIN("foo", long_title);
  EXPECT_THAT(TestInterceptor::instance->events, ElementsAre(long_title));

  tracing_session->get()->StopBlocking();
}

void EmitConsoleEvents() {
  TRACE_EVENT_INSTANT("foo", "Instant event");
  TRACE_EVENT("foo", "Scoped event");
  TRACE_EVENT_BEGIN("foo", "Nested event");
  TRACE_EVENT_INSTANT("foo", "Instant event");
  TRACE_EVENT_INSTANT("foo", "Annotated event", "foo", 1, "bar", "hello");
  TRACE_EVENT_END("foo");
  uint64_t async_id = 4004;
  auto track = perfetto::Track(async_id, perfetto::ThreadTrack::Current());
  auto desc = track.Serialize();
  desc.set_name("AsyncTrack");
  perfetto::TrackEvent::SetTrackDescriptor(track, std::move(desc));
  TRACE_EVENT_BEGIN("test", "AsyncEvent", track);

  std::thread thread([&] {
    TRACE_EVENT("foo", "EventFromAnotherThread");
    TRACE_EVENT_INSTANT("foo", "Instant event");
    TRACE_EVENT_END("test", track);
  });
  thread.join();

  TRACE_EVENT_INSTANT(
      "foo", "More annotations", "dict",
      [](perfetto::TracedValue context) {
        auto dict = std::move(context).WriteDictionary();
        dict.Add("key", 123);
      },
      "array",
      [](perfetto::TracedValue context) {
        auto array = std::move(context).WriteArray();
        array.Append("first");
        array.Append("second");
      });
}

TEST_P(PerfettoApiTest, ConsoleInterceptorPrint) {
  perfetto::ConsoleInterceptor::Register();

  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  ds_cfg->mutable_interceptor_config()->set_name("console");

  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  EmitConsoleEvents();
  tracing_session->get()->StopBlocking();
}

TEST_P(PerfettoApiTest, ConsoleInterceptorVerify) {
  perfetto::ConsoleInterceptor::Register();
  auto temp_file = perfetto::test::CreateTempFile();
  perfetto::ConsoleInterceptor::SetOutputFdForTesting(temp_file.fd);

  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");
  ds_cfg->mutable_interceptor_config()->set_name("console");

  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  EmitConsoleEvents();
  tracing_session->get()->StopBlocking();
  perfetto::ConsoleInterceptor::SetOutputFdForTesting(0);

  std::vector<std::string> lines;
  FILE* f = fdopen(temp_file.fd, "r");
  fseek(f, 0u, SEEK_SET);
  std::array<char, 128> line{};
  while (fgets(line.data(), line.size(), f)) {
    // Ignore timestamps and process/thread ids.
    std::string s(line.data() + 28);
    // Filter out durations.
    s = std::regex_replace(s, std::regex(" [+][0-9]*ms"), "");
    lines.push_back(std::move(s));
  }
  fclose(f);
  EXPECT_EQ(0, remove(temp_file.path.c_str()));

  // clang-format off
  std::vector<std::string> golden_lines = {
      "foo   Instant event\n",
      "foo   Scoped event {\n",
      "foo   -  Nested event {\n",
      "foo   -  -  Instant event\n",
      "foo   -  -  Annotated event(foo:1, bar:hello)\n",
      "foo   -  } Nested event\n",
      "test  AsyncEvent {\n",
      "foo   EventFromAnotherThread {\n",
      "foo   -  Instant event\n",
      "test  } AsyncEvent\n",
      "foo   } EventFromAnotherThread\n",
      "foo   -  More annotations(dict:{key:123}, array:[first, second])\n",
      "foo   } Scoped event\n",
  };
  // clang-format on
  EXPECT_THAT(lines, ContainerEq(golden_lines));
}

TEST_P(PerfettoApiTest, TrackEventObserver) {
  class Observer : public perfetto::TrackEventSessionObserver {
   public:
    ~Observer() override = default;

    void OnSetup(const perfetto::DataSourceBase::SetupArgs&) {
      // Since other tests here register multiple track event data sources,
      // ignore all but the first notifications.
      if (setup_called)
        return;
      setup_called = true;
      if (unsubscribe_at_setup)
        perfetto::TrackEvent::RemoveSessionObserver(this);
      // This event isn't recorded in the trace because tracing isn't active yet
      // when OnSetup is called.
      TRACE_EVENT_INSTANT("foo", "OnSetup");
      // However the active tracing categories have already been updated at this
      // point.
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
    }

    void OnStart(const perfetto::DataSourceBase::StartArgs&) {
      if (start_called)
        return;
      start_called = true;
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      TRACE_EVENT_INSTANT("foo", "OnStart");
    }

    void OnStop(const perfetto::DataSourceBase::StopArgs&) {
      if (stop_called)
        return;
      stop_called = true;
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      TRACE_EVENT_INSTANT("foo", "OnStop");
      perfetto::TrackEvent::Flush();
    }

    bool setup_called{};
    bool start_called{};
    bool stop_called{};
    bool unsubscribe_at_setup{};
  };

  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
  {
    Observer observer;
    perfetto::TrackEvent::AddSessionObserver(&observer);

    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    EXPECT_TRUE(observer.setup_called);
    EXPECT_TRUE(observer.start_called);
    tracing_session->get()->StopBlocking();
    EXPECT_TRUE(observer.stop_called);
    perfetto::TrackEvent::RemoveSessionObserver(&observer);
    auto slices = ReadSlicesFromTraceSession(tracing_session->get());
    EXPECT_THAT(slices, ElementsAre("I:foo.OnStart", "I:foo.OnStop"));
  }

  // No notifications after removing observer.
  {
    Observer observer;
    perfetto::TrackEvent::AddSessionObserver(&observer);
    perfetto::TrackEvent::RemoveSessionObserver(&observer);
    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    EXPECT_FALSE(observer.setup_called);
    EXPECT_FALSE(observer.start_called);
    tracing_session->get()->StopBlocking();
    EXPECT_FALSE(observer.stop_called);
  }

  // Removing observer in a callback.
  {
    Observer observer;
    observer.unsubscribe_at_setup = true;
    perfetto::TrackEvent::AddSessionObserver(&observer);
    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    EXPECT_TRUE(observer.setup_called);
    EXPECT_FALSE(observer.start_called);
    tracing_session->get()->StopBlocking();
    EXPECT_FALSE(observer.stop_called);
    perfetto::TrackEvent::RemoveSessionObserver(&observer);
  }

  // Multiple observers.
  {
    Observer observer1;
    Observer observer2;
    perfetto::TrackEvent::AddSessionObserver(&observer1);
    perfetto::TrackEvent::AddSessionObserver(&observer2);
    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    tracing_session->get()->StopBlocking();
    perfetto::TrackEvent::RemoveSessionObserver(&observer1);
    perfetto::TrackEvent::RemoveSessionObserver(&observer2);
    auto slices = ReadSlicesFromTraceSession(tracing_session->get());
    EXPECT_THAT(slices, ElementsAre("I:foo.OnStart", "I:foo.OnStart",
                                    "I:foo.OnStop", "I:foo.OnStop"));
  }

  // Multiple observers with one being removed midway.
  {
    Observer observer1;
    Observer observer2;
    perfetto::TrackEvent::AddSessionObserver(&observer1);
    perfetto::TrackEvent::AddSessionObserver(&observer2);
    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    perfetto::TrackEvent::RemoveSessionObserver(&observer1);
    tracing_session->get()->StopBlocking();
    perfetto::TrackEvent::RemoveSessionObserver(&observer2);
    auto slices = ReadSlicesFromTraceSession(tracing_session->get());
    EXPECT_THAT(slices,
                ElementsAre("I:foo.OnStart", "I:foo.OnStart", "I:foo.OnStop"));
  }
  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
}

TEST_P(PerfettoApiTest, TrackEventObserver_ClearIncrementalState) {
  class Observer : public perfetto::TrackEventSessionObserver {
   public:
    ~Observer() override = default;

    void OnStart(const perfetto::DataSourceBase::StartArgs&) {
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      TRACE_EVENT_INSTANT("foo", "OnStart");
    }

    void WillClearIncrementalState(
        const perfetto::DataSourceBase::ClearIncrementalStateArgs&) {
      if (clear_incremental_state_called)
        return;
      clear_incremental_state_called = true;
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      TRACE_EVENT_INSTANT("foo", "WillClearIncrementalState");
      perfetto::TrackEvent::Flush();
    }

    bool clear_incremental_state_called{};
  };

  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
  {
    Observer observer;
    perfetto::TrackEvent::AddSessionObserver(&observer);

    perfetto::TraceConfig cfg;
    cfg.mutable_incremental_state_config()->set_clear_period_ms(100);
    auto* tracing_session = NewTraceWithCategories({"foo"}, {}, cfg);

    tracing_session->get()->StartBlocking();
    tracing_session->on_stop.Wait();

    EXPECT_TRUE(observer.clear_incremental_state_called);
    perfetto::TrackEvent::RemoveSessionObserver(&observer);
    auto slices = ReadSlicesFromTraceSession(tracing_session->get());
    EXPECT_THAT(slices, ElementsAre("I:foo.OnStart",
                                    "I:foo.WillClearIncrementalState"));
  }
  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
}

TEST_P(PerfettoApiTest, TrackEventObserver_TwoDataSources) {
  class Observer : public perfetto::TrackEventSessionObserver {
   public:
    ~Observer() override = default;

    void OnStart(const perfetto::DataSourceBase::StartArgs&) {
      EXPECT_FALSE(start_called);
      start_called = true;
    }

    bool start_called{};
  };

  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
  EXPECT_FALSE(tracing_module::IsEnabled());

  {
    Observer observer1, observer2;
    perfetto::TrackEvent::AddSessionObserver(&observer1);
    tracing_module::AddSessionObserver(&observer2);

    perfetto::TraceConfig cfg;
    auto* tracing_session = NewTraceWithCategories({"foo"}, {}, cfg);

    tracing_session->get()->StartBlocking();
    tracing_session->on_stop.Wait();

    // The tracing_module hasn't registered its data source yet, so observer2
    // should not be notified.
    EXPECT_TRUE(observer1.start_called);
    EXPECT_FALSE(observer2.start_called);
    perfetto::TrackEvent::RemoveSessionObserver(&observer1);
    tracing_module::RemoveSessionObserver(&observer2);
  }

  tracing_module::InitializeCategories();

  {
    Observer observer1, observer2;
    perfetto::TrackEvent::AddSessionObserver(&observer1);
    tracing_module::AddSessionObserver(&observer2);

    perfetto::TraceConfig cfg;
    auto* tracing_session = NewTraceWithCategories({"foo"}, {}, cfg);

    tracing_session->get()->StartBlocking();
    tracing_session->on_stop.Wait();

    // Each observer should be notified exactly once.
    EXPECT_TRUE(observer1.start_called);
    EXPECT_TRUE(observer2.start_called);
    perfetto::TrackEvent::RemoveSessionObserver(&observer1);
    tracing_module::RemoveSessionObserver(&observer2);
  }

  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
  EXPECT_FALSE(tracing_module::IsEnabled());
}

TEST_P(PerfettoApiTest, TrackEventObserver_AsyncStop) {
  class Observer : public perfetto::TrackEventSessionObserver {
   public:
    ~Observer() override = default;

    void OnStop(const perfetto::DataSourceBase::StopArgs& args) {
      std::lock_guard<std::mutex> lock(mutex_);
      async_stop_closure_ = args.HandleStopAsynchronously();
    }

    void EmitFinalEvents() {
      EXPECT_TRUE(perfetto::TrackEvent::IsEnabled());
      EXPECT_TRUE(TRACE_EVENT_CATEGORY_ENABLED("foo"));
      TRACE_EVENT_INSTANT("foo", "FinalEvent");
      perfetto::TrackEvent::Flush();
      std::function<void()> closure;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        closure = async_stop_closure_;
      }
      closure();
    }

   private:
    std::mutex mutex_;
    std::function<void()> async_stop_closure_;
  };

  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
  {
    Observer observer;
    perfetto::TrackEvent::AddSessionObserver(&observer);

    auto* tracing_session = NewTraceWithCategories({"foo"});
    WaitableTestEvent consumer_stop_signal;
    tracing_session->get()->SetOnStopCallback(
        [&consumer_stop_signal] { consumer_stop_signal.Notify(); });
    tracing_session->get()->StartBlocking();

    // Stop and wait for the producer to have seen the stop event.
    tracing_session->get()->Stop();

    // At this point tracing should be still allowed because of the
    // HandleStopAsynchronously() call. This usleep is here just to prevent that
    // we accidentally pass the test just by virtue of hitting some race. We
    // should be able to trace up until 5 seconds after seeing the stop when
    // using the deferred stop mechanism.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    observer.EmitFinalEvents();

    // Wait that the stop is propagated to the consumer.
    consumer_stop_signal.Wait();

    perfetto::TrackEvent::RemoveSessionObserver(&observer);
    auto slices = ReadSlicesFromTraceSession(tracing_session->get());
    EXPECT_THAT(slices, ElementsAre("I:foo.FinalEvent"));
  }
  EXPECT_FALSE(perfetto::TrackEvent::IsEnabled());
}

#if PERFETTO_BUILDFLAG(PERFETTO_COMPILER_CLANG)
struct __attribute__((capability("mutex"))) MockMutex {
  void Lock() __attribute__((acquire_capability())) {}
  void Unlock() __attribute__((release_capability())) {}
};

struct AnnotatedObject {
  MockMutex mutex;
  int value __attribute__((guarded_by(mutex))) = {};
};

TEST_P(PerfettoApiTest, ThreadSafetyAnnotation) {
  AnnotatedObject obj;

  // Access to the locked field is only allowed while holding the mutex.
  obj.mutex.Lock();
  obj.value = 1;
  obj.mutex.Unlock();

  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  // It should be possible to trace the field while holding the lock.
  obj.mutex.Lock();
  TRACE_EVENT_INSTANT("cat", "Instant", "value", obj.value);
  TRACE_EVENT_INSTANT1("cat", "InstantLegacy", 0, "value", obj.value);
  {
    TRACE_EVENT("cat", "Scoped", "value", obj.value);
  }
  {
    TRACE_EVENT1("cat", "ScopedLegacy", "value", obj.value);
  }
  obj.mutex.Unlock();

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("I:cat.Instant(value=(int)1)",
                                  "[track=0]I:cat.InstantLegacy(value=(int)1)",
                                  "B:cat.Scoped(value=(int)1)", "E",
                                  "B:cat.ScopedLegacy(value=(int)1)", "E"));
}
#endif  // PERFETTO_BUILDFLAG(PERFETTO_COMPILER_CLANG)

TEST_P(PerfettoApiTest, CountersDeltaEncoding) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  // Describe a counter track.
  perfetto::CounterTrack track1 =
      perfetto::CounterTrack("Framerate1", "fps1").set_is_incremental(true);
  // Global tracks can be constructed at build time.
  constexpr perfetto::CounterTrack track2 =
      perfetto::CounterTrack::Global("Framerate2", "fps2")
          .set_is_incremental(true);
  perfetto::CounterTrack track3 = perfetto::CounterTrack("Framerate3", "fps3");

  TRACE_COUNTER("cat", track1, 120);
  TRACE_COUNTER("cat", track2, 1000);
  TRACE_COUNTER("cat", track3, 10009);

  TRACE_COUNTER("cat", track1, 10);
  TRACE_COUNTER("cat", track1, 1200);
  TRACE_COUNTER("cat", track1, 34);

  TRACE_COUNTER("cat", track3, 975);
  TRACE_COUNTER("cat", track2, 449);
  TRACE_COUNTER("cat", track2, 2);

  TRACE_COUNTER("cat", track3, 1091);
  TRACE_COUNTER("cat", track3, 110);
  TRACE_COUNTER("cat", track3, 1081);

  TRACE_COUNTER("cat", track1, 98);
  TRACE_COUNTER("cat", track2, 1084);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  std::unordered_map<uint64_t, std::string> counter_names;
  // Map(Counter name -> counter values)
  std::unordered_map<std::string, std::vector<int64_t>> values;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_descriptor()) {
      auto& desc = packet.track_descriptor();
      if (!desc.has_counter())
        continue;
      counter_names[desc.uuid()] =
          desc.has_name() ? desc.name() : desc.static_name();
      EXPECT_EQ((desc.static_name() != "Framerate3"),
                desc.counter().is_incremental());
    }
    if (packet.has_track_event()) {
      auto event = packet.track_event();
      EXPECT_EQ(perfetto::protos::gen::TrackEvent_Type_TYPE_COUNTER,
                event.type());
      auto& counter_name = counter_names.at(event.track_uuid());
      values[counter_name].push_back(event.counter_value());
    }
  }
  ASSERT_EQ(3u, values.size());
  using IntVector = std::vector<int64_t>;
  EXPECT_EQ((IntVector{120, -110, 1190, -1166, 64}), values.at("Framerate1"));
  EXPECT_EQ((IntVector{1000, -551, -447, 1082}), values.at("Framerate2"));
  EXPECT_EQ((IntVector{10009, 975, 1091, 110, 1081}), values.at("Framerate3"));
}

TEST_P(PerfettoApiTest, Counters) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  // Describe a counter track.
  perfetto::CounterTrack fps_track = perfetto::CounterTrack("Framerate", "fps");

  // Emit an integer sample.
  TRACE_COUNTER("cat", fps_track, 120);

  // Global tracks can be constructed at build time.
  constexpr auto goats_track =
      perfetto::CounterTrack::Global("Goats teleported", "goats x 1000")
          .set_unit_multiplier(1000);
  static_assert(goats_track.uuid == 0x6072fc234f82df11,
                "Counter track uuid mismatch");

  // Emit some floating point samples.
  TRACE_COUNTER("cat", goats_track, 0.25);
  TRACE_COUNTER("cat", goats_track, 0.5);
  TRACE_COUNTER("cat", goats_track, 0.75);

  // Emit a sample using an inline track name.
  TRACE_COUNTER("cat", "Voltage", 220);

  // Emit sample with a custom timestamp.
  TRACE_COUNTER("cat",
                perfetto::CounterTrack("Power", "GW").set_category("dmc"),
                MyTimestamp(1985u), 1.21f);
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  std::map<uint64_t, std::string> counter_names;
  std::vector<std::string> counter_samples;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_event()) {
      auto event = packet.track_event();
      EXPECT_EQ(perfetto::protos::gen::TrackEvent_Type_TYPE_COUNTER,
                event.type());
      std::stringstream sample;
      std::string counter_name = counter_names[event.track_uuid()];
      sample << counter_name << " = ";
      if (event.has_counter_value()) {
        sample << event.counter_value();
      } else if (event.has_double_counter_value()) {
        sample << event.double_counter_value();
      }
      if (counter_name == "Power") {
        EXPECT_EQ(1985u, packet.timestamp());
      }
      counter_samples.push_back(sample.str());
    }

    if (!packet.has_track_descriptor() ||
        !packet.track_descriptor().has_counter()) {
      continue;
    }
    auto desc = packet.track_descriptor();
    counter_names[desc.uuid()] =
        desc.has_name() ? desc.name() : desc.static_name();
    if (desc.name() == "Framerate") {
      EXPECT_EQ("fps", desc.counter().unit_name());
    } else if (desc.name() == "Goats teleported") {
      EXPECT_EQ("goats x 1000", desc.counter().unit_name());
      EXPECT_EQ(1000, desc.counter().unit_multiplier());
    } else if (desc.name() == "Power") {
      EXPECT_EQ("GW", desc.counter().unit_name());
      EXPECT_EQ("dmc", desc.counter().categories()[0]);
    }
  }
  EXPECT_EQ(4u, counter_names.size());
  EXPECT_THAT(counter_samples,
              ElementsAre("Framerate = 120", "Goats teleported = 0.25",
                          "Goats teleported = 0.5", "Goats teleported = 0.75",
                          "Voltage = 220", "Power = 1.21"));
}

TEST_P(PerfettoApiTest, CounterTrackUuid) {
  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();

  perfetto::CounterTrack track1 = perfetto::CounterTrack("MyCustomCounter", 1);
  perfetto::CounterTrack track2 = perfetto::CounterTrack("MyCustomCounter", 2);

  TRACE_COUNTER("cat", track1, 1);
  TRACE_COUNTER("cat", track2, 2);

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);

  std::map<uint64_t, size_t> counter_tracks;
  std::map<uint64_t, size_t> counter_events;
  for (const auto& packet : trace.packet()) {
    if (packet.has_track_event()) {
      auto track_event = packet.track_event();
      EXPECT_EQ(perfetto::protos::gen::TrackEvent_Type_TYPE_COUNTER,
                track_event.type());
      ++counter_events[track_event.track_uuid()];
    }
    if (packet.has_track_descriptor() &&
        packet.track_descriptor().has_counter()) {
      auto desc = packet.track_descriptor();
      EXPECT_EQ("MyCustomCounter", desc.static_name());
      ++counter_tracks[desc.uuid()];
    }
  }
  ASSERT_EQ(counter_events.size(), 2U);
  ASSERT_EQ(counter_tracks.size(), 2U);
  for (auto track : counter_tracks) {
    ASSERT_EQ(counter_events.count(track.first), 1U);
    EXPECT_EQ(counter_events.at(track.first), 1U);
    EXPECT_EQ(track.second, 1U);
  }
}

TEST_P(PerfettoApiTest, ScrapingTrackEventBegin) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");

  // Stop tracing but don't flush. Rely on scraping to get the chunk contents.
  tracing_session->get()->StopBlocking();

  auto slices = ReadSlicesFromTraceSession(tracing_session->get());

  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
}

TEST_P(PerfettoApiTest, ScrapingTrackEventEnd) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");
  TRACE_EVENT_END("test");

  // Stop tracing but don't flush. Rely on scraping to get the chunk contents.
  tracing_session->get()->StopBlocking();

  auto slices = ReadSlicesFromTraceSession(tracing_session->get());

  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent", "E"));
}

TEST_P(PerfettoApiTest, EmptyEvent) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");

  // An empty event will allow the previous track packet to be scraped.
  PERFETTO_INTERNAL_ADD_EMPTY_EVENT();

  // Stop tracing but don't flush. Rely on scraping to get the chunk contents.
  tracing_session->get()->StopBlocking();

  auto slices = ReadSlicesFromTraceSession(tracing_session->get());

  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
}

TEST_P(PerfettoApiTest, ConsecutiveEmptyEventsSkipped) {
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");

  // Emit many empty events that wouldn't fit into one chunk.
  constexpr int kNumEvents = 10000;
  for (int i = 0; i < kNumEvents; ++i) {
    PERFETTO_INTERNAL_ADD_EMPTY_EVENT();
  }
  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  auto it = std::find_if(trace.packet().begin(), trace.packet().end(),
                         [](const perfetto::protos::gen::TracePacket& packet) {
                           return packet.has_trace_stats();
                         });
  EXPECT_NE(it, trace.packet().end());
  // Extra empty events should be skipped so only one chunk should be allocated.
  EXPECT_EQ(it->trace_stats().buffer_stats()[0].chunks_read(), 1u);
}

// Make sure that we set correct track_uuid for legacy events
// of type TrackEvent::TYPE_UNSPECIFIED.
// For such events we set fields of `track_event.legacy_event` and
// we set `track_event.track_uuid` to zero to dissociate it with
// default track.
TEST_P(PerfettoApiTest, CorrectTrackUUIDForLegacyEvents) {
  auto* tracing_session = NewTraceWithCategories({"cat"});
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("cat", "foo",
                                    TRACE_ID_WITH_SCOPE("foo", 1));

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices,
              ElementsAre("[track=0]Legacy_b(unscoped_id=11250026935264495724)("
                          "id_scope=\"foo\"):cat.foo"));
}

TEST_P(PerfettoApiTest, ActivateTriggers) {
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  perfetto::TraceConfig::TriggerConfig* tr_cfg = cfg.mutable_trigger_config();
  tr_cfg->set_trigger_mode(perfetto::TraceConfig::TriggerConfig::STOP_TRACING);
  tr_cfg->set_trigger_timeout_ms(5000);
  perfetto::TraceConfig::TriggerConfig::Trigger* trigger =
      tr_cfg->add_triggers();
  trigger->set_name("trigger1");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  perfetto::Tracing::ActivateTriggers({"trigger2", "trigger1"}, 10000);

  tracing_session->on_stop.Wait();

  std::vector<char> bytes = tracing_session->get()->ReadTraceBlocking();
  perfetto::protos::gen::Trace parsed_trace;
  ASSERT_TRUE(parsed_trace.ParseFromArray(bytes.data(), bytes.size()));
  EXPECT_THAT(
      parsed_trace,
      Property(&perfetto::protos::gen::Trace::packet,
               Contains(Property(
                   &perfetto::protos::gen::TracePacket::trigger,
                   Property(&perfetto::protos::gen::Trigger::trigger_name,
                            "trigger1")))));
}

TEST_P(PerfettoApiTest, StartTracingWhileExecutingTracepoint) {
  perfetto::TraceConfig cfg;
  auto* buffer = cfg.add_buffers();
  buffer->set_size_kb(64);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");

  std::atomic<bool> quit{false};
  WaitableTestEvent outside_tracing;
  WaitableTestEvent tracing;
  std::thread t([&] {
    while (!quit) {
      MockDataSource::Trace([&](MockDataSource::TraceContext ctx) {
        {
          auto packet = ctx.NewTracePacket();
          packet->set_for_testing()->set_str("My String");
        }
        {
          auto packet = ctx.NewTracePacket();
        }
        tracing.Notify();
      });
      outside_tracing.Notify();
      std::this_thread::yield();
    }
  });
  outside_tracing.Wait();

  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  tracing.Wait();
  tracing_session->get()->StopBlocking();

  // The data source instance should be stopped.
  auto* data_source = &data_sources_["my_data_source"];
  data_source->on_stop.Wait();

  quit = true;
  t.join();

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  std::vector<std::string> test_strings;
  for (auto& trace_packet : trace.packet()) {
    if (trace_packet.has_for_testing()) {
      test_strings.push_back(trace_packet.for_testing().str());
    }
  }
  EXPECT_THAT(test_strings, AllOf(Not(IsEmpty()), Each("My String")));
}

TEST_P(PerfettoApiTest, SystemDisconnect) {
  if (GetParam() != perfetto::kSystemBackend) {
    GTEST_SKIP();
  }
  auto* data_source = &data_sources_["my_data_source"];
  data_source->handle_stop_asynchronously = true;

  perfetto::TraceConfig cfg;
  auto* buffer = cfg.add_buffers();
  buffer->set_size_kb(64);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  std::atomic<bool> quit1{false};
  WaitableTestEvent tracing1;
  std::atomic<bool> quit2{false};
  WaitableTestEvent tracing2;
  std::thread t([&] {
    while (!quit1) {
      MockDataSource::Trace(
          [&](MockDataSource::TraceContext) { tracing1.Notify(); });
      std::this_thread::yield();
    }
    while (!quit2) {
      MockDataSource::Trace([&](MockDataSource::TraceContext ctx) {
        {
          auto packet = ctx.NewTracePacket();
          packet->set_for_testing()->set_str("New session");
        }
        {
          auto packet = ctx.NewTracePacket();
        }
        tracing2.Notify();
      });
      std::this_thread::yield();
    }
  });
  auto cleanup = MakeCleanup([&] {
    if (t.joinable()) {
      quit1 = true;
      quit2 = true;
      t.join();
    }
  });
  tracing1.Wait();

  // Restarts the system service. This will cause the producer and consumer to
  // disconnect.
  system_service_.Restart();

  // The data source instance should be stopped.
  data_source->on_stop.Wait();

  // The stop is not finalized yet. Test that creating a new trace writer
  // doesn't cause any problem.
  MockDataSource::Trace([&](MockDataSource::TraceContext ctx) {
    {
      auto packet = ctx.NewTracePacket();
      packet->set_for_testing()->set_str("Content");
    }
    {
      auto packet = ctx.NewTracePacket();
    }
  });

  data_source->async_stop_closure();

  tracing_session->on_stop.Wait();

  std::unique_ptr<perfetto::TracingSession> new_session =
      perfetto::Tracing::NewTrace(/*backend=*/GetParam());
  // Wait for reconnection
  ASSERT_TRUE(WaitForOneProducerConnected(new_session.get()));

  auto* tracing_session2 = NewTrace(cfg);
  tracing_session2->get()->StartBlocking();

  quit1 = true;
  tracing2.Wait();
  quit2 = true;
  t.join();

  data_source->handle_stop_asynchronously = false;

  auto trace = StopSessionAndReturnParsedTrace(tracing_session2);
  std::vector<std::string> test_strings;
  for (auto& trace_packet : trace.packet()) {
    if (trace_packet.has_for_testing()) {
      test_strings.push_back(trace_packet.for_testing().str());
    }
  }
  EXPECT_THAT(test_strings, AllOf(Not(IsEmpty()), Each("New session")));
}

TEST_P(PerfettoApiTest, SystemDisconnectAsyncOnStopNoTracing) {
  if (GetParam() != perfetto::kSystemBackend) {
    GTEST_SKIP();
  }
  auto* data_source = &data_sources_["my_data_source"];
  data_source->handle_stop_asynchronously = true;

  perfetto::TraceConfig cfg;
  auto* buffer = cfg.add_buffers();
  buffer->set_size_kb(64);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  std::atomic<bool> quit1{false};
  WaitableTestEvent tracing1;
  std::thread t([&] {
    while (!quit1) {
      MockDataSource::Trace(
          [&](MockDataSource::TraceContext) { tracing1.Notify(); });
      std::this_thread::yield();
    }
  });
  auto cleanup = MakeCleanup([&] {
    if (t.joinable()) {
      quit1 = true;
      t.join();
    }
  });
  tracing1.Wait();

  // Restarts the system service. This will cause the producer and consumer to
  // disconnect.
  system_service_.Restart();

  // The data source instance should be stopped. Don't acknowledge the stop yet.
  data_source->on_stop.Wait();

  tracing_session->on_stop.Wait();

  std::unique_ptr<perfetto::TracingSession> new_session =
      perfetto::Tracing::NewTrace(/*backend=*/GetParam());
  // Wait for reconnection
  ASSERT_TRUE(WaitForOneProducerConnected(new_session.get()));

  data_source->async_stop_closure();

  data_source->handle_stop_asynchronously = false;
}

TEST_P(PerfettoApiTest, SystemDisconnectAsyncOnStopRestartTracing) {
  if (GetParam() != perfetto::kSystemBackend) {
    GTEST_SKIP();
  }
  auto* data_source = &data_sources_["my_data_source"];
  data_source->handle_stop_asynchronously = true;

  perfetto::TraceConfig cfg;
  auto* buffer = cfg.add_buffers();
  buffer->set_size_kb(64);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();

  std::atomic<bool> quit1{false};
  WaitableTestEvent tracing1;
  std::atomic<bool> quit2{false};
  WaitableTestEvent tracing2;
  std::thread t([&] {
    while (!quit1) {
      MockDataSource::Trace(
          [&](MockDataSource::TraceContext) { tracing1.Notify(); });
      std::this_thread::yield();
    }
    while (!quit2) {
      MockDataSource::Trace([&](MockDataSource::TraceContext ctx) {
        {
          auto packet = ctx.NewTracePacket();
          packet->set_for_testing()->set_str("New session");
        }
        {
          auto packet = ctx.NewTracePacket();
        }
        tracing2.Notify();
      });
      std::this_thread::yield();
    }
  });
  auto cleanup = MakeCleanup([&] {
    if (t.joinable()) {
      quit1 = true;
      quit2 = true;
      t.join();
    }
  });
  tracing1.Wait();

  // Restarts the system service. This will cause the producer and consumer to
  // disconnect.
  system_service_.Restart();

  // The data source instance should be stopped. Don't acknowledge the stop yet.
  data_source->on_stop.Wait();

  tracing_session->on_stop.Wait();

  std::unique_ptr<perfetto::TracingSession> new_session =
      perfetto::Tracing::NewTrace(/*backend=*/GetParam());
  // Wait for reconnection
  ASSERT_TRUE(WaitForOneProducerConnected(new_session.get()));

  auto* tracing_session2 = NewTrace(cfg);
  tracing_session2->get()->StartBlocking();

  data_source->async_stop_closure();

  quit1 = true;
  tracing2.Wait();
  quit2 = true;
  t.join();

  data_source->handle_stop_asynchronously = false;

  auto trace = StopSessionAndReturnParsedTrace(tracing_session2);
  std::vector<std::string> test_strings;
  for (auto& trace_packet : trace.packet()) {
    if (trace_packet.has_for_testing()) {
      test_strings.push_back(trace_packet.for_testing().str());
    }
  }
  EXPECT_THAT(test_strings, AllOf(Not(IsEmpty()), Each("New session")));
}

TEST_P(PerfettoApiTest, SystemDisconnectWhileStopping) {
  if (GetParam() != perfetto::kSystemBackend) {
    GTEST_SKIP();
  }
  auto* data_source = &data_sources_["my_data_source"];
  data_source->handle_stop_asynchronously = true;

  perfetto::TraceConfig cfg;
  auto* buffer = cfg.add_buffers();
  buffer->set_size_kb(64);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("my_data_source");
  auto* tracing_session = NewTrace(cfg);
  tracing_session->get()->StartBlocking();
  data_source->on_start.Wait();

  // Stop the session and wait until DataSource::OnStop is called. Don't
  // complete the async stop yet.
  tracing_session->get()->Stop();
  data_source->on_stop.Wait();

  // Restart the service. This should not call DataSource::OnStop again while
  // another async stop is in progress.
  system_service_.Restart();
  tracing_session->on_stop.Wait();

  data_source->async_stop_closure();
  data_source->handle_stop_asynchronously = false;
}

TEST_P(PerfettoApiTest, CloneSession) {
  perfetto::TraceConfig cfg;
  cfg.set_unique_session_name("test_session");
  auto* tracing_session = NewTraceWithCategories({"test"}, {}, cfg);
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "TestEvent");
  TRACE_EVENT_END("test");

  sessions_.emplace_back();
  TestTracingSessionHandle* other_tracing_session = &sessions_.back();
  other_tracing_session->session =
      perfetto::Tracing::NewTrace(/*backend_type=*/GetParam());

  WaitableTestEvent session_cloned;
  other_tracing_session->get()->CloneTrace(
      {"test_session"}, [&](perfetto::TracingSession::CloneTraceCallbackArgs) {
        session_cloned.Notify();
      });
  session_cloned.Wait();

  {
    std::vector<char> raw_trace =
        other_tracing_session->get()->ReadTraceBlocking();
    std::string trace(raw_trace.data(), raw_trace.size());
    EXPECT_THAT(trace, HasSubstr("TestEvent"));
  }

  {
    std::vector<char> raw_trace = StopSessionAndReturnBytes(tracing_session);
    std::string trace(raw_trace.data(), raw_trace.size());
    EXPECT_THAT(trace, HasSubstr("TestEvent"));
  }
}

class PerfettoStartupTracingApiTest : public PerfettoApiTest {
 public:
  using SetupStartupTracingOpts = perfetto::Tracing::SetupStartupTracingOpts;
  void SetupStartupTracing(perfetto::TraceConfig cfg = {},
                           SetupStartupTracingOpts opts = {}) {
    // Setup startup tracing in the current process.
    cfg.set_duration_ms(500);
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");
    te_cfg.add_enabled_categories("test");
    ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

    opts.backend = GetParam();
    session_ =
        perfetto::Tracing::SetupStartupTracingBlocking(cfg, std::move(opts));
    EXPECT_EQ(TRACE_EVENT_CATEGORY_ENABLED("test"), true);
  }

  void AbortStartupTracing() {
    session_->AbortBlocking();
    session_.reset();
  }

  void TearDown() override {
    if (session_) {
      AbortStartupTracing();
    }
    // We need to sync producer because when we start StartupTracing, the
    // producer is disconnected to reconnect again. Note that
    // `SetupStartupTracingBlocking` returns right after data sources are
    // started. `SetupStartupTracingBlocking` doesn't wait for reconnection
    // to succeed before returning. Hence we need to wait for reconnection here
    // because `TracingMuxerImpl::ResetForTesting` will destroy the
    // producer if it find it is not connected to service. Which is problematic
    // because when reconnection happens (via service transport), it will be
    // referencing a deleted producer, which will lead to crash.
    perfetto::test::SyncProducers();
    this->PerfettoApiTest::TearDown();
  }

 protected:
  std::unique_ptr<perfetto::StartupTracingSession> session_;
};

// Test `SetupStartupTracing` API (non block version).
TEST_P(PerfettoStartupTracingApiTest, NonBlockingAPI) {
  // Setup startup tracing in the current process.
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_disabled_categories("*");
  te_cfg.add_enabled_categories("test");
  ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

  SetupStartupTracingOpts opts;
  opts.backend = GetParam();
  session_ = perfetto::Tracing::SetupStartupTracing(cfg, std::move(opts));
  // We need perfetto::test::SyncProducers() to Round-trip to ensure that the
  // muxer has enabled startup tracing.
  perfetto::test::SyncProducers();
  EXPECT_EQ(TRACE_EVENT_CATEGORY_ENABLED("test"), true);

  TRACE_EVENT_BEGIN("test", "Event");

  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  // Emit another event after starting.
  TRACE_EVENT_END("test");
  // Both events should be retained.
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("B:test.Event", "E"));
}

// Test that a startup tracing session will be adopted even when the config
// is not exactly identical (but still compatible).
TEST_P(PerfettoStartupTracingApiTest, CompatibleConfig) {
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_disabled_categories("*");
  te_cfg.add_enabled_categories("foo");
  te_cfg.add_enabled_categories("bar");
  ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

  SetupStartupTracingOpts opts;
  opts.backend = GetParam();
  session_ = perfetto::Tracing::SetupStartupTracing(cfg, std::move(opts));
  // We need perfetto::test::SyncProducers() to Round-trip to ensure that the
  // muxer has enabled startup tracing.
  perfetto::test::SyncProducers();

  TRACE_EVENT_BEGIN("foo", "Event");

  // Note the different order of categories. The config is essentially the same,
  // but is not byte-by-byte identical.
  auto* tracing_session = NewTraceWithCategories({"bar", "foo"});
  tracing_session->get()->StartBlocking();

  // Emit another event after starting.
  TRACE_EVENT_END("foo");

  // Both events should be retained.
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("B:foo.Event", "E"));
}

// Test that a startup tracing session won't be adopted when the config is not
// compatible (in this case, the privacy setting is different).
TEST_P(PerfettoStartupTracingApiTest, IncompatibleConfig) {
  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_disabled_categories("*");
  te_cfg.add_enabled_categories("foo");
  te_cfg.set_filter_debug_annotations(true);
  ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

  SetupStartupTracingOpts opts;
  opts.backend = GetParam();
  session_ = perfetto::Tracing::SetupStartupTracing(cfg, std::move(opts));
  // We need perfetto::test::SyncProducers() to Round-trip to ensure that the
  // muxer has enabled startup tracing.
  perfetto::test::SyncProducers();

  TRACE_EVENT_BEGIN("foo", "Event");

  // This config will have |filter_debug_annotations| set to false.
  auto* tracing_session = NewTraceWithCategories({"foo"});
  tracing_session->get()->StartBlocking();

  // Emit another event after starting.
  TRACE_EVENT_END("foo");

  // The startup session should not be adopted, so we should only see the end
  // event.
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("E"));
}

TEST_P(PerfettoStartupTracingApiTest, WithExistingSmb) {
  {
    // Start and tear down a first session, just to set up the SMB.
    auto* tracing_session = NewTraceWithCategories({"foo"});
    tracing_session->get()->StartBlocking();
    tracing_session->get()->StopBlocking();
  }

  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "Event");

  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  // Emit another event after starting.
  TRACE_EVENT_END("test");

  // Both events should be retained.
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("B:test.Event", "E"));
}

TEST_P(PerfettoStartupTracingApiTest, WithProducerProvidedSmb) {
  ASSERT_FALSE(perfetto::test::TracingMuxerImplInternalsForTest::
                   DoesSystemBackendHaveSMB());
  // The backend has no SMB set up yet. Instead, the SDK will
  // reconnect to the backend with a producer-provided SMB.
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "Event");

  // Create a new trace session.
  auto* tracing_session = NewTraceWithCategories({"test"});
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  // Emit another event after starting.
  TRACE_EVENT_END("test");

  // Both events should be retained.
  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  EXPECT_THAT(slices, ElementsAre("B:test.Event", "E"));
}

TEST_P(PerfettoStartupTracingApiTest, DontTraceBeforeStartupSetup) {
  // This event should not be recorded.
  TRACE_EVENT_BEGIN("test", "EventBeforeStartupTrace");
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "Event");

  auto* tracing_session = NewTraceWithCategories({"test"});
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_END("test");

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);

  EXPECT_THAT(slices, ElementsAre("B:test.Event", "E"));
}

// Test the StartupTracing when there are multiple data sources registered
// (2 data sources in this test) but only a few of them contribute in startup
// tracing.
TEST_P(PerfettoStartupTracingApiTest, MultipleDataSourceFewContributing) {
  perfetto::TraceConfig cfg;
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("CustomDataSource");
  SetupStartupTracing(cfg);
  TRACE_EVENT_BEGIN("test", "TrackEvent.Startup");

  auto* tracing_session = NewTraceWithCategories({"test"}, {}, cfg);
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "TrackEvent.Main");
  perfetto::TrackEvent::Flush();
  CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
    {
      auto packet = ctx.NewTracePacket();
      packet->set_for_testing()->set_str("CustomDataSource.Main");
    }
    ctx.Flush();
  });

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  auto slices = ReadSlicesFromTrace(trace);
  EXPECT_THAT(slices, ElementsAre("B:test.TrackEvent.Startup",
                                  "B:test.TrackEvent.Main"));
  std::vector<std::string> test_strings;
  for (auto& trace_packet : trace.packet()) {
    if (trace_packet.has_for_testing()) {
      test_strings.push_back(trace_packet.for_testing().str());
    }
  }
  EXPECT_THAT(test_strings, ElementsAre("CustomDataSource.Main"));
}

// Test the StartupTracing when there are multiple data sources registered
// (2 data sources in this test) and all of them are contributing in startup
// tracing.
TEST_P(PerfettoStartupTracingApiTest, MultipleDataSourceAllContributing) {
  perfetto::TraceConfig cfg;
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("CustomDataSource");
  SetupStartupTracing(cfg);
  TRACE_EVENT_BEGIN("test", "TrackEvent.Startup");
  CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_for_testing()->set_str("CustomDataSource.Startup");
  });

  auto* tracing_session = NewTraceWithCategories({"test"}, {}, cfg);
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "TrackEvent.Main");
  perfetto::TrackEvent::Flush();
  CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
    {
      auto packet = ctx.NewTracePacket();
      packet->set_for_testing()->set_str("CustomDataSource.Main");
    }
    ctx.Flush();
  });

  auto trace = StopSessionAndReturnParsedTrace(tracing_session);
  auto slices = ReadSlicesFromTrace(trace);
  EXPECT_THAT(slices, ElementsAre("B:test.TrackEvent.Startup",
                                  "B:test.TrackEvent.Main"));
  std::vector<std::string> test_strings;
  for (auto& trace_packet : trace.packet()) {
    if (trace_packet.has_for_testing()) {
      test_strings.push_back(trace_packet.for_testing().str());
    }
  }
  EXPECT_THAT(test_strings,
              ElementsAre("CustomDataSource.Startup", "CustomDataSource.Main"));
}

// Startup tracing requires BufferExhaustedPolicy::kDrop, i.e. once the SMB is
// filled with startup events, any further events should be dropped.
// TODO(b/261493947): fix or remove. go/aosp_ci_failure23
TEST_P(PerfettoStartupTracingApiTest, DISABLED_DropPolicy) {
  SetupStartupTracing();
  constexpr int kNumEvents = 100000;
  for (int i = 0; i < kNumEvents; i++) {
    TRACE_EVENT_BEGIN("test", "StartupEvent");
  }

  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);
  std::unordered_map<std::string, int> freq_map;
  for (auto& slice : slices) {
    freq_map[slice]++;
  }
  EXPECT_GT(freq_map["B:test.StartupEvent"], 0);
  EXPECT_LT(freq_map["B:test.StartupEvent"], kNumEvents);
}

// TODO(b/261493947): fix or remove.
TEST_P(PerfettoStartupTracingApiTest, DISABLED_Abort) {
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "StartupEvent");
  AbortStartupTracing();

  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");

  auto slices = StopSessionAndReadSlicesFromTrace(tracing_session);

  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
}

TEST_P(PerfettoStartupTracingApiTest, AbortAndRestart) {
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "StartupEvent1");
  AbortStartupTracing();
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "StartupEvent2");

  auto* tracing_session = NewTraceWithCategories({"test"});
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();

  TRACE_EVENT_BEGIN("test", "MainEvent");
  perfetto::TrackEvent::Flush();

  tracing_session->get()->StopBlocking();

  auto slices = ReadSlicesFromTraceSession(tracing_session->get());

  EXPECT_THAT(slices, ElementsAre("B:test.StartupEvent2", "B:test.MainEvent"));
}

TEST_P(PerfettoStartupTracingApiTest, Timeout) {
  SetupStartupTracingOpts args;
  args.timeout_ms = 2000;
  SetupStartupTracing({}, args);
  for (int i = 0; i < 25; i++) {
    TRACE_EVENT_BEGIN("test", "StartupEvent");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  auto* tracing_session = NewTraceWithCategories({"test"});
  tracing_session->get()->StartBlocking();
  TRACE_EVENT_BEGIN("test", "MainEvent");

  perfetto::TrackEvent::Flush();

  tracing_session->get()->StopBlocking();

  auto slices = ReadSlicesFromTraceSession(tracing_session->get());
  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
}

// TODO(b/261493947): fix or remove.
TEST_P(PerfettoStartupTracingApiTest, DISABLED_Callbacks) {
  for (bool abort : {true, false}) {
    SetupStartupTracingOpts args;
    std::vector<std::string> callback_events;
    using CallbackArgs = perfetto::Tracing::OnStartupTracingSetupCallbackArgs;
    args.on_setup = [&](CallbackArgs callback_arg) {
      callback_events.push_back(
          "OnSetup(num_data_sources_started=" +
          std::to_string(callback_arg.num_data_sources_started) + ")");
    };
    args.on_adopted = [&]() { callback_events.push_back("OnAdopted()"); };
    args.on_aborted = [&]() { callback_events.push_back("OnAborted()"); };
    SetupStartupTracing({}, args);
    TRACE_EVENT_BEGIN("test", "StartupEvent");
    if (abort) {
      AbortStartupTracing();
    }
    auto* tracing_session = NewTraceWithCategories({"test"});
    tracing_session->get()->StartBlocking();

    TRACE_EVENT_BEGIN("test", "MainEvent");
    perfetto::TrackEvent::Flush();

    tracing_session->get()->StopBlocking();

    auto slices = ReadSlicesFromTraceSession(tracing_session->get());

    ASSERT_EQ(2u, callback_events.size());
    EXPECT_EQ("OnSetup(num_data_sources_started=1)", callback_events.at(0));
    if (abort) {
      EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
      EXPECT_EQ("OnAborted()", callback_events.at(1));
    } else {
      EXPECT_THAT(slices,
                  ElementsAre("B:test.StartupEvent", "B:test.MainEvent"));
      EXPECT_EQ("OnAdopted()", callback_events.at(1));
    }
  }
}

// Test that it's ok if main tracing is never started.
// TODO(b/261493947): fix or remove.
TEST_P(PerfettoStartupTracingApiTest, DISABLED_MainTracingNeverStarted) {
  SetupStartupTracing();
  TRACE_EVENT_BEGIN("test", "StartupEvent");
}

// Validates that Startup Trace works fine if we dont emit any event
// during startup tracing session.
TEST_P(PerfettoStartupTracingApiTest, NoEventInStartupTracing) {
  SetupStartupTracing();

  auto* tracing_session = NewTraceWithCategories({"test"});
  ASSERT_TRUE(WaitForOneProducerConnected(tracing_session->get()));
  tracing_session->get()->StartBlocking();
  // Emit an event now that the session was fully started. This should go
  // strait to the SMB.
  TRACE_EVENT_BEGIN("test", "MainEvent");
  perfetto::TrackEvent::Flush();
  tracing_session->get()->StopBlocking();
  auto slices = ReadSlicesFromTraceSession(tracing_session->get());
  EXPECT_THAT(slices, ElementsAre("B:test.MainEvent"));
}

class ConcurrentSessionTest : public ::testing::Test {
 public:
  void SetUp() override {
    system_service_ = perfetto::test::SystemService::Start();
    if (!system_service_.valid()) {
      GTEST_SKIP();
    }
    ASSERT_FALSE(perfetto::Tracing::IsInitialized());
  }

  void InitPerfetto(bool supports_multiple_data_source_instances = true) {
    TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend | perfetto::kSystemBackend;
    args.supports_multiple_data_source_instances =
        supports_multiple_data_source_instances;
    g_test_tracing_policy->should_allow_consumer_connection = true;
    args.tracing_policy = g_test_tracing_policy;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();
    perfetto::test::SyncProducers();
    perfetto::test::DisableReconnectLimit();
  }

  void TearDown() override { perfetto::Tracing::ResetForTesting(); }

  static std::unique_ptr<perfetto::TracingSession> StartTracing(
      perfetto::BackendType backend_type,
      bool short_stop_timeout = false) {
    perfetto::TraceConfig cfg;
    if (short_stop_timeout) {
      cfg.set_data_source_stop_timeout_ms(500);
    }
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");
    auto tracing_session = perfetto::Tracing::NewTrace(backend_type);
    tracing_session->Setup(cfg);
    tracing_session->StartBlocking();
    return tracing_session;
  }
  std::vector<std::string> StopTracing(
      std::unique_ptr<perfetto::TracingSession> tracing_session,
      bool expect_incremental_state_cleared = true) {
    perfetto::TrackEvent::Flush();
    tracing_session->StopBlocking();
    std::vector<char> trace_data(tracing_session->ReadTraceBlocking());
    return ReadSlicesFromTrace(trace_data, expect_incremental_state_cleared);
  }

  perfetto::test::SystemService system_service_;
};

// Verify that concurrent sessions works well by default.
// (i.e. when `disallow_concurrent_sessions` param is not set)
TEST_F(ConcurrentSessionTest, ConcurrentBackends) {
  InitPerfetto();
  auto tracing_session1 = StartTracing(perfetto::kSystemBackend);
  TRACE_EVENT_BEGIN("test", "DrawGame1");

  auto tracing_session2 = StartTracing(perfetto::kInProcessBackend);
  // Should be recorded by both sessions.
  TRACE_EVENT_BEGIN("test", "DrawGame2");

  auto slices1 = StopTracing(std::move(tracing_session1));
  EXPECT_THAT(slices1, ElementsAre("B:test.DrawGame1", "B:test.DrawGame2"));

  auto slices2 = StopTracing(std::move(tracing_session2));
  EXPECT_THAT(slices2, ElementsAre("B:test.DrawGame2"));

  auto tracing_session3 = StartTracing(perfetto::kInProcessBackend);
  TRACE_EVENT_BEGIN("test", "DrawGame3");

  auto slices3 = StopTracing(std::move(tracing_session3));
  EXPECT_THAT(slices3, ElementsAre("B:test.DrawGame3"));
}

// When `supports_multiple_data_source_instances = false`, second session
// should not be started.
TEST_F(ConcurrentSessionTest, DisallowMultipleSessionBasic) {
  InitPerfetto(/* supports_multiple_data_source_instances = */ false);
  auto tracing_session1 = StartTracing(perfetto::kInProcessBackend);
  TRACE_EVENT_BEGIN("test", "DrawGame1");

  auto tracing_session2 =
      StartTracing(perfetto::kInProcessBackend, /*short_stop_timeout=*/true);
  TRACE_EVENT_BEGIN("test", "DrawGame2");

  auto slices1 = StopTracing(std::move(tracing_session1));
  EXPECT_THAT(slices1, ElementsAre("B:test.DrawGame1", "B:test.DrawGame2"));

  auto slices2 = StopTracing(std::move(tracing_session2),
                             false /* expect_incremental_state_cleared */);
  // Because `tracing_session2` was not really started.
  EXPECT_THAT(slices2, ElementsAre());

  auto tracing_session3 = StartTracing(perfetto::kInProcessBackend);
  TRACE_EVENT_BEGIN("test", "DrawGame3");

  auto slices3 = StopTracing(std::move(tracing_session3));
  EXPECT_THAT(slices3, ElementsAre("B:test.DrawGame3"));
}

TEST(PerfettoApiInitTest, NonInitializedThreadTrackCurrent) {
  ASSERT_FALSE(perfetto::Tracing::IsInitialized());

  auto PERFETTO_UNUSED track = perfetto::ThreadTrack::Current();
}

TEST(PerfettoApiInitTest, NonInitializedDataSourceTrace) {
  ASSERT_FALSE(perfetto::Tracing::IsInitialized());

  CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
    {
      auto packet = ctx.NewTracePacket();
      packet->set_for_testing()->set_str("CustomDataSource.Main");
    }
    ctx.Flush();
  });
}

TEST(PerfettoApiInitTest, NonInitializedTraceEventMacro) {
  ASSERT_FALSE(perfetto::Tracing::IsInitialized());

  TRACE_EVENT("cat", "Foo");
}

TEST(PerfettoApiInitTest, DisableSystemConsumer) {
  g_test_tracing_policy->should_allow_consumer_connection = true;

  auto system_service = perfetto::test::SystemService::Start();
  // If the system backend isn't supported, skip
  if (!system_service.valid()) {
    GTEST_SKIP();
  }

  EXPECT_FALSE(perfetto::Tracing::IsInitialized());
  TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  args.tracing_policy = g_test_tracing_policy;
  args.enable_system_consumer = false;
  perfetto::Tracing::Initialize(args);

  // If this wasn't the first test to run in this process, any producers
  // connected to the old system service will have been disconnected by the
  // service restarting above. Wait for all producers to connect again before
  // proceeding with the test.
  perfetto::test::SyncProducers();

  perfetto::test::DisableReconnectLimit();

  // Creating the consumer with kUnspecifiedBackend should cause a connection
  // error: there's no consumer backend.
  {
    std::unique_ptr<perfetto::TracingSession> ts =
        perfetto::Tracing::NewTrace(perfetto::kUnspecifiedBackend);

    WaitableTestEvent got_error;
    ts->SetOnErrorCallback([&](perfetto::TracingError error) {
      EXPECT_EQ(perfetto::TracingError::kDisconnected, error.code);
      EXPECT_FALSE(error.message.empty());
      got_error.Notify();
    });
    got_error.Wait();
  }

  // Creating the consumer with kSystemBackend should create a system consumer
  // backend on the spot.
  EXPECT_TRUE(perfetto::Tracing::NewTrace(perfetto::kSystemBackend)
                  ->QueryServiceStateBlocking()
                  .success);

  // Now even a consumer with kUnspecifiedBackend should succeed, because the
  // backend has been created.
  EXPECT_TRUE(perfetto::Tracing::NewTrace(perfetto::kUnspecifiedBackend)
                  ->QueryServiceStateBlocking()
                  .success);

  perfetto::Tracing::ResetForTesting();
}

TEST(PerfettoApiInitTest, SeparateInitializations) {
  auto system_service = perfetto::test::SystemService::Start();
  // If the system backend isn't supported, skip
  if (!system_service.valid()) {
    GTEST_SKIP();
  }

  {
    EXPECT_FALSE(perfetto::Tracing::IsInitialized());
    TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
  }

  // If this wasn't the first test to run in this process, any producers
  // connected to the old system service will have been disconnected by the
  // service restarting above. Wait for all producers to connect again before
  // proceeding with the test.
  perfetto::test::SyncProducers();

  perfetto::test::DisableReconnectLimit();

  {
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name("CustomDataSource");
    CustomDataSource::Register(dsd);
  }

  {
    std::unique_ptr<perfetto::TracingSession> tracing_session =
        perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    auto result = tracing_session->QueryServiceStateBlocking();
    perfetto::protos::gen::TracingServiceState state;
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                     result.service_state_data.size()));
    EXPECT_THAT(state.data_sources(),
                Contains(Property(
                    &perfetto::protos::gen::TracingServiceState::DataSource::
                        ds_descriptor,
                    Property(&perfetto::protos::gen::DataSourceDescriptor::name,
                             "CustomDataSource"))));
  }

  {
    EXPECT_TRUE(perfetto::Tracing::IsInitialized());
    TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    args.enable_system_consumer = false;
    perfetto::Tracing::Initialize(args);
  }

  perfetto::test::SyncProducers();

  {
    std::unique_ptr<perfetto::TracingSession> tracing_session =
        perfetto::Tracing::NewTrace(perfetto::kSystemBackend);
    auto result = tracing_session->QueryServiceStateBlocking();
    perfetto::protos::gen::TracingServiceState state;
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(state.ParseFromArray(result.service_state_data.data(),
                                     result.service_state_data.size()));
    EXPECT_THAT(state.data_sources(),
                Contains(Property(
                    &perfetto::protos::gen::TracingServiceState::DataSource::
                        ds_descriptor,
                    Property(&perfetto::protos::gen::DataSourceDescriptor::name,
                             "CustomDataSource"))));
  }
  perfetto::test::TracingMuxerImplInternalsForTest::
      ClearDataSourceTlsStateOnReset<CustomDataSource>();

  perfetto::Tracing::ResetForTesting();
}

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
namespace {

int ConnectUnixSocket() {
  std::string socket_name = perfetto::GetProducerSocket();
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un saddr;
  memset(&saddr, 0, sizeof(saddr));
  memcpy(saddr.sun_path, socket_name.data(), socket_name.size());
  saddr.sun_family = AF_UNIX;
  auto size = static_cast<socklen_t>(__builtin_offsetof(sockaddr_un, sun_path) +
                                     socket_name.size() + 1);
  connect(fd, reinterpret_cast<const struct sockaddr*>(&saddr), size);
  return fd;
}

using CreateSocketFunction =
    std::function<void(perfetto::CreateSocketCallback)>;

CreateSocketFunction* g_std_function = nullptr;

void SetCreateSocketFunction(CreateSocketFunction func) {
  g_std_function = new CreateSocketFunction(func);
}

void ResetCreateSocketFunction() {
  delete g_std_function;
}

void CallCreateSocketFunction(perfetto::CreateSocketCallback cb) {
  PERFETTO_DCHECK(g_std_function);
  (*g_std_function)(cb);
}

}  // namespace

TEST(PerfettoApiInitTest, AsyncSocket) {
  auto system_service = perfetto::test::SystemService::Start();
  // If the system backend isn't supported, skip
  if (!system_service.valid()) {
    GTEST_SKIP();
  }

  EXPECT_FALSE(perfetto::Tracing::IsInitialized());

  perfetto::CreateSocketCallback socket_callback;
  WaitableTestEvent create_socket_called;

  TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  args.tracing_policy = g_test_tracing_policy;
  args.create_socket_async = &CallCreateSocketFunction;
  SetCreateSocketFunction([&socket_callback, &create_socket_called](
                              perfetto::CreateSocketCallback cb) {
    socket_callback = cb;
    create_socket_called.Notify();
  });

  perfetto::Tracing::Initialize(args);
  create_socket_called.Wait();

  int fd = ConnectUnixSocket();
  socket_callback(fd);

  perfetto::test::SyncProducers();
  EXPECT_TRUE(perfetto::Tracing::NewTrace(perfetto::kSystemBackend)
                  ->QueryServiceStateBlocking()
                  .success);

  perfetto::Tracing::ResetForTesting();
  ResetCreateSocketFunction();
}

TEST(PerfettoApiInitTest, AsyncSocketDisconnect) {
  auto system_service = perfetto::test::SystemService::Start();
  // If the system backend isn't supported, skip
  if (!system_service.valid()) {
    GTEST_SKIP();
  }

  EXPECT_FALSE(perfetto::Tracing::IsInitialized());

  perfetto::CreateSocketCallback socket_callback;
  testing::MockFunction<CreateSocketFunction> mock_create_socket;
  WaitableTestEvent create_socket_called1, create_socket_called2;

  TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  args.tracing_policy = g_test_tracing_policy;
  args.create_socket_async = &CallCreateSocketFunction;
  SetCreateSocketFunction(mock_create_socket.AsStdFunction());

  EXPECT_CALL(mock_create_socket, Call)
      .WillOnce([&socket_callback,
                 &create_socket_called1](perfetto::CreateSocketCallback cb) {
        socket_callback = cb;
        create_socket_called1.Notify();
      })
      .WillOnce([&socket_callback,
                 &create_socket_called2](perfetto::CreateSocketCallback cb) {
        socket_callback = cb;
        create_socket_called2.Notify();
      });

  perfetto::Tracing::Initialize(args);
  create_socket_called1.Wait();
  int fd = ConnectUnixSocket();
  socket_callback(fd);

  perfetto::test::SyncProducers();
  EXPECT_TRUE(perfetto::Tracing::NewTrace(perfetto::kSystemBackend)
                  ->QueryServiceStateBlocking()
                  .success);

  // Restart the system service. This will cause the producer and consumer to
  // disconnect and reconnect. The create_socket_async function should be called
  // for the second time.
  system_service.Restart();
  create_socket_called2.Wait();
  fd = ConnectUnixSocket();
  socket_callback(fd);

  perfetto::test::SyncProducers();
  EXPECT_TRUE(perfetto::Tracing::NewTrace(perfetto::kSystemBackend)
                  ->QueryServiceStateBlocking()
                  .success);

  perfetto::Tracing::ResetForTesting();
  ResetCreateSocketFunction();
}

TEST(PerfettoApiInitTest, AsyncSocketStartupTracing) {
  auto system_service = perfetto::test::SystemService::Start();
  // If the system backend isn't supported, skip
  if (!system_service.valid()) {
    GTEST_SKIP();
  }

  EXPECT_FALSE(perfetto::Tracing::IsInitialized());

  perfetto::CreateSocketCallback socket_callback;
  WaitableTestEvent create_socket_called;

  TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  args.tracing_policy = g_test_tracing_policy;
  args.create_socket_async = &CallCreateSocketFunction;
  SetCreateSocketFunction([&socket_callback, &create_socket_called](
                              perfetto::CreateSocketCallback cb) {
    socket_callback = cb;
    create_socket_called.Notify();
  });

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  perfetto::TraceConfig cfg;
  cfg.set_duration_ms(500);
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  perfetto::protos::gen::TrackEventConfig te_cfg;
  te_cfg.add_disabled_categories("*");
  te_cfg.add_enabled_categories("test");
  ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

  perfetto::Tracing::SetupStartupTracingOpts opts;
  opts.backend = perfetto::kSystemBackend;
  auto startup_session =
      perfetto::Tracing::SetupStartupTracingBlocking(cfg, std::move(opts));

  // Emit a significant number of events to write >1 chunk of data.
  constexpr size_t kNumEvents = 1000;
  for (size_t i = 0; i < kNumEvents; i++) {
    TRACE_EVENT_INSTANT("test", "StartupEvent");
  }

  // Now proceed with the connection to the service and wait until it completes.
  int fd = ConnectUnixSocket();
  socket_callback(fd);
  perfetto::test::SyncProducers();

  auto session = perfetto::Tracing::NewTrace(perfetto::kSystemBackend);
  session->Setup(cfg);
  session->StartBlocking();

  // Write even more events, now with connection established.
  for (size_t i = 0; i < kNumEvents; i++) {
    TRACE_EVENT_INSTANT("test", "TraceEvent");
  }

  perfetto::TrackEvent::Flush();
  session->StopBlocking();

  auto raw_trace = session->ReadTraceBlocking();
  perfetto::protos::gen::Trace parsed_trace;
  EXPECT_TRUE(parsed_trace.ParseFromArray(raw_trace.data(), raw_trace.size()));

  size_t n_track_events = 0;
  for (const auto& packet : parsed_trace.packet()) {
    if (packet.has_track_event()) {
      ++n_track_events;
    }
  }

  // Events from both startup and service-initiated sessions should be retained.
  EXPECT_EQ(n_track_events, kNumEvents * 2);

  startup_session.reset();
  session.reset();
  perfetto::Tracing::ResetForTesting();
  ResetCreateSocketFunction();
}
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

struct BackendTypeAsString {
  std::string operator()(
      const ::testing::TestParamInfo<perfetto::BackendType>& info) const {
    switch (info.param) {
      case perfetto::kInProcessBackend:
        return "InProc";
      case perfetto::kSystemBackend:
        return "System";
      case perfetto::kCustomBackend:
        return "Custom";
      case perfetto::kUnspecifiedBackend:
        return "Unspec";
    }
    return nullptr;
  }
};

INSTANTIATE_TEST_SUITE_P(PerfettoApiTest,
                         PerfettoApiTest,
                         ::testing::Values(perfetto::kInProcessBackend,
                                           perfetto::kSystemBackend),
                         BackendTypeAsString());

// In-process backend doesn't support startup tracing.
INSTANTIATE_TEST_SUITE_P(PerfettoStartupTracingApiTest,
                         PerfettoStartupTracingApiTest,
                         ::testing::Values(perfetto::kSystemBackend),
                         BackendTypeAsString());

class PerfettoApiEnvironment : public ::testing::Environment {
 public:
  void TearDown() override {
    // Test shutting down Perfetto only when all other tests have been run and
    // no more tracing code will be executed.
    PERFETTO_CHECK(!perfetto::Tracing::IsInitialized());
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::Tracing::Shutdown();
    PERFETTO_CHECK(!perfetto::Tracing::IsInitialized());
    // Shutting down again is a no-op.
    perfetto::Tracing::Shutdown();
    PERFETTO_CHECK(!perfetto::Tracing::IsInitialized());
  }
};

int PERFETTO_UNUSED initializer =
    perfetto::integration_tests::RegisterApiIntegrationTestInitializer([] {
      ::testing::AddGlobalTestEnvironment(new PerfettoApiEnvironment);
    });

}  // namespace

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(CustomDataSource);
PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(MockDataSource);
PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(MockDataSource2);
PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(TestIncrementalDataSource,
                                            TestIncrementalDataSourceTraits);

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(CustomDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(MockDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(MockDataSource2);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(TestIncrementalDataSource,
                                           TestIncrementalDataSourceTraits);
