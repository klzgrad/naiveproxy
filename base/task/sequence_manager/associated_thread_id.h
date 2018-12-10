// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_ASSOCIATED_THREAD_ID_H_
#define BASE_TASK_SEQUENCE_MANAGER_ASSOCIATED_THREAD_ID_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_checker.h"

namespace base {
namespace sequence_manager {
namespace internal {

// TODO(eseckler): Make this owned by SequenceManager once the TaskQueue
// refactor has happened (https://crbug.com/865411).
struct BASE_EXPORT AssociatedThreadId
    : public base::RefCountedThreadSafe<AssociatedThreadId> {
 public:
  PlatformThreadId thread_id = kInvalidThreadId;
  // TODO(eseckler): Replace thread_checker with sequence_checker everywhere.
  THREAD_CHECKER(thread_checker);
  SEQUENCE_CHECKER(sequence_checker);

  static scoped_refptr<AssociatedThreadId> CreateUnbound() {
    return MakeRefCounted<AssociatedThreadId>();
  }

  static scoped_refptr<AssociatedThreadId> CreateBound() {
    auto associated_thread = MakeRefCounted<AssociatedThreadId>();
    associated_thread->BindToCurrentThread();
    return associated_thread;
  }

  // Rebind the associated thread to the current thread. This allows creating
  // the SequenceManager and TaskQueues on a different thread/sequence than the
  // one it will manage. Should only be called once.
  void BindToCurrentThread() {
    DCHECK_EQ(kInvalidThreadId, thread_id);
    thread_id = PlatformThread::CurrentId();

    // Rebind the thread and sequence checkers to the current thread/sequence.
    DETACH_FROM_THREAD(thread_checker);
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker);

    DETACH_FROM_SEQUENCE(sequence_checker);
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker);
  }

  // TODO(eseckler): Add a method that checks that we are either bound already
  // or on the thread which created us and use it in any_thread() accessors.

 private:
  friend class base::RefCountedThreadSafe<AssociatedThreadId>;
  ~AssociatedThreadId() = default;
};

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_ASSOCIATED_THREAD_ID_H_
