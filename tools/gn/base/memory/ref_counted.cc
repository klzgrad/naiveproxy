// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"

namespace base {

namespace subtle {

bool RefCountedThreadSafeBase::HasOneRef() const {
  return ref_count_.IsOne();
}

#if defined(ARCH_CPU_64_BIT)
void RefCountedBase::AddRefImpl() const {
  // Check if |ref_count_| overflow only on 64 bit archs since the number of
  // objects may exceed 2^32.
  // To avoid the binary size bloat, use non-inline function here.
  CHECK(++ref_count_ > 0);
}
#endif

#if !defined(ARCH_CPU_X86_FAMILY)
bool RefCountedThreadSafeBase::Release() const {
  return ReleaseImpl();
}
void RefCountedThreadSafeBase::AddRef() const {
  AddRefImpl();
}
#endif

}  // namespace subtle

}  // namespace base
