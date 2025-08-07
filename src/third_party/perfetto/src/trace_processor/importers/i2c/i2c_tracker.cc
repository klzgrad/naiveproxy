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

#include "src/trace_processor/importers/i2c/i2c_tracker.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace trace_processor {

I2cTracker::I2cTracker(TraceProcessorContext* context) : context_(context) {
  for (size_t i = 0; i < kMaxI2cAdapters; i++) {
    StringId id = kNullStringId;
    base::StackString<255> adapter_name("i2c-%zu", i);
    id = context_->storage->InternString(adapter_name.string_view());
    i2c_adapter_to_string_id_[i] = id;
  }
}

I2cTracker::~I2cTracker() = default;

void I2cTracker::Enter(int64_t ts,
                       UniqueTid utid,
                       uint32_t adapter_nr,
                       uint32_t msg_nr) {
  StringId name = i2c_adapter_to_string_id_[adapter_nr];
  if (name.is_null())
    return;
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  std::vector<I2cAdapterMessageCount>& ops = inflight_i2c_ops_[utid];
  if (ops.empty()) {
    context_->slice_tracker->Begin(ts, track_id, kNullStringId, name);
    I2cAdapterMessageCount req_count;
    req_count.adapter_nr = adapter_nr;
    req_count.nr_msgs = msg_nr + 1;
    ops.push_back(req_count);
  } else {
    ops.back().nr_msgs = std::max(msg_nr + 1, ops.back().nr_msgs);
  }
}

void I2cTracker::Exit(int64_t ts,
                      UniqueTid utid,
                      uint32_t adapter_nr,
                      uint32_t nr_msgs) {
  StringId name = i2c_adapter_to_string_id_[adapter_nr];
  if (name.is_null())
    return;
  std::vector<I2cAdapterMessageCount>& ops = inflight_i2c_ops_[utid];
  if (ops.empty())
    return;
  if (ops.back().adapter_nr != adapter_nr || ops.back().nr_msgs != nr_msgs)
    return;
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  ops.pop_back();
  context_->slice_tracker->End(ts, track_id, kNullStringId, name);
}

}  // namespace trace_processor
}  // namespace perfetto
