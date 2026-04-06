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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <memory>

#include <benchmark/benchmark.h>

#include "perfetto/public/abi/atomic.h"
#include "perfetto/public/data_source.h"
#include "perfetto/public/pb_utils.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/protos/trace/test_event.pzc.h"
#include "perfetto/public/protos/trace/trace.pzc.h"
#include "perfetto/public/protos/trace/trace_packet.pzc.h"
#include "perfetto/public/protos/trace/track_event/debug_annotation.pzc.h"
#include "perfetto/public/protos/trace/track_event/track_event.pzc.h"
#include "perfetto/public/te_category_macros.h"
#include "perfetto/public/te_macros.h"
#include "perfetto/public/track_event.h"

#include "src/shared_lib/test/utils.h"

static struct PerfettoDs custom = PERFETTO_DS_INIT();

#define BENCHMARK_CATEGORIES(C) C(benchmark_cat, "benchmark", "")

PERFETTO_TE_CATEGORIES_DEFINE(BENCHMARK_CATEGORIES)

namespace {

using ::perfetto::shlib::test_utils::FieldView;
using ::perfetto::shlib::test_utils::IdFieldView;
using ::perfetto::shlib::test_utils::TracingSession;

constexpr char kDataSourceName[] = "com.example.custom_data_source";

bool Initialize() {
  struct PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
  args.backends = PERFETTO_BACKEND_IN_PROCESS;
  PerfettoProducerInit(args);
  PerfettoDsRegister(&custom, kDataSourceName, PerfettoDsParamsDefault());
  PerfettoTeInit();
  PERFETTO_TE_REGISTER_CATEGORIES(BENCHMARK_CATEGORIES);
  return true;
}

void EnsureInitialized() {
  static bool initialized = Initialize();
  (void)initialized;
}

size_t DecodePacketSizes(const std::vector<uint8_t>& data) {
  for (struct PerfettoPbDecoderField field :
       IdFieldView(data, perfetto_protos_Trace_packet_field_number)) {
    if (field.status != PERFETTO_PB_DECODER_OK ||
        field.wire_type != PERFETTO_PB_WIRE_TYPE_DELIMITED) {
      abort();
    }
    IdFieldView for_testing_fields(
        field, perfetto_protos_TracePacket_for_testing_field_number);
    if (!for_testing_fields.ok()) {
      abort();
    }
    if (for_testing_fields.size() == 0) {
      continue;
    }
    if (for_testing_fields.size() > 1 || for_testing_fields.front().wire_type !=
                                             PERFETTO_PB_WIRE_TYPE_DELIMITED) {
      abort();
    }
    return field.value.delimited.len;
  }

  return 0;
}

void BM_Shlib_DataSource_Disabled(benchmark::State& state) {
  EnsureInitialized();
  for (auto _ : state) {
    PERFETTO_DS_TRACE(custom, ctx) {}
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_DataSource_DifferentPacketSize(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session =
      TracingSession::Builder().set_data_source_name(kDataSourceName).Build();

  // This controls the number of times a field is added in the trace packet.
  // It controls the size of the trace packet. The PacketSize counter reports
  // the exact number.
  const size_t kNumFields = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    PERFETTO_DS_TRACE(custom, ctx) {
      struct PerfettoDsRootTracePacket trace_packet;
      PerfettoDsTracerPacketBegin(&ctx, &trace_packet);

      {
        struct perfetto_protos_TestEvent for_testing;
        perfetto_protos_TracePacket_begin_for_testing(&trace_packet.msg,
                                                      &for_testing);
        {
          struct perfetto_protos_TestEvent_TestPayload payload;
          perfetto_protos_TestEvent_begin_payload(&for_testing, &payload);
          for (size_t i = 0; i < kNumFields; i++) {
            perfetto_protos_TestEvent_TestPayload_set_cstr_str(&payload,
                                                               "ABCDEFGH");
          }
          perfetto_protos_TestEvent_end_payload(&for_testing, &payload);
        }
        perfetto_protos_TracePacket_end_for_testing(&trace_packet.msg,
                                                    &for_testing);
      }
      PerfettoDsTracerPacketEnd(&ctx, &trace_packet);
    }
    benchmark::ClobberMemory();
  }

  tracing_session.StopBlocking();
  std::vector<uint8_t> data = tracing_session.ReadBlocking();

  // Just compute the PacketSize counter.
  state.counters["PacketSize"] = static_cast<double>(DecodePacketSizes(data));
}

void BM_Shlib_TeDisabled(benchmark::State& state) {
  EnsureInitialized();
  while (state.KeepRunning()) {
    PERFETTO_TE(benchmark_cat, PERFETTO_TE_SLICE_BEGIN("DisabledEvent"));
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeBasic(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    PERFETTO_TE(benchmark_cat, PERFETTO_TE_SLICE_BEGIN("Event"));
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeBasicNoIntern(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    PERFETTO_TE(benchmark_cat, PERFETTO_TE_SLICE_BEGIN("Event"),
                PERFETTO_TE_NO_INTERN());
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeDebugAnnotations(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    PERFETTO_TE(benchmark_cat, PERFETTO_TE_SLICE_BEGIN("Event"),
                PERFETTO_TE_ARG_UINT64("value", 42));
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeCustomProto(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    PERFETTO_TE(
        benchmark_cat, PERFETTO_TE_SLICE_BEGIN("Event"),
        PERFETTO_TE_PROTO_FIELDS(PERFETTO_TE_PROTO_FIELD_NESTED(
            perfetto_protos_TrackEvent_debug_annotations_field_number,
            PERFETTO_TE_PROTO_FIELD_CSTR(
                perfetto_protos_DebugAnnotation_name_field_number, "value"),
            PERFETTO_TE_PROTO_FIELD_VARINT(
                perfetto_protos_DebugAnnotation_uint_value_field_number, 42))));
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeLlBasic(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    if (PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
            benchmark_cat.enabled, PERFETTO_MEMORY_ORDER_RELAXED))) {
      struct PerfettoTeTimestamp timestamp = PerfettoTeGetTimestamp();
      int32_t type = PERFETTO_TE_TYPE_SLICE_BEGIN;
      const char* name = "Event";
      for (struct PerfettoTeLlIterator ctx =
               PerfettoTeLlBeginSlowPath(&benchmark_cat, timestamp);
           ctx.impl.ds.tracer != nullptr;
           PerfettoTeLlNext(&benchmark_cat, timestamp, &ctx)) {
        uint64_t name_iid;
        {
          struct PerfettoDsRootTracePacket trace_packet;
          PerfettoTeLlPacketBegin(&ctx, &trace_packet);
          PerfettoTeLlWriteTimestamp(&trace_packet.msg, &timestamp);
          perfetto_protos_TracePacket_set_sequence_flags(
              &trace_packet.msg,
              perfetto_protos_TracePacket_SEQ_NEEDS_INCREMENTAL_STATE);
          {
            struct PerfettoTeLlInternContext intern_ctx;
            PerfettoTeLlInternContextInit(&intern_ctx, ctx.impl.incr,
                                          &trace_packet.msg);
            PerfettoTeLlInternRegisteredCat(&intern_ctx, &benchmark_cat);
            name_iid = PerfettoTeLlInternEventName(&intern_ctx, name);
            PerfettoTeLlInternContextDestroy(&intern_ctx);
          }
          {
            struct perfetto_protos_TrackEvent te_msg;
            perfetto_protos_TracePacket_begin_track_event(&trace_packet.msg,
                                                          &te_msg);
            perfetto_protos_TrackEvent_set_type(
                &te_msg,
                static_cast<enum perfetto_protos_TrackEvent_Type>(type));
            PerfettoTeLlWriteRegisteredCat(&te_msg, &benchmark_cat);
            PerfettoTeLlWriteInternedEventName(&te_msg, name_iid);
            perfetto_protos_TracePacket_end_track_event(&trace_packet.msg,
                                                        &te_msg);
          }
          PerfettoTeLlPacketEnd(&ctx, &trace_packet);
        }
      }
    }

    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeLlBasicNoIntern(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    if (PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
            benchmark_cat.enabled, PERFETTO_MEMORY_ORDER_RELAXED))) {
      struct PerfettoTeTimestamp timestamp = PerfettoTeGetTimestamp();
      int32_t type = PERFETTO_TE_TYPE_SLICE_BEGIN;
      const char* name = "Event";
      for (struct PerfettoTeLlIterator ctx =
               PerfettoTeLlBeginSlowPath(&benchmark_cat, timestamp);
           ctx.impl.ds.tracer != nullptr;
           PerfettoTeLlNext(&benchmark_cat, timestamp, &ctx)) {
        {
          struct PerfettoDsRootTracePacket trace_packet;
          PerfettoTeLlPacketBegin(&ctx, &trace_packet);
          PerfettoTeLlWriteTimestamp(&trace_packet.msg, &timestamp);
          perfetto_protos_TracePacket_set_sequence_flags(
              &trace_packet.msg,
              perfetto_protos_TracePacket_SEQ_NEEDS_INCREMENTAL_STATE);
          {
            struct PerfettoTeLlInternContext intern_ctx;
            PerfettoTeLlInternContextInit(&intern_ctx, ctx.impl.incr,
                                          &trace_packet.msg);
            PerfettoTeLlInternRegisteredCat(&intern_ctx, &benchmark_cat);
            PerfettoTeLlInternContextDestroy(&intern_ctx);
          }
          {
            struct perfetto_protos_TrackEvent te_msg;
            perfetto_protos_TracePacket_begin_track_event(&trace_packet.msg,
                                                          &te_msg);
            perfetto_protos_TrackEvent_set_type(
                &te_msg,
                static_cast<enum perfetto_protos_TrackEvent_Type>(type));
            PerfettoTeLlWriteRegisteredCat(&te_msg, &benchmark_cat);
            PerfettoTeLlWriteEventName(&te_msg, name);
            perfetto_protos_TracePacket_end_track_event(&trace_packet.msg,
                                                        &te_msg);
          }
          PerfettoTeLlPacketEnd(&ctx, &trace_packet);
        }
      }
    }
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeLlDebugAnnotations(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    if (PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
            benchmark_cat.enabled, PERFETTO_MEMORY_ORDER_RELAXED))) {
      struct PerfettoTeTimestamp timestamp = PerfettoTeGetTimestamp();
      int32_t type = PERFETTO_TE_TYPE_SLICE_BEGIN;
      const char* name = "Event";
      for (struct PerfettoTeLlIterator ctx =
               PerfettoTeLlBeginSlowPath(&benchmark_cat, timestamp);
           ctx.impl.ds.tracer != nullptr;
           PerfettoTeLlNext(&benchmark_cat, timestamp, &ctx)) {
        uint64_t name_iid;
        uint64_t dbg_arg_iid;
        {
          struct PerfettoDsRootTracePacket trace_packet;
          PerfettoTeLlPacketBegin(&ctx, &trace_packet);
          PerfettoTeLlWriteTimestamp(&trace_packet.msg, &timestamp);
          perfetto_protos_TracePacket_set_sequence_flags(
              &trace_packet.msg,
              perfetto_protos_TracePacket_SEQ_NEEDS_INCREMENTAL_STATE);
          {
            struct PerfettoTeLlInternContext intern_ctx;
            PerfettoTeLlInternContextInit(&intern_ctx, ctx.impl.incr,
                                          &trace_packet.msg);
            PerfettoTeLlInternRegisteredCat(&intern_ctx, &benchmark_cat);
            name_iid = PerfettoTeLlInternEventName(&intern_ctx, name);
            dbg_arg_iid = PerfettoTeLlInternDbgArgName(&intern_ctx, "value");
            PerfettoTeLlInternContextDestroy(&intern_ctx);
          }
          {
            struct perfetto_protos_TrackEvent te_msg;
            perfetto_protos_TracePacket_begin_track_event(&trace_packet.msg,
                                                          &te_msg);
            perfetto_protos_TrackEvent_set_type(
                &te_msg,
                static_cast<enum perfetto_protos_TrackEvent_Type>(type));
            PerfettoTeLlWriteRegisteredCat(&te_msg, &benchmark_cat);
            PerfettoTeLlWriteInternedEventName(&te_msg, name_iid);
            {
              struct perfetto_protos_DebugAnnotation dbg_arg;
              perfetto_protos_TrackEvent_begin_debug_annotations(&te_msg,
                                                                 &dbg_arg);
              perfetto_protos_DebugAnnotation_set_name_iid(&dbg_arg,
                                                           dbg_arg_iid);
              perfetto_protos_DebugAnnotation_set_uint_value(&dbg_arg, 42);
              perfetto_protos_TrackEvent_end_debug_annotations(&te_msg,
                                                               &dbg_arg);
            }
            perfetto_protos_TracePacket_end_track_event(&trace_packet.msg,
                                                        &te_msg);
          }
          PerfettoTeLlPacketEnd(&ctx, &trace_packet);
        }
      }
    }
    benchmark::ClobberMemory();
  }
}

void BM_Shlib_TeLlCustomProto(benchmark::State& state) {
  EnsureInitialized();
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();

  while (state.KeepRunning()) {
    if (PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
            benchmark_cat.enabled, PERFETTO_MEMORY_ORDER_RELAXED))) {
      struct PerfettoTeTimestamp timestamp = PerfettoTeGetTimestamp();
      int32_t type = PERFETTO_TE_TYPE_SLICE_BEGIN;
      const char* name = "Event";
      for (struct PerfettoTeLlIterator ctx =
               PerfettoTeLlBeginSlowPath(&benchmark_cat, timestamp);
           ctx.impl.ds.tracer != nullptr;
           PerfettoTeLlNext(&benchmark_cat, timestamp, &ctx)) {
        uint64_t name_iid;
        {
          struct PerfettoDsRootTracePacket trace_packet;
          PerfettoTeLlPacketBegin(&ctx, &trace_packet);
          PerfettoTeLlWriteTimestamp(&trace_packet.msg, &timestamp);
          perfetto_protos_TracePacket_set_sequence_flags(
              &trace_packet.msg,
              perfetto_protos_TracePacket_SEQ_NEEDS_INCREMENTAL_STATE);
          {
            struct PerfettoTeLlInternContext intern_ctx;
            PerfettoTeLlInternContextInit(&intern_ctx, ctx.impl.incr,
                                          &trace_packet.msg);
            PerfettoTeLlInternRegisteredCat(&intern_ctx, &benchmark_cat);
            name_iid = PerfettoTeLlInternEventName(&intern_ctx, name);
            PerfettoTeLlInternContextDestroy(&intern_ctx);
          }
          {
            struct perfetto_protos_TrackEvent te_msg;
            perfetto_protos_TracePacket_begin_track_event(&trace_packet.msg,
                                                          &te_msg);
            perfetto_protos_TrackEvent_set_type(
                &te_msg,
                static_cast<enum perfetto_protos_TrackEvent_Type>(type));
            PerfettoTeLlWriteRegisteredCat(&te_msg, &benchmark_cat);
            PerfettoTeLlWriteInternedEventName(&te_msg, name_iid);
            {
              struct perfetto_protos_DebugAnnotation dbg_arg;
              perfetto_protos_TrackEvent_begin_debug_annotations(&te_msg,
                                                                 &dbg_arg);
              perfetto_protos_DebugAnnotation_set_cstr_name(&dbg_arg, "value");
              perfetto_protos_DebugAnnotation_set_uint_value(&dbg_arg, 42);
              perfetto_protos_TrackEvent_end_debug_annotations(&te_msg,
                                                               &dbg_arg);
            }
            perfetto_protos_TracePacket_end_track_event(&trace_packet.msg,
                                                        &te_msg);
          }
          PerfettoTeLlPacketEnd(&ctx, &trace_packet);
        }
      }
    }
    benchmark::ClobberMemory();
  }
}

}  // namespace

BENCHMARK(BM_Shlib_DataSource_Disabled);
BENCHMARK(BM_Shlib_DataSource_DifferentPacketSize)->Range(1, 1000);
BENCHMARK(BM_Shlib_TeDisabled);
BENCHMARK(BM_Shlib_TeBasic);
BENCHMARK(BM_Shlib_TeBasicNoIntern);
BENCHMARK(BM_Shlib_TeDebugAnnotations);
BENCHMARK(BM_Shlib_TeCustomProto);
BENCHMARK(BM_Shlib_TeLlBasic);
BENCHMARK(BM_Shlib_TeLlBasicNoIntern);
BENCHMARK(BM_Shlib_TeLlDebugAnnotations);
BENCHMARK(BM_Shlib_TeLlCustomProto);
