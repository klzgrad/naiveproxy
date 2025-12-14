/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_UTILS_H_
#define SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_UTILS_H_

#include "perfetto/public/abi/track_event_abi.h"
#include "protos/perfetto/common/track_event_descriptor.pbzero.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"

namespace perfetto::shlib {

bool IsSingleCategoryEnabled(const PerfettoTeCategoryDescriptor&,
                             const perfetto::protos::gen::TrackEventConfig&);

void SerializeCategory(const PerfettoTeCategoryDescriptor& desc,
                       perfetto::protos::pbzero::TrackEventDescriptor* ted);

}  // namespace perfetto::shlib

#endif  // SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_UTILS_H_
