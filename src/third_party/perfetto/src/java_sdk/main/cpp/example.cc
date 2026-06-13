/*
 * Copyright (C) 2025 The Android Open Source Project
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

// This example demonstrates in-process tracing with Perfetto.
// This program adds trace in a few example functions like DrawPlayer DrawGame
// etc. and collect the trace in file `example.pftrace`.
//
// This file was copied from 'examples/sdk/example.cc' and migrated
// to use the 'libperfetto_c' API.

#include "src/java_sdk/main/cpp/example.h"

#include "src/java_sdk/main/cpp/utils.h"

#include "perfetto/public/producer.h"
#include "perfetto/public/te_category_macros.h"
#include "perfetto/public/te_macros.h"
#include "perfetto/public/track_event.h"

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

namespace {
#define EXAMPLE_CATEGORIES(C)                                    \
  C(rendering, "rendering", "Rendering and graphics events")     \
  C(network, "network.debug", "Verbose network events", "debug") \
  C(audio, "audio.latency", "Detailed audio latency metrics", "verbose")

PERFETTO_TE_CATEGORIES_DEFINE(EXAMPLE_CATEGORIES)

void InitializePerfetto() {
  PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
  args.backends = PERFETTO_BACKEND_IN_PROCESS;
  PerfettoProducerInit(args);
  PerfettoTeInit();
  PERFETTO_TE_REGISTER_CATEGORIES(EXAMPLE_CATEGORIES);
}

std::unique_ptr<perfetto::java_sdk::utils::TracingSession> StartTracing() {
  using perfetto::java_sdk::utils::TracingSession;
  TracingSession tracing_session = TracingSession::Builder()
                                       .set_data_source_name("track_event")
                                       .add_enabled_category("*")
                                       .Build();
  return std::make_unique<TracingSession>(std::move(tracing_session));
}

void StopTracing(
    std::unique_ptr<perfetto::java_sdk::utils::TracingSession> tracing_session,
    const std::string& output_file_path) {
  // Stop tracing and read the trace data.
  tracing_session->StopBlocking();
  std::vector<uint8_t> trace_data(tracing_session->ReadBlocking());

  // Write the result into a file.
  // Note: To save memory with longer traces, you can tell Perfetto to write
  // directly into a file by passing a file descriptor into Setup() above.
  std::ofstream output;
  output.open(output_file_path, std::ios::out | std::ios::binary);
  output.write(reinterpret_cast<const std::ostream::char_type*>(&trace_data[0]),
               static_cast<std::streamsize>(trace_data.size()));
  output.close();
  printf(
      "Trace written in %s file. To read this trace in "
      "text form, run `./tools/traceconv text example.pftrace`\n",
      output_file_path.c_str());
}

void DrawPlayer(int player_number) {
  PERFETTO_TE_SCOPED(rendering, PERFETTO_TE_SLICE("DrawPlayer"),
                     PERFETTO_TE_ARG_INT64("player_number", player_number));
  // Sleep to simulate a long computation.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void DrawGame() {
  // This is an example of an unscoped slice, which begins and ends at specific
  // points (instead of at the end of the current block scope).
  PERFETTO_TE(rendering, PERFETTO_TE_SLICE_BEGIN("DrawGame"));
  DrawPlayer(1);
  DrawPlayer(2);
  PERFETTO_TE(rendering, PERFETTO_TE_SLICE_END());

  // Record the rendering framerate as a counter sample.
  PERFETTO_TE(
      rendering, PERFETTO_TE_COUNTER(),
      PERFETTO_TE_COUNTER_TRACK("Framerate", PerfettoTeProcessTrackUuid()),
      PERFETTO_TE_INT_COUNTER(120));
}
}  // namespace

int run_main(const std::string& output_file_path) {
  InitializePerfetto();
  auto tracing_session = StartTracing();

  // Simulate some work that emits trace events.
  DrawGame();

  StopTracing(std::move(tracing_session), output_file_path);
  return 0;
}
