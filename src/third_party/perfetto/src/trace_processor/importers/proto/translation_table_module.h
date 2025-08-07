/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRANSLATION_TABLE_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRANSLATION_TABLE_MODULE_H_

#include <cstdint>
#include <optional>

#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

class TranslationTableModule : public ProtoImporterModule {
 public:
  explicit TranslationTableModule(TraceProcessorContext* context);

  ~TranslationTableModule() override;

  ModuleResult TokenizePacket(
      const protos::pbzero::TracePacket_Decoder& decoder,
      TraceBlobView* packet,
      int64_t packet_timestamp,
      RefPtr<PacketSequenceStateGeneration> state,
      uint32_t field_id) override;

 private:
  void ParseChromeHistogramRules(protozero::ConstBytes bytes);
  void ParseChromeUserEventRules(protozero::ConstBytes bytes);
  void ParseChromePerformanceMarkRules(protozero::ConstBytes bytes);
  void ParseSliceNameRules(protozero::ConstBytes bytes);
  void ParseProcessTrackNameRules(protozero::ConstBytes bytes);
  void ParseChromeStudyRules(protozero::ConstBytes bytes);

  TraceProcessorContext* context_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRANSLATION_TABLE_MODULE_H_
