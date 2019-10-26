// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_
#define BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_

#include <tuple>

#include "testing/gmock/include/gmock/gmock.h"

namespace base {
namespace test {

// Matchers for base::Callback and base::Closure.

MATCHER(IsNullCallback, "a null callback") {
  return (arg.is_null());
}

MATCHER(IsNotNullCallback, "a non-null callback") {
  return (!arg.is_null());
}

// The RunClosure<N>() action invokes Run() method on the N-th (0-based)
// argument of the mock function.

ACTION_TEMPLATE(RunClosure,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_0_VALUE_PARAMS()) {
  ::testing::get<k>(args).Run();
}

ACTION_P(RunClosure, closure) {
  closure.Run();
}

// Various overloads for RunCallback<N>().
//
// The RunCallback<N>(p1, p2, ..., p_k) action invokes Run() method on the N-th
// (0-based) argument of the mock function, with arguments p1, p2, ..., p_k.
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

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_0_VALUE_PARAMS()) {
  return ::testing::get<k>(args).Run();
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p0)) {
  return ::testing::get<k>(args).Run(p0);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_2_VALUE_PARAMS(p0, p1)) {
  return ::testing::get<k>(args).Run(p0, p1);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_3_VALUE_PARAMS(p0, p1, p2)) {
  return ::testing::get<k>(args).Run(p0, p1, p2);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_4_VALUE_PARAMS(p0, p1, p2, p3)) {
  return ::testing::get<k>(args).Run(p0, p1, p2, p3);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_5_VALUE_PARAMS(p0, p1, p2, p3, p4)) {
  return ::testing::get<k>(args).Run(p0, p1, p2, p3, p4);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_6_VALUE_PARAMS(p0, p1, p2, p3, p4, p5)) {
  return ::testing::get<k>(args).Run(p0, p1, p2, p3, p4, p5);
}

ACTION_TEMPLATE(RunCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_7_VALUE_PARAMS(p0, p1, p2, p3, p4, p5, p6)) {
  return ::testing::get<k>(args).Run(p0, p1, p2, p3, p4, p5, p6);
}

// Various overloads for RunOnceClosure and RunOnceCallback<N>(). These are
// mostly the same as RunClosure and RunCallback<N>() above except that they
// support the move-only base::OnceCallback types.

ACTION_TEMPLATE(RunOnceClosure,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_0_VALUE_PARAMS()) {
  std::move(::testing::get<k>(args)).Run();
}

ACTION_P(RunOnceClosure, closure) {
  std::move(closure).Run();
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_0_VALUE_PARAMS()) {
  return std::move(::testing::get<k>(args)).Run();
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p0)) {
  return std::move(::testing::get<k>(args)).Run(p0);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_2_VALUE_PARAMS(p0, p1)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_3_VALUE_PARAMS(p0, p1, p2)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1, p2);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_4_VALUE_PARAMS(p0, p1, p2, p3)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1, p2, p3);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_5_VALUE_PARAMS(p0, p1, p2, p3, p4)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1, p2, p3, p4);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_6_VALUE_PARAMS(p0, p1, p2, p3, p4, p5)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1, p2, p3, p4, p5);
}

ACTION_TEMPLATE(RunOnceCallback,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_7_VALUE_PARAMS(p0, p1, p2, p3, p4, p5, p6)) {
  return std::move(::testing::get<k>(args)).Run(p0, p1, p2, p3, p4, p5, p6);
}

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_GMOCK_CALLBACK_SUPPORT_H_
