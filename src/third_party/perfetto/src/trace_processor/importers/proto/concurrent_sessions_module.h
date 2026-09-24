/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_CONCURRENT_SESSIONS_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_CONCURRENT_SESSIONS_MODULE_H_

#include <cstdint>

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// Turns ConcurrentSessionEvent packets (emitted by traced to record the state
// changes of other concurrently active tracing sessions) into one state track
// per session.
class ConcurrentSessionsModule : public ProtoImporterModule {
 public:
  ConcurrentSessionsModule(ProtoImporterModuleContext* module_context,
                           TraceProcessorContext* context);
  ~ConcurrentSessionsModule() override = default;

  void ParseField(const ParseFieldArgs& args) override;

 private:
  void ParseConcurrentSessionEvent(int64_t ts, protozero::ConstBytes blob);

  TraceProcessorContext* const context_;

  const StringId arg_consumer_uid_;
  const StringId arg_num_data_sources_;

  // State names, same strings as TracingServiceState.TracingSession.state.
  const StringId state_disabled_;
  const StringId state_configured_;
  const StringId state_started_;
  const StringId state_disabling_waiting_stop_acks_;
  const StringId state_cloned_read_only_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_CONCURRENT_SESSIONS_MODULE_H_
