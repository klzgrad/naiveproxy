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

#include "src/trace_processor/importers/android_bugreport/android_log_event_parser_impl.h"

#include <cstdint>
#include <utility>

#include "src/trace_processor/importers/android_bugreport/android_log_event.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

AndroidLogEventParserImpl::~AndroidLogEventParserImpl() = default;

void AndroidLogEventParserImpl::ParseAndroidLogEvent(int64_t ts,
                                                     AndroidLogEvent event) {
  tables::AndroidLogTable::Row row;
  row.ts = ts;
  row.utid = context_->process_tracker->UpdateThread(event.tid, event.pid);
  row.prio = event.prio;
  row.tag = event.tag;
  row.msg = event.msg;
  context_->storage->mutable_android_log_table()->Insert(std::move(row));
}

}  // namespace perfetto::trace_processor
