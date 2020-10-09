// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/partition_alloc_features.h"

#include "base/feature_list.h"

namespace base {

// If enabled, PartitionAllocator reserves an address space(named, giga cage)
// initially and uses a part of the address space for each allocation.
const Feature kPartitionAllocGigaCage{"PartitionAllocGigaCage",
                                      FEATURE_DISABLED_BY_DEFAULT};

}  // namespace base
