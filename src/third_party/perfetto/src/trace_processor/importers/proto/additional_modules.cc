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

#include "src/trace_processor/importers/proto/additional_modules.h"

#include <memory>

#include "perfetto/base/build_config.h"
#include "src/trace_processor/importers/etw/etw_module_impl.h"
#include "src/trace_processor/importers/ftrace/ftrace_module_impl.h"
#include "src/trace_processor/importers/generic_kernel/generic_kernel_module.h"
#include "src/trace_processor/importers/proto/android_camera_event_module.h"
#include "src/trace_processor/importers/proto/android_kernel_wakelocks_module.h"
#include "src/trace_processor/importers/proto/android_probes_module.h"
#include "src/trace_processor/importers/proto/app_wakelock_module.h"
#include "src/trace_processor/importers/proto/content_analyzer.h"
#include "src/trace_processor/importers/proto/deobfuscation_module.h"
#include "src/trace_processor/importers/proto/graphics_event_module.h"
#include "src/trace_processor/importers/proto/heap_graph_module.h"
#include "src/trace_processor/importers/proto/metadata_module.h"
#include "src/trace_processor/importers/proto/multi_machine_trace_manager.h"
#include "src/trace_processor/importers/proto/network_trace_module.h"
#include "src/trace_processor/importers/proto/pixel_modem_module.h"
#include "src/trace_processor/importers/proto/profile_module.h"
#include "src/trace_processor/importers/proto/statsd_module.h"
#include "src/trace_processor/importers/proto/system_probes_module.h"
#include "src/trace_processor/importers/proto/trace.descriptor.h"
#include "src/trace_processor/importers/proto/translation_table_module.h"
#include "src/trace_processor/importers/proto/v8_module.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
#include "src/trace_processor/importers/proto/winscope/winscope_module.h"
#endif

namespace perfetto::trace_processor {

void RegisterAdditionalModules(TraceProcessorContext* context) {
  // Content analyzer and metadata module both depend on this.
  context->descriptor_pool_->AddFromFileDescriptorSet(kTraceDescriptor.data(),
                                                      kTraceDescriptor.size());

  context->modules.emplace_back(new AndroidKernelWakelocksModule(context));
  context->modules.emplace_back(new AndroidProbesModule(context));
  context->modules.emplace_back(new NetworkTraceModule(context));
  context->modules.emplace_back(new GraphicsEventModule(context));
  context->modules.emplace_back(new HeapGraphModule(context));
  context->modules.emplace_back(new DeobfuscationModule(context));
  context->modules.emplace_back(new SystemProbesModule(context));
  context->modules.emplace_back(new TranslationTableModule(context));
  context->modules.emplace_back(new StatsdModule(context));
  context->modules.emplace_back(new AndroidCameraEventModule(context));
  context->modules.emplace_back(new MetadataModule(context));
  context->modules.emplace_back(new V8Module(context));
  context->modules.emplace_back(new PixelModemModule(context));
  context->modules.emplace_back(new ProfileModule(context));
  context->modules.emplace_back(new AppWakelockModule(context));
  context->modules.emplace_back(new GenericKernelModule(context));

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  context->modules.emplace_back(new WinscopeModule(context));
#endif

  // Ftrace/Etw modules are special, because it has one extra method for parsing
  // ftrace/etw packets. So we need to store a pointer to it separately.
  context->modules.emplace_back(new FtraceModuleImpl(context));
  context->ftrace_module =
      static_cast<FtraceModule*>(context->modules.back().get());
  context->modules.emplace_back(new EtwModuleImpl(context));
  context->etw_module = static_cast<EtwModule*>(context->modules.back().get());

  if (context->multi_machine_trace_manager) {
    context->multi_machine_trace_manager->EnableAdditionalModules(
        RegisterAdditionalModules);
  }

  if (context->config.analyze_trace_proto_content) {
    context->content_analyzer = std::make_unique<ProtoContentAnalyzer>(context);
  }
}

}  // namespace perfetto::trace_processor
