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
#ifndef INCLUDE_PERFETTO_EXT_PROFILING_SMAPS_H_
#define INCLUDE_PERFETTO_EXT_PROFILING_SMAPS_H_

#include <stdio.h>

#include "protos/perfetto/config/profiling/smaps_config.gen.h"
#include "protos/perfetto/trace/profiling/smaps.pbzero.h"

namespace perfetto {
namespace profiling {

void ParseAndSerializeSmaps(FILE* file,
                            const protos::gen::SmapsConfig& config,
                            protos::pbzero::SmapsPacket* packet);

}  // namespace profiling
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_PROFILING_SMAPS_H_
