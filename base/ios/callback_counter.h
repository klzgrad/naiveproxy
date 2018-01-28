// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_CALLBACK_COUNTER_H_
#define IOS_CHROME_BROWSER_CALLBACK_COUNTER_H_

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread.h"

// A helper class that keeps count of the number of pending callbacks that need
// to be received. Calls |final_callback| when all callbacks have been received.
// All methods (except the destructor) must be called on the same thread.
class CallbackCounter : public base::RefCounted<CallbackCounter> {
 public:
  typedef base::Callback<void()> FinalCallback;

  explicit CallbackCounter(const FinalCallback& final_callback);

  // Increments the count of pending callbacks by |count| .
  void IncrementCount(int count);

  // Increments the count of pending callbacks by 1.
  void IncrementCount();

  // Decrements the count of pending callbacks.
  void DecrementCount();

 private:
  friend class base::RefCounted<CallbackCounter>;

  ~CallbackCounter();

  // The number of callbacks that still need to be received.
  unsigned callback_count_;
  // The callback that is finally called when all callbacks have been received
  // (when the |callback_count_| goes down to 0).
  FinalCallback final_callback_;
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(CallbackCounter);
};

#endif  // IOS_CHROME_BROWSER_CALLBACK_COUNTER_H_
