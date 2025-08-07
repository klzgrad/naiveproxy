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

#include "src/trace_processor/importers/art_method/art_method_parser_impl.h"

#include <cstdint>

#include "src/trace_processor/importers/art_method/art_method_event.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor::art_method {

ArtMethodParserImpl::ArtMethodParserImpl(TraceProcessorContext* context)
    : context_(context),
      pathname_id_(context->storage->InternString("pathname")),
      line_number_id_(context->storage->InternString("line_number")) {}

ArtMethodParserImpl::~ArtMethodParserImpl() = default;

void ArtMethodParserImpl::ParseArtMethodEvent(int64_t ts, ArtMethodEvent e) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(e.tid);
  if (e.comm) {
    context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
        e.tid, *e.comm, ThreadNamePriority::kOther);
  }
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  switch (e.action) {
    case ArtMethodEvent::kEnter:
      context_->slice_tracker->Begin(
          ts, track_id, kNullStringId, e.method,
          [this, &e](ArgsTracker::BoundInserter* i) {
            if (e.pathname) {
              i->AddArg(pathname_id_, Variadic::String(*e.pathname));
            }
            if (e.line_number) {
              i->AddArg(line_number_id_, Variadic::Integer(*e.line_number));
            }
          });
      break;
    case ArtMethodEvent::kExit:
      context_->slice_tracker->End(ts, track_id);
      break;
  }
}

}  // namespace perfetto::trace_processor::art_method
