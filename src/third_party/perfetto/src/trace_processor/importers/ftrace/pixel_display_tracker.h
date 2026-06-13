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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_PIXEL_DISPLAY_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_PIXEL_DISPLAY_TRACKER_H_

#include <cstddef>
#include <cstdint>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/descriptors.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class PixelDisplayTracker {
 public:
  explicit PixelDisplayTracker(TraceProcessorContext*);

  void ParseDpuDispFrameStartTimeout(int64_t timestamp, protozero::ConstBytes);
  void ParseDpuDispFrameDoneTimeout(int64_t timestamp, protozero::ConstBytes);
  void ParseDpuDispFrameStartMissing(int64_t timestamp, protozero::ConstBytes);
  void ParseDpuDispFrameDoneMissing(int64_t timestamp, protozero::ConstBytes);
  void ParseDpuDispVblankIrqEnable(int64_t timestamp, protozero::ConstBytes);

 private:
  TraceProcessorContext* context_;
  // track names
  const StringId frame_start_timeout_name_;
  const StringId frame_done_timeout_name_;
  const StringId frame_start_missing_name_;
  const StringId frame_done_missing_name_;
  const StringId vblank_irq_enable_name_;

  // event arguments
  const StringId display_id_arg_;
  const StringId output_id_arg_;
  const StringId frames_pending_arg_;
  const StringId te_count_arg_;
  const StringId during_disable_arg_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_PIXEL_DISPLAY_TRACKER_H_
