/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/storage/trace_storage.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {

std::vector<NullTermStringView> CreateRefTypeStringMap() {
  std::vector<NullTermStringView> map(static_cast<size_t>(RefType::kRefMax));
  map[static_cast<size_t>(RefType::kRefNoRef)] = NullTermStringView();
  map[static_cast<size_t>(RefType::kRefUtid)] = "utid";
  map[static_cast<size_t>(RefType::kRefCpuId)] = "cpu";
  map[static_cast<size_t>(RefType::kRefGpuId)] = "gpu";
  map[static_cast<size_t>(RefType::kRefIrq)] = "irq";
  map[static_cast<size_t>(RefType::kRefSoftIrq)] = "softirq";
  map[static_cast<size_t>(RefType::kRefUpid)] = "upid";
  map[static_cast<size_t>(RefType::kRefTrack)] = "track";
  return map;
}

}  // namespace

const std::vector<NullTermStringView>& GetRefTypeStringMap() {
  static const base::NoDestructor<std::vector<NullTermStringView>> map(
      CreateRefTypeStringMap());
  return map.ref();
}

TraceStorage::TraceStorage(const Config&) {
  for (uint32_t i = 0; i < variadic_type_ids_.size(); ++i) {
    variadic_type_ids_[i] = InternString(Variadic::kTypeNames[i]);
  }
}

TraceStorage::~TraceStorage() {}

uint32_t TraceStorage::SqlStats::RecordQueryBegin(const std::string& query,
                                                  int64_t time_started) {
  if (queries_.size() >= kMaxLogEntries) {
    queries_.pop_front();
    times_started_.pop_front();
    times_first_next_.pop_front();
    times_ended_.pop_front();
    popped_queries_++;
  }
  queries_.push_back(query);
  times_started_.push_back(time_started);
  times_first_next_.push_back(0);
  times_ended_.push_back(0);
  return static_cast<uint32_t>(popped_queries_ + queries_.size() - 1);
}

void TraceStorage::SqlStats::RecordQueryFirstNext(uint32_t row,
                                                  int64_t time_first_next) {
  // This means we've popped this query off the queue of queries before it had
  // a chance to finish. Just silently drop this number.
  if (popped_queries_ > row)
    return;
  uint32_t queue_row = row - popped_queries_;
  PERFETTO_DCHECK(queue_row < queries_.size());
  times_first_next_[queue_row] = time_first_next;
}

void TraceStorage::SqlStats::RecordQueryEnd(uint32_t row, int64_t time_ended) {
  // This means we've popped this query off the queue of queries before it had
  // a chance to finish. Just silently drop this number.
  if (popped_queries_ > row)
    return;
  uint32_t queue_row = row - popped_queries_;
  PERFETTO_DCHECK(queue_row < queries_.size());
  times_ended_[queue_row] = time_ended;
}

}  // namespace perfetto::trace_processor
