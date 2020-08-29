// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_CHECK_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_CHECK_H_

#include "base/allocator/buildflags.h"
#include "base/check.h"

// When PartitionAlloc is used as the default allocator, we cannot use the
// regular (D)CHECK() macros, as they allocate internally. When an assertion is
// triggered, they format strings, leading to reentrency in the code, which none
// of PartitionAlloc is designed to support (and especially not for error
// paths).
//
// As a consequence:
// - When PartitionAlloc is not malloc(), use the regular macros
// - Otherwise, crash immediately. This provides worse error messages though.
#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
// See base/check.h for implementation details.
#define PA_CHECK(condition) \
  UNLIKELY(!(condition)) ? IMMEDIATE_CRASH() : EAT_CHECK_STREAM_PARAMS()

#if DCHECK_IS_ON()
#define PA_DCHECK(condition) PA_CHECK(condition)
#else
#define PA_DCHECK(condition) EAT_CHECK_STREAM_PARAMS(!(condition))
#endif  // DCHECK_IS_ON()

#else
#define PA_CHECK(condition) CHECK(condition)
#define PA_DCHECK(condition) DCHECK(condition)
#endif  // BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_ALLOC_CHECK_H_
