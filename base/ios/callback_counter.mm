// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/ios/callback_counter.h"

CallbackCounter::CallbackCounter(const FinalCallback& final_callback)
    : callback_count_(0U), final_callback_(final_callback) {
  DCHECK(!final_callback.is_null());
}

CallbackCounter::~CallbackCounter() {
  DCHECK_EQ(0U, callback_count_);
}

void CallbackCounter::IncrementCount(int count) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!final_callback_.is_null());
  callback_count_ += count;
}

void CallbackCounter::IncrementCount() {
  IncrementCount(1);
}

void CallbackCounter::DecrementCount() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(callback_count_);

  --callback_count_;
  if (callback_count_ == 0) {
    final_callback_.Run();
    final_callback_.Reset();
  }
}
