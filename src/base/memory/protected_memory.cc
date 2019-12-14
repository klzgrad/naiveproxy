// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/protected_memory.h"
#include "base/synchronization/lock.h"

namespace base {

#if !defined(COMPONENT_BUILD)
PROTECTED_MEMORY_SECTION int AutoWritableMemory::writers = 0;
#endif  // !defined(COMPONENT_BUILD)

base::LazyInstance<Lock>::Leaky AutoWritableMemory::writers_lock =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace base
