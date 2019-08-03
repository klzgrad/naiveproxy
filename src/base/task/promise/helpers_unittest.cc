// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/helpers.h"

#include "base/bind.h"
#include "base/task/promise/promise.h"
#include "base/test/bind_test_util.h"
#include "base/test/do_nothing_promise.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

TEST(UseMoveSemantics, GeneralTypes) {
  static_assert(!UseMoveSemantics<int>::value,
                "Integral types don't need to be moved");

  static_assert(UseMoveSemantics<std::unique_ptr<int>>::value,
                "Move only types should be moved");

  static_assert(
      !UseMoveSemantics<scoped_refptr<AbstractPromise>>::value,
      "To support multiple callbacks scoped_refptr doesn't need to be moved.");
}

TEST(CallbackTraits, CallbackReferenceTypes) {
  static_assert(
      std::is_same<int&,
                   CallbackTraits<Callback<int&(int&)>>::ResolveType>::value,
      "");

  static_assert(
      std::is_same<int&, CallbackTraits<Callback<int&(int&)>>::ArgType>::value,
      "");
}

TEST(CallbackTraits, RepeatingCallbackReferenceTypes) {
  static_assert(
      std::is_same<
          int&,
          CallbackTraits<RepeatingCallback<int&(int&)>>::ResolveType>::value,
      "");

  static_assert(
      std::is_same<
          int&, CallbackTraits<RepeatingCallback<int&(int&)>>::ArgType>::value,
      "");
}

TEST(PromiseCombiner, InvalidCombos) {
  static_assert(!PromiseCombiner<Resolved<int>, Rejected<float>, Resolved<int>,
                                 Rejected<bool>>::valid,
                "Invalid, reject types differ");
  static_assert(!PromiseCombiner<Resolved<int>, Rejected<float>, Resolved<void>,
                                 Rejected<float>>::valid,
                "Invalid, resolve types differ");
}

TEST(PromiseCombiner, TypesMatch) {
  using Result = PromiseCombiner<Resolved<int>, Rejected<float>, Resolved<int>,
                                 Rejected<float>>;
  static_assert(Result::valid, "Types are the same, should match");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, NoResolve) {
  using Result = PromiseCombiner<NoResolve, Rejected<float>, Resolved<int>,
                                 Rejected<float>>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, NoResolve2) {
  using Result = PromiseCombiner<Resolved<int>, Rejected<float>, NoResolve,
                                 Rejected<float>>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, BothNoResolve) {
  using Result =
      PromiseCombiner<NoResolve, Rejected<float>, NoResolve, Rejected<float>>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, NoResolve>::value,
                "Resolve type should be NoResolve");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, NoReject) {
  using Result =
      PromiseCombiner<Resolved<int>, NoReject, Resolved<int>, Rejected<float>>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, NoReject2) {
  using Result =
      PromiseCombiner<Resolved<int>, Rejected<float>, Resolved<int>, NoReject>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, BothNoReject) {
  using Result =
      PromiseCombiner<Resolved<int>, NoReject, Resolved<int>, NoReject>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, NoReject>::value,
                "Resolve type should be NoReject");
}

TEST(PromiseCombiner, NoResolveAndNoReject) {
  using Result =
      PromiseCombiner<Resolved<int>, NoReject, NoResolve, Rejected<float>>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(PromiseCombiner, NoResolveAndNoReject2) {
  using Result =
      PromiseCombiner<NoResolve, Rejected<float>, Resolved<int>, NoReject>;
  static_assert(Result::valid, "Valid combination");

  static_assert(std::is_same<Result::ResolveType, Resolved<int>>::value,
                "Resolve type should be int");

  static_assert(std::is_same<Result::RejectType, Rejected<float>>::value,
                "Resolve type should be float");
}

TEST(EmplaceHelper, EmplacePromiseResult) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> reject = DoNothingPromiseBuilder(FROM_HERE);

  EmplaceHelper<Resolved<int>, Rejected<std::string>>::Emplace(
      resolve.get(), PromiseResult<int, std::string>(123));
  EmplaceHelper<Resolved<int>, Rejected<std::string>>::Emplace(
      reject.get(), PromiseResult<int, std::string>("Oh no!"));

  EXPECT_EQ(unique_any_cast<Resolved<int>>(resolve->value()).value, 123);
  EXPECT_EQ(unique_any_cast<Rejected<std::string>>(reject->value()).value,
            "Oh no!");
}

TEST(EmplaceHelper, EmplacePromise) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> curried = DoNothingPromiseBuilder(FROM_HERE);

  EmplaceHelper<Resolved<int>, Rejected<NoReject>>::Emplace(
      resolve.get(), Promise<int>(std::move(curried)));

  EXPECT_TRUE(resolve->IsResolvedWithPromise());
}

TEST(EmplaceHelper, NakedType) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);

  EmplaceHelper<Resolved<int>, Rejected<NoReject>>::Emplace(resolve.get(), 123);

  EXPECT_EQ(unique_any_cast<Resolved<int>>(resolve->value()).value, 123);
}

TEST(EmplaceHelper, ReferenceType) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);

  int a = 12345;

  EmplaceHelper<Resolved<int&>, Rejected<NoReject>>::Emplace<int&>(
      resolve.get(), a);

  EXPECT_EQ(unique_any_cast<Resolved<int&>>(resolve->value()).value, 12345);
}

TEST(EmplaceHelper, ResolvedInt) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);

  EmplaceHelper<Resolved<int>, Rejected<NoReject>>::Emplace(resolve.get(),
                                                            Resolved<int>(123));

  EXPECT_EQ(unique_any_cast<Resolved<int>>(resolve->value()).value, 123);
}

TEST(EmplaceHelper, RejectedString) {
  scoped_refptr<AbstractPromise> resolve = DoNothingPromiseBuilder(FROM_HERE);

  EmplaceHelper<Resolved<void>, Rejected<std::string>>::Emplace(
      resolve.get(), Rejected<std::string>("Whoops!"));

  EXPECT_EQ(unique_any_cast<Rejected<std::string>>(resolve->value()).value,
            "Whoops!");
}

TEST(RunHelper, CallbackVoidArgumentIntResult) {
  scoped_refptr<AbstractPromise> arg = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> result = DoNothingPromiseBuilder(FROM_HERE);

  RunHelper<OnceCallback<int()>, Resolved<void>, Resolved<int>,
            Rejected<std::string>>::Run(BindOnce([]() { return 123; }),
                                        arg.get(), result.get());

  EXPECT_EQ(unique_any_cast<Resolved<int>>(result->value()).value, 123);
}

TEST(RunHelper, CallbackVoidArgumentVoidResult) {
  scoped_refptr<AbstractPromise> arg = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> result = DoNothingPromiseBuilder(FROM_HERE);

  RunHelper<OnceCallback<void()>, Resolved<void>, Resolved<void>,
            Rejected<std::string>>::Run(BindOnce([]() {}), arg.get(),
                                        result.get());

  EXPECT_EQ(result->value().type(), TypeId::From<Resolved<void>>());
}

TEST(RunHelper, CallbackIntArgumentIntResult) {
  scoped_refptr<AbstractPromise> arg = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> result = DoNothingPromiseBuilder(FROM_HERE);
  arg->emplace(Resolved<int>(123));

  RunHelper<OnceCallback<int(int)>, Resolved<int>, Resolved<int>,
            Rejected<std::string>>::Run(BindOnce([](int value) {
                                          return value + 1;
                                        }),
                                        arg.get(), result.get());

  EXPECT_EQ(unique_any_cast<Resolved<int>>(result->value()).value, 124);
}

TEST(RunHelper, CallbackIntArgumentArgumentVoidResult) {
  scoped_refptr<AbstractPromise> arg = DoNothingPromiseBuilder(FROM_HERE);
  scoped_refptr<AbstractPromise> result = DoNothingPromiseBuilder(FROM_HERE);
  arg->emplace(Resolved<int>(123));

  int value;
  RunHelper<OnceCallback<void(int)>, Resolved<int>, Resolved<void>,
            Rejected<std::string>>::Run(BindLambdaForTesting([&](int arg) {
                                          value = arg;
                                        }),
                                        arg.get(), result.get());

  EXPECT_EQ(value, 123);
  EXPECT_EQ(result->value().type(), TypeId::From<Resolved<void>>());
}

}  // namespace internal
}  // namespace base
