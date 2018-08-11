// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sequence_checker_impl.h"

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/sequence_token.h"
#include "base/threading/thread_checker_impl.h"

namespace base {

class SequenceCheckerImpl::Core {
 public:
  Core() : sequence_token_(SequenceToken::GetForCurrentThread()) {}

  ~Core() = default;

  bool CalledOnValidSequence() const {
    if (sequence_token_.IsValid())
      return sequence_token_ == SequenceToken::GetForCurrentThread();

    // SequenceChecker behaves as a ThreadChecker when it is not bound to a
    // valid sequence token.
    return thread_checker_.CalledOnValidThread();
  }

 private:
  SequenceToken sequence_token_;

  // Used when |sequence_token_| is invalid.
  ThreadCheckerImpl thread_checker_;
};

SequenceCheckerImpl::SequenceCheckerImpl() : core_(std::make_unique<Core>()) {}
SequenceCheckerImpl::~SequenceCheckerImpl() = default;

bool SequenceCheckerImpl::CalledOnValidSequence() const {
  AutoLock auto_lock(lock_);
  if (!core_)
    core_ = std::make_unique<Core>();
  return core_->CalledOnValidSequence();
}

void SequenceCheckerImpl::DetachFromSequence() {
  AutoLock auto_lock(lock_);
  core_.reset();
}

}  // namespace base
