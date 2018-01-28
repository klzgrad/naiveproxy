// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_SYNCHRONIZATION_ATOMIC_FLAG_H_
#define BASE_SYNCHRONIZATION_ATOMIC_FLAG_H_

#include "base/atomicops.h"
#include "base/base_export.h"
#include "base/macros.h"
#include "base/sequence_checker.h"

namespace base {

// A flag that can safely be set from one thread and read from other threads.
//
// This class IS NOT intended for synchronization between threads.
class BASE_EXPORT AtomicFlag {
 public:
  AtomicFlag();
  ~AtomicFlag() = default;

  // Set the flag. Must always be called from the same sequence.
  void Set();

  // Returns true iff the flag was set. If this returns true, the current thread
  // is guaranteed to be synchronized with all memory operations on the sequence
  // which invoked Set() up until at least the first call to Set() on it.
  bool IsSet() const;

  // Resets the flag. Be careful when using this: callers might not expect
  // IsSet() to return false after returning true once.
  void UnsafeResetForTesting();

 private:
  base::subtle::Atomic32 flag_ = 0;
  SequenceChecker set_sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(AtomicFlag);
};

}  // namespace base

#endif  // BASE_SYNCHRONIZATION_ATOMIC_FLAG_H_
