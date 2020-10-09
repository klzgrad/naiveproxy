// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_
#define BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_

#include <functional>
#include <tuple>
#include <utility>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace base {
namespace test {

// TODO(crbug.com/752720): Simplify using std::apply once C++17 is available.
template <typename CallbackFunc, typename ArgTuple, size_t... I>
decltype(auto) RunOnceCallbackUnwrapped(CallbackFunc&& f,
                                        ArgTuple&& t,
                                        std::index_sequence<I...>) {
  return std::move(f).Run(std::get<I>(t)...);
}

// TODO(crbug.com/752720): Simplify using std::apply once C++17 is available.
template <typename CallbackFunc, typename ArgTuple, size_t... I>
decltype(auto) RunRepeatingCallbackUnwrapped(CallbackFunc&& f,
                                             ArgTuple&& t,
                                             std::index_sequence<I...>) {
  return f.Run(std::get<I>(t)...);
}

// Functor used for RunOnceClosure<N>() and RunOnceCallback<N>() actions.
template <size_t I, typename... Vals>
struct RunOnceCallbackAction {
  std::tuple<Vals...> vals;

  template <typename... Args>
  decltype(auto) operator()(Args&&... args) {
    constexpr size_t size = std::tuple_size<decltype(vals)>::value;
    return RunOnceCallbackUnwrapped(
        std::get<I>(std::forward_as_tuple(std::forward<Args>(args)...)),
        std::move(vals), std::make_index_sequence<size>{});
  }
};

// Functor used for RunClosure<N>() and RunCallback<N>() actions.
template <size_t I, typename... Vals>
struct RunRepeatingCallbackAction {
  std::tuple<Vals...> vals;

  template <typename... Args>
  decltype(auto) operator()(Args&&... args) {
    constexpr size_t size = std::tuple_size<decltype(vals)>::value;
    return RunRepeatingCallbackUnwrapped(
        std::get<I>(std::forward_as_tuple(std::forward<Args>(args)...)),
        std::move(vals), std::make_index_sequence<size>{});
  }
};

// Matchers for base::{Once,Repeating}Callback and
// base::{Once,Repeating}Closure.
MATCHER(IsNullCallback, "a null callback") {
  return (arg.is_null());
}

MATCHER(IsNotNullCallback, "a non-null callback") {
  return (!arg.is_null());
}

// The Run[Once]Closure() action invokes the Run() method on the closure
// provided when the action is constructed. Function arguments passed when the
// action is run will be ignored.
ACTION_P(RunClosure, closure) {
  closure.Run();
}

// This action can be invoked at most once. Any further invocation will trigger
// a CHECK failure.
inline auto RunOnceClosure(base::OnceClosure cb) {
  // Mock actions need to be copyable, but OnceClosure is not. Wrap the closure
  // in a base::RefCountedData<> to allow it to be copied. An alternative would
  // be to use AdaptCallbackForRepeating(), but that allows the closure to be
  // run more than once and silently ignores any invocation after the first.
  // Since this is for use by tests, it's better to crash or CHECK-fail and
  // surface the incorrect usage, rather than have a silent unexpected success.
  using RefCountedOnceClosure = base::RefCountedData<base::OnceClosure>;
  scoped_refptr<RefCountedOnceClosure> copyable_cb =
      base::MakeRefCounted<RefCountedOnceClosure>(std::move(cb));
  return [copyable_cb](auto&&...) {
    CHECK(copyable_cb->data);
    std::move(copyable_cb->data).Run();
  };
}

// The Run[Once]Closure<N>() action invokes the Run() method on the N-th
// (0-based) argument of the mock function.
template <size_t I>
RunRepeatingCallbackAction<I> RunClosure() {
  return {};
}

template <size_t I>
RunOnceCallbackAction<I> RunOnceClosure() {
  return {};
}

// The Run[Once]Callback<N>(p1, p2, ..., p_k) action invokes the Run() method on
// the N-th (0-based) argument of the mock function, with arguments p1, p2, ...,
// p_k.
//
// Notes:
//
//   1. The arguments are passed by value by default.  If you need to
//   pass an argument by reference, wrap it inside ByRef().  For example,
//
//     RunCallback<1>(5, string("Hello"), ByRef(foo))
//
//   passes 5 and string("Hello") by value, and passes foo by reference.
//
//   2. If the callback takes an argument by reference but ByRef() is
//   not used, it will receive the reference to a copy of the value,
//   instead of the original value.  For example, when the 0-th
//   argument of the callback takes a const string&, the action
//
//     RunCallback<0>(string("Hello"))
//
//   makes a copy of the temporary string("Hello") object and passes a
//   reference of the copy, instead of the original temporary object,
//   to the callback.  This makes it easy for a user to define an
//   RunCallback action from temporary values and have it performed later.
template <size_t I, typename... Vals>
RunOnceCallbackAction<I, std::decay_t<Vals>...> RunOnceCallback(
    Vals&&... vals) {
  return {std::forward_as_tuple(std::forward<Vals>(vals)...)};
}

template <size_t I, typename... Vals>
RunRepeatingCallbackAction<I, std::decay_t<Vals>...> RunCallback(
    Vals&&... vals) {
  return {std::forward_as_tuple(std::forward<Vals>(vals)...)};
}

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_
