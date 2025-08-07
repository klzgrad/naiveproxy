// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <benchmark/benchmark.h>

#include "perfetto/tracing.h"
#include "protos/perfetto/trace/test_event.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/log_message.pbzero.h"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category("benchmark"));
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace {

class BenchmarkDataSource : public perfetto::DataSource<BenchmarkDataSource> {
 public:
  void OnSetup(const SetupArgs&) override {}
  void OnStart(const StartArgs&) override {}
  void OnStop(const StopArgs&) override {}
};

static void BM_TracingDataSourceDisabled(benchmark::State& state) {
  while (state.KeepRunning()) {
    BenchmarkDataSource::Trace([&](BenchmarkDataSource::TraceContext) {});
    benchmark::ClobberMemory();
  }
}

std::unique_ptr<perfetto::TracingSession> StartTracing(
    const std::string& data_source_name) {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::kInProcessBackend;
  perfetto::Tracing::Initialize(args);

  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("benchmark");
  BenchmarkDataSource::Register(dsd);
  perfetto::TrackEvent::Register();

  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name(data_source_name);
  auto tracing_session =
      perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
  tracing_session->Setup(cfg);
  tracing_session->StartBlocking();
  return tracing_session;
}

static void BM_TracingDataSourceLambda(benchmark::State& state) {
  auto tracing_session = StartTracing("benchmark");

  while (state.KeepRunning()) {
    BenchmarkDataSource::Trace([&](BenchmarkDataSource::TraceContext ctx) {
      auto packet = ctx.NewTracePacket();
      packet->set_timestamp(42);
      packet->set_for_testing()->set_str("benchmark");
    });
    benchmark::ClobberMemory();
  }

  tracing_session->StopBlocking();
  PERFETTO_CHECK(!tracing_session->ReadTraceBlocking().empty());
}

// Parses `trace` and returns the size of the first trace packet that contains
// `for_testing()`.
size_t GetForTestingPacketSizeFromTrace(const std::vector<char>& trace) {
  size_t packet_size = 0;
  perfetto::protos::pbzero::Trace::Decoder decoder(
      reinterpret_cast<const uint8_t*>(trace.data()), trace.size());
  for (auto packet = decoder.packet(); packet; packet++) {
    perfetto::protos::pbzero::TracePacket::Decoder packet_decoder(*packet);

    if (packet_decoder.has_for_testing()) {
      packet_size = packet->size();
      break;
    }
  }
  return packet_size;
}

static void BM_TracingDataSourceLambdaDifferentPacketSize(
    benchmark::State& state) {
  auto tracing_session = StartTracing("benchmark");
  // The number of string fields added in the nested submessage. This controls
  // the size of the trace packet (reported by the "PacketSize" counter).
  const size_t kNumFields = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    BenchmarkDataSource::Trace([&](BenchmarkDataSource::TraceContext ctx) {
      auto packet = ctx.NewTracePacket();
      auto* payload = packet->set_for_testing()->set_payload();
      for (size_t i = 0; i < kNumFields; i++) {
        payload->add_str("ABCDEFGH");
      }
    });
    benchmark::ClobberMemory();
  }

  tracing_session->StopBlocking();

  std::vector<char> trace = tracing_session->ReadTraceBlocking();
  PERFETTO_CHECK(!trace.empty());
  state.counters["PacketSize"] =
      static_cast<double>(GetForTestingPacketSizeFromTrace(trace));
}

static void BM_TracingTrackEventDisabled(benchmark::State& state) {
  while (state.KeepRunning()) {
    TRACE_EVENT_BEGIN("benchmark", "DisabledEvent");
    benchmark::ClobberMemory();
  }
}

static void BM_TracingTrackEventBasic(benchmark::State& state) {
  auto tracing_session = StartTracing("track_event");

  while (state.KeepRunning()) {
    TRACE_EVENT_BEGIN("benchmark", "Event");
    benchmark::ClobberMemory();
  }

  tracing_session->StopBlocking();
  PERFETTO_CHECK(!tracing_session->ReadTraceBlocking().empty());
}

static void BM_TracingTrackEventDebugAnnotations(benchmark::State& state) {
  auto tracing_session = StartTracing("track_event");

  while (state.KeepRunning()) {
    TRACE_EVENT_BEGIN("benchmark", "Event", "value", 42);
    benchmark::ClobberMemory();
  }

  tracing_session->StopBlocking();
  PERFETTO_CHECK(!tracing_session->ReadTraceBlocking().empty());
}

static void BM_TracingTrackEventLambda(benchmark::State& state) {
  auto tracing_session = StartTracing("track_event");

  while (state.KeepRunning()) {
    TRACE_EVENT_BEGIN("benchmark", "Event", [&](perfetto::EventContext ctx) {
      auto* log = ctx.event()->set_log_message();
      log->set_source_location_iid(42);
      log->set_body_iid(1234);
    });
    benchmark::ClobberMemory();
  }

  tracing_session->StopBlocking();
  PERFETTO_CHECK(!tracing_session->ReadTraceBlocking().empty());
}

}  // namespace

BENCHMARK(BM_TracingDataSourceDisabled);
BENCHMARK(BM_TracingDataSourceLambda);
BENCHMARK(BM_TracingDataSourceLambdaDifferentPacketSize)->Range(1, 1000);
BENCHMARK(BM_TracingTrackEventBasic);
BENCHMARK(BM_TracingTrackEventDebugAnnotations);
BENCHMARK(BM_TracingTrackEventDisabled);
BENCHMARK(BM_TracingTrackEventLambda);
