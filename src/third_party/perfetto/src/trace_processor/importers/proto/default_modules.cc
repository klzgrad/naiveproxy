/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/default_modules.h"
#include "src/trace_processor/importers/etw/etw_module.h"
#include "src/trace_processor/importers/ftrace/ftrace_module.h"
#include "src/trace_processor/importers/proto/chrome_system_probes_module.h"
#include "src/trace_processor/importers/proto/memory_tracker_snapshot_module.h"
#include "src/trace_processor/importers/proto/metadata_minimal_module.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/track_event_module.h"

namespace perfetto {
namespace trace_processor {

void RegisterDefaultModules(TraceProcessorContext* context) {
  // Ftrace and Etw modules are special, because they have an extra method for
  // parsing the ftrace/etw packets. So we need to store a pointer to it
  // separately.
  context->modules.emplace_back(new FtraceModule());
  context->ftrace_module =
      static_cast<FtraceModule*>(context->modules.back().get());
  context->modules.emplace_back(new EtwModule());
  context->etw_module = static_cast<EtwModule*>(context->modules.back().get());

  context->modules.emplace_back(new TrackEventModule(context));
  context->track_module =
      static_cast<TrackEventModule*>(context->modules.back().get());

  context->modules.emplace_back(new MemoryTrackerSnapshotModule(context));
  context->modules.emplace_back(new ChromeSystemProbesModule(context));
  context->modules.emplace_back(new MetadataMinimalModule(context));
}

}  // namespace trace_processor
}  // namespace perfetto
