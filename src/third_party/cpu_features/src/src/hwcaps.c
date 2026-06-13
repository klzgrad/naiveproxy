// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "internal/hwcaps.h"

#include <stdbool.h>

static bool IsSet(const uint32_t mask, const uint32_t value) {
  if (mask == 0) return false;
  return (value & mask) == mask;
}

bool CpuFeatures_IsHwCapsSet(const HardwareCapabilities hwcaps_mask,
                             const HardwareCapabilities hwcaps) {
  return IsSet(hwcaps_mask.hwcaps, hwcaps.hwcaps) ||
         IsSet(hwcaps_mask.hwcaps2, hwcaps.hwcaps2);
}
