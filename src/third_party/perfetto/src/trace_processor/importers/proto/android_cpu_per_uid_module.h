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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_MODULE_H_

#include <cstdint>
#include <string>

#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class AndroidCpuPerUidModule : public ProtoImporterModule {
 public:
  explicit AndroidCpuPerUidModule(ProtoImporterModuleContext* module_context,
                                  TraceProcessorContext* context);

  ~AndroidCpuPerUidModule() override;

  void ParseTracePacketData(const protos::pbzero::TracePacket::Decoder& decoder,
                            int64_t ts,
                            const TracePacketData&,
                            uint32_t field_id) override;

 private:
  void UpdateCounter(int64_t ts,
                     uint32_t uid,
                     uint32_t cluster,
                     uint64_t value);

  TraceProcessorContext* context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_MODULE_H_
