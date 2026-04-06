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

#include "src/trace_processor/importers/proto/jit_tracker.h"

#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/jit_cache.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

JitTracker::JitTracker(TraceProcessorContext* context) : context_(context) {}
JitTracker::~JitTracker() = default;

JitCache* JitTracker::CreateJitCache(std::string name,
                                     UniquePid upid,
                                     AddressRange range) {
  auto cache =
      std::make_unique<JitCache>(context_, std::move(name), upid, range);
  JitCache* cache_ptr = cache.get();
  // Dealing with overlaps is complicated. Do we delete the entire range, only
  // the overlap, how do we deal with requests to the old JitCache. And it
  // doesn't really happen in practice (e.g. for v8 you would need to delete an
  // isolate and recreate it.), so just make sure our assumption (this never
  // happens) is correct with a check.
  PERFETTO_CHECK(caches_[upid].Emplace(range, std::move(cache)));
  context_->mapping_tracker->AddJitRange(upid, range, cache_ptr);
  return cache_ptr;
}

}  // namespace perfetto::trace_processor
