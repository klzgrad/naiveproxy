// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_STORE_TEST_CALLBACKS_H_
#define NET_COOKIES_COOKIE_STORE_TEST_CALLBACKS_H_

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_store.h"

namespace base {
class MessageLoop;
class Thread;
}

namespace net {

// Defines common behaviour for the callbacks from GetCookies, SetCookies, etc.
// Asserts that the current thread is the expected invocation thread, sends a
// quit to the thread in which it was constructed.
class CookieCallback {
 public:
  // Waits until the callback is invoked.
  void WaitUntilDone();

 protected:
  // Constructs a callback that expects to be called in the given thread.
  explicit CookieCallback(base::Thread* run_in_thread);

  // Constructs a callback that expects to be called in current thread and will
  // send a QUIT to the constructing thread.
  CookieCallback();

  ~CookieCallback();

  // Tests whether the current thread was the caller's thread.
  // Sends a QUIT to the constructing thread.
  void CallbackEpilogue();

 private:
  base::Thread* run_in_thread_;
  base::MessageLoop* run_in_loop_;
  base::RunLoop loop_to_quit_;
};

// Callback implementations for the asynchronous CookieStore methods.

template <typename T>
class ResultSavingCookieCallback : public CookieCallback {
 public:
  ResultSavingCookieCallback() {
  }
  explicit ResultSavingCookieCallback(base::Thread* run_in_thread)
      : CookieCallback(run_in_thread) {
  }

  void Run(T result) {
    result_ = result;
    CallbackEpilogue();
  }

  const T& result() { return result_; }

 private:
  T result_;
};

class StringResultCookieCallback : public CookieCallback {
 public:
  StringResultCookieCallback();
  explicit StringResultCookieCallback(base::Thread* run_in_thread);

  void Run(const std::string& result) {
    result_ = result;
    CallbackEpilogue();
  }

  const std::string& result() { return result_; }

 private:
  std::string result_;
};

class NoResultCookieCallback : public CookieCallback {
 public:
  NoResultCookieCallback();
  explicit NoResultCookieCallback(base::Thread* run_in_thread);

  void Run() {
    CallbackEpilogue();
  }
};

class GetCookieListCallback : public CookieCallback {
 public:
  GetCookieListCallback();
  explicit GetCookieListCallback(base::Thread* run_in_thread);

  ~GetCookieListCallback();

  void Run(const CookieList& cookies);

  const CookieList& cookies() { return cookies_; }

 private:
  CookieList cookies_;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_STORE_TEST_CALLBACKS_H_
