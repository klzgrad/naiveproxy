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

#include "perfetto/public/abi/heap_buffer.h"

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/shared_lib/stream_writer.h"

struct PerfettoHeapBuffer* PerfettoHeapBufferCreate(
    struct PerfettoStreamWriter* w) {
  auto* shb = new protozero::ScatteredHeapBuffer(4096, 4096);
  auto* sw = new protozero::ScatteredStreamWriter(shb);
  shb->set_writer(sw);

  w->impl = reinterpret_cast<PerfettoStreamWriterImpl*>(sw);
  perfetto::UpdateStreamWriter(*sw, w);
  return reinterpret_cast<PerfettoHeapBuffer*>(shb);
}

void PerfettoHeapBufferCopyInto(struct PerfettoHeapBuffer* buf,
                                struct PerfettoStreamWriter* w,
                                void* dst,
                                size_t size) {
  auto* shb = reinterpret_cast<protozero::ScatteredHeapBuffer*>(buf);
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  sw->set_write_ptr(w->write_ptr);

  uint8_t* dst_ptr = reinterpret_cast<uint8_t*>(dst);
  for (const protozero::ScatteredHeapBuffer::Slice& slice : shb->GetSlices()) {
    if (size == 0) {
      break;
    }
    protozero::ContiguousMemoryRange used_range = slice.GetUsedRange();
    size_t to_copy = std::min(size, used_range.size());
    memcpy(dst_ptr, used_range.begin, to_copy);
    dst_ptr += to_copy;
    size -= to_copy;
  }
}

void PerfettoHeapBufferDestroy(struct PerfettoHeapBuffer* buf,
                               struct PerfettoStreamWriter* w) {
  auto* shb = reinterpret_cast<protozero::ScatteredHeapBuffer*>(buf);
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);
  delete sw;
  delete shb;
}
