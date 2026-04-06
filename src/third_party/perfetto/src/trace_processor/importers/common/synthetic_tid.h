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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYNTHETIC_TID_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYNTHETIC_TID_H_

#include <cstdint>

namespace perfetto {
namespace trace_processor {

// Returns a unique synthetic tid based on the provided |tid| and |pid|.
// This is useful in systems where the pid is unique but the tid is not.
// The returned synthetic tid has the following format:
// [PID(high 32-bits)][TID(low 32-bits)]
inline int64_t CreateSyntheticTid(int64_t tid, int64_t pid) {
  return (pid << 32) | tid;
}

// Returns true if the provided tid is constructed synthetically from both pid
// and tid. See CreateSyntheticTid() for details.
inline bool IsSyntheticTid(int64_t tid) {
  return tid & static_cast<int64_t>(0xffffffff00000000);
}

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYNTHETIC_TID_H_
