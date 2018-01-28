// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_store_test_callbacks.h"

#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

CookieCallback::CookieCallback(base::Thread* run_in_thread)
    : run_in_thread_(run_in_thread), run_in_loop_(NULL) {}

CookieCallback::CookieCallback()
    : run_in_thread_(NULL), run_in_loop_(base::MessageLoop::current()) {}

CookieCallback::~CookieCallback() {}

void CookieCallback::CallbackEpilogue() {
  base::MessageLoop* expected_loop = NULL;
  if (run_in_thread_) {
    DCHECK(!run_in_loop_);
    expected_loop = run_in_thread_->message_loop();
  } else if (run_in_loop_) {
    expected_loop = run_in_loop_;
  }
  ASSERT_TRUE(expected_loop != NULL);

  EXPECT_EQ(expected_loop, base::MessageLoop::current());
  loop_to_quit_.Quit();
}

void CookieCallback::WaitUntilDone() {
  loop_to_quit_.Run();
}

StringResultCookieCallback::StringResultCookieCallback() {}
StringResultCookieCallback::StringResultCookieCallback(
    base::Thread* run_in_thread)
    : CookieCallback(run_in_thread) {}

NoResultCookieCallback::NoResultCookieCallback() {}
NoResultCookieCallback::NoResultCookieCallback(base::Thread* run_in_thread)
    : CookieCallback(run_in_thread) {}

GetCookieListCallback::GetCookieListCallback() {}
GetCookieListCallback::GetCookieListCallback(base::Thread* run_in_thread)
    : CookieCallback(run_in_thread) {}

GetCookieListCallback::~GetCookieListCallback() {}

void GetCookieListCallback::Run(const CookieList& cookies) {
  cookies_ = cookies;
  CallbackEpilogue();
}

}  // namespace net
