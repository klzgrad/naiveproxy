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

#include "src/trace_redaction/filtering.h"

namespace perfetto::trace_redaction {

PidFilter::~PidFilter() = default;

bool ConnectedToPackage::Includes(const Context& context,
                                  uint64_t ts,
                                  int32_t pid) const {
  PERFETTO_DCHECK(context.timeline);
  PERFETTO_DCHECK(context.package_uid.has_value());
  return context.timeline->PidConnectsToUid(ts, pid, *context.package_uid);
}

bool AllowAll::Includes(const Context&, uint64_t, int32_t) const {
  return true;
}

bool AllowAll::Includes(const Context&, protozero::Field) const {
  return true;
}

MatchesPid::MatchesPid(int32_t pid) : pid_(pid) {}

bool MatchesPid::Includes(const Context&, uint64_t, int32_t pid) const {
  return pid == pid_;
}

}  // namespace perfetto::trace_redaction
