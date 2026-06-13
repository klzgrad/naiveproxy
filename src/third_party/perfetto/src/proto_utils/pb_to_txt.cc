/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "src/proto_utils/pb_to_txt.h"

#include <string>

#include "src/proto_utils/config.descriptor.h"
#include "src/proto_utils/trace_summary.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/protozero_to_text.h"

namespace perfetto {
namespace {

std::string PbToTxt(const uint8_t* desc_data,
                    size_t desc_size,
                    const char* msg_type,
                    const void* data,
                    size_t size) {
  trace_processor::DescriptorPool pool;
  pool.AddFromFileDescriptorSet(desc_data, desc_size);
  return trace_processor::protozero_to_text::ProtozeroToText(
      pool, msg_type,
      protozero::ConstBytes{static_cast<const uint8_t*>(data), size},
      trace_processor::protozero_to_text::kIncludeNewLines);
}

}  // namespace

std::string TraceConfigPbToTxt(const void* data, size_t size) {
  return PbToTxt(kConfigDescriptor.data(), kConfigDescriptor.size(),
                 ".perfetto.protos.TraceConfig", data, size);
}

std::string TraceSummarySpecPbToTxt(const void* data, size_t size) {
  return PbToTxt(kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size(),
                 ".perfetto.protos.TraceSummarySpec", data, size);
}

}  // namespace perfetto
