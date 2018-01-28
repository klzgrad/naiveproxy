// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_REF_COUNTED_DELETE_ON_SEQUENCE_H_
#define BASE_MEMORY_REF_COUNTED_DELETE_ON_SEQUENCE_H_

#include <utility>

#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"

namespace base {

// RefCountedDeleteOnSequence is similar to RefCountedThreadSafe, and ensures
// that the object will be deleted on a specified sequence.
//
// Sample usage:
// class Foo : public RefCountedDeleteOnSequence<Foo> {
//
//   Foo(scoped_refptr<SequencedTaskRunner> task_runner)
//       : RefCountedDeleteOnSequence<Foo>(std::move(task_runner)) {}
//   ...
//  private:
//   friend class RefCountedDeleteOnSequence<Foo>;
//   friend class DeleteHelper<Foo>;
//
//   ~Foo();
// };
template <class T>
class RefCountedDeleteOnSequence : public subtle::RefCountedThreadSafeBase {
 public:
  static constexpr subtle::StartRefCountFromZeroTag kRefCountPreference =
      subtle::kStartRefCountFromZeroTag;

  // A SequencedTaskRunner for the current sequence can be acquired by calling
  // SequencedTaskRunnerHandle::Get().
  RefCountedDeleteOnSequence(scoped_refptr<SequencedTaskRunner> task_runner)
      : subtle::RefCountedThreadSafeBase(T::kRefCountPreference),
        task_runner_(std::move(task_runner)) {
    DCHECK(task_runner_);
  }

  void AddRef() const { subtle::RefCountedThreadSafeBase::AddRef(); }

  void Release() const {
    if (subtle::RefCountedThreadSafeBase::Release())
      DestructOnSequence();
  }

 protected:
  friend class DeleteHelper<RefCountedDeleteOnSequence>;
  ~RefCountedDeleteOnSequence() = default;

 private:
  void DestructOnSequence() const {
    const T* t = static_cast<const T*>(this);
    if (task_runner_->RunsTasksInCurrentSequence())
      delete t;
    else
      task_runner_->DeleteSoon(FROM_HERE, t);
  }

  const scoped_refptr<SequencedTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(RefCountedDeleteOnSequence);
};

}  // namespace base

#endif  // BASE_MEMORY_REF_COUNTED_DELETE_ON_SEQUENCE_H_
