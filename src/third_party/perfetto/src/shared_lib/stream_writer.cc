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

#include "src/shared_lib/stream_writer.h"

#include <algorithm>

#include "perfetto/base/compiler.h"
#include "perfetto/protozero/contiguous_memory_range.h"
#include "perfetto/protozero/scattered_stream_writer.h"
#include "perfetto/public/abi/stream_writer_abi.h"

void PerfettoStreamWriterUpdateWritePtr(struct PerfettoStreamWriter* w) {
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  sw->set_write_ptr(w->write_ptr);
}

void PerfettoStreamWriterNewChunk(struct PerfettoStreamWriter* w) {
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  sw->set_write_ptr(w->write_ptr);
  sw->Extend();
  perfetto::UpdateStreamWriter(*sw, w);
}

uint8_t* PerfettoStreamWriterAnnotatePatch(struct PerfettoStreamWriter* w,
                                           uint8_t* patch_addr) {
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  static_assert(PERFETTO_STREAM_WRITER_PATCH_SIZE ==
                    protozero::ScatteredStreamWriter::Delegate::kPatchSize,
                "Size mismatch");
  memset(patch_addr, 0, PERFETTO_STREAM_WRITER_PATCH_SIZE);
  return sw->AnnotatePatch(patch_addr);
}

void PerfettoStreamWriterAppendBytesSlowpath(struct PerfettoStreamWriter* w,
                                             const uint8_t* src,
                                             size_t size) {
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  sw->set_write_ptr(w->write_ptr);
  sw->WriteBytesSlowPath(src, size);
  perfetto::UpdateStreamWriter(*sw, w);
}

void PerfettoStreamWriterReserveBytesSlowpath(struct PerfettoStreamWriter* w,
                                              size_t size) {
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  sw->set_write_ptr(w->write_ptr);
  sw->ReserveBytes(size);
  perfetto::UpdateStreamWriter(*sw, w);
}
