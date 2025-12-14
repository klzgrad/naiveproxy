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
#include "src/trace_processor/importers/etw/etw_module.h"
#include "src/trace_processor/importers/etw/etw_module_impl.h"
#include "src/trace_processor/importers/ftrace/ftrace_module.h"
#include "src/trace_processor/importers/ftrace/ftrace_module_impl.h"
#include "src/trace_processor/importers/generic_kernel/generic_kernel_module.h"
#include "src/trace_processor/importers/proto/android_camera_event_module.h"
#include "src/trace_processor/importers/proto/android_cpu_per_uid_module.h"
#include "src/trace_processor/importers/proto/android_kernel_wakelocks_module.h"
#include "src/trace_processor/importers/proto/android_probes_module.h"
#include "src/trace_processor/importers/proto/app_wakelock_module.h"
#include "src/trace_processor/importers/proto/content_analyzer.h"
#include "src/trace_processor/importers/proto/deobfuscation_module.h"
#include "src/trace_processor/importers/proto/graphics_event_module.h"
#include "src/trace_processor/importers/proto/heap_graph_module.h"
#include "src/trace_processor/importers/proto/metadata_module.h"
#include "src/trace_processor/importers/proto/network_trace_module.h"
#include "src/trace_processor/importers/proto/pixel_modem_module.h"
#include "src/trace_processor/importers/proto/profile_module.h"
#include "src/trace_processor/importers/proto/statsd_module.h"
#include "src/trace_processor/importers/proto/system_probes_module.h"
#include "src/trace_processor/importers/proto/trace.descriptor.h"
#include "src/trace_processor/importers/proto/translation_table_module.h"
#include "src/trace_processor/importers/proto/v8_module.h"
#include "src/trace_processor/types/trace_processor_context.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
#include "src/trace_processor/importers/proto/winscope/winscope_module.h"
#endif

namespace perfetto::trace_processor {

void RegisterAdditionalModules(ProtoImporterModuleContext* module_context,
                               TraceProcessorContext* context) {
  // Content analyzer and metadata module both depend on this.
  context->descriptor_pool_->AddFromFileDescriptorSet(kTraceDescriptor.data(),
                                                      kTraceDescriptor.size());

  module_context->modules.emplace_back(
      new AndroidCpuPerUidModule(module_context, context));
  module_context->modules.emplace_back(
      new AndroidKernelWakelocksModule(module_context, context));
  module_context->modules.emplace_back(
      new AndroidProbesModule(module_context, context));
  module_context->modules.emplace_back(
      new NetworkTraceModule(module_context, context));
  module_context->modules.emplace_back(
      new GraphicsEventModule(module_context, context));
  module_context->modules.emplace_back(
      new HeapGraphModule(module_context, context));
  module_context->modules.emplace_back(
      new DeobfuscationModule(module_context, context));
  module_context->modules.emplace_back(
      new SystemProbesModule(module_context, context));
  module_context->modules.emplace_back(
      new TranslationTableModule(module_context, context));
  module_context->modules.emplace_back(
      new StatsdModule(module_context, context));
  module_context->modules.emplace_back(
      new AndroidCameraEventModule(module_context, context));
  module_context->modules.emplace_back(
      new MetadataModule(module_context, context));
  module_context->modules.emplace_back(new V8Module(module_context, context));
  module_context->modules.emplace_back(
      new PixelModemModule(module_context, context));
  module_context->modules.emplace_back(
      new ProfileModule(module_context, context));
  module_context->modules.emplace_back(
      new AppWakelockModule(module_context, context));
  module_context->modules.emplace_back(
      new GenericKernelModule(module_context, context));

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  module_context->modules.emplace_back(
      new WinscopeModule(module_context, context));
#endif

  // Ftrace/Etw modules are special, because it has one extra method for parsing
  // ftrace/etw packets. So we need to store a pointer to it separately.
  module_context->modules.emplace_back(
      new FtraceModuleImpl(module_context, context));
  module_context->ftrace_module =
      static_cast<FtraceModule*>(module_context->modules.back().get());
  module_context->modules.emplace_back(
      new EtwModuleImpl(module_context, context));
  module_context->etw_module =
      static_cast<EtwModule*>(module_context->modules.back().get());

  if (context->config.analyze_trace_proto_content) {
    context->content_analyzer = std::make_unique<ProtoContentAnalyzer>(context);
  }
}

}  // namespace perfetto::trace_processor
