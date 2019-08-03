// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_HELPERS_H_
#define BASE_TASK_PROMISE_HELPERS_H_

#include <tuple>
#include <type_traits>

#include "base/bind.h"
#include "base/callback.h"
#include "base/parameter_pack.h"
#include "base/task/promise/abstract_promise.h"
#include "base/task/promise/promise_result.h"

namespace base {
namespace internal {

template <typename T>
using ToNonVoidT = std::conditional_t<std::is_void<T>::value, Void, T>;

// PromiseCallbackTraits computes the resolve and reject types of a Promise
// from the return type of a resolve or reject callback.
//
// Usage example:
//   using Traits = PromiseCallbackTraits<int>;
//
//   Traits::
//       ResolveType is int
//       RejectType is NoReject
//       could_resolve is true
//       could_reject is false
template <typename T>
struct PromiseCallbackTraits {
  using ResolveType = T;
  using RejectType = NoReject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = false;
};

template <typename T>
struct PromiseCallbackTraits<Resolved<T>> {
  using ResolveType = T;
  using RejectType = NoReject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = false;
};

template <typename T>
struct PromiseCallbackTraits<Rejected<T>> {
  using ResolveType = NoResolve;
  using RejectType = T;
  static constexpr bool could_resolve = false;
  static constexpr bool could_reject = true;
};

template <typename Reject>
struct PromiseCallbackTraits<Promise<NoResolve, Reject>> {
  using ResolveType = NoResolve;
  using RejectType = Reject;
  static constexpr bool could_resolve = false;
  static constexpr bool could_reject = true;
};

template <typename Resolve>
struct PromiseCallbackTraits<Promise<Resolve, NoReject>> {
  using ResolveType = Resolve;
  using RejectType = NoReject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = false;
};

template <typename Resolve, typename Reject>
struct PromiseCallbackTraits<Promise<Resolve, Reject>> {
  using ResolveType = Resolve;
  using RejectType = Reject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = true;
};

template <typename Reject>
struct PromiseCallbackTraits<PromiseResult<NoResolve, Reject>> {
  using ResolveType = NoResolve;
  using RejectType = Reject;
  static constexpr bool could_resolve = false;
  static constexpr bool could_reject = true;
};

template <typename Resolve>
struct PromiseCallbackTraits<PromiseResult<Resolve, NoReject>> {
  using ResolveType = Resolve;
  using RejectType = NoReject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = false;
};

template <typename Resolve, typename Reject>
struct PromiseCallbackTraits<PromiseResult<Resolve, Reject>> {
  using ResolveType = Resolve;
  using RejectType = Reject;
  static constexpr bool could_resolve = true;
  static constexpr bool could_reject = true;
};

template <typename T>
struct IsScopedRefPtr {
  static constexpr bool value = false;
};

template <typename T>
struct IsScopedRefPtr<scoped_refptr<T>> {
  static constexpr bool value = true;
};

// UseMoveSemantics determines whether move semantics should be used to
// pass |T| as a function parameter.
//
// Usage example:
//
//   UseMoveSemantics<std::unique_ptr<int>>::value;  // is true
//   UseMoveSemantics<int>::value; // is false
//   UseMoveSemantics<scoped_refptr<Dummy>>::value; // is false
//
// Will give false positives for some copyable types, but that should be
// harmless.
template <typename T>
constexpr bool UseMove() {
  return !std::is_reference<T>::value && !std::is_pointer<T>::value &&
         !std::is_fundamental<std::decay_t<T>>::value &&
         !IsScopedRefPtr<T>::value;
}

template <typename T>
struct UseMoveSemantics : public std::integral_constant<bool, UseMove<T>()> {
  static_assert(!std::is_rvalue_reference<T>::value,
                "Promise<T&&> not supported");

  static constexpr AbstractPromise::Executor::ArgumentPassingType
      argument_passing_type =
          UseMove<T>()
              ? AbstractPromise::Executor::ArgumentPassingType::kMove
              : AbstractPromise::Executor::ArgumentPassingType::kNormal;
};

// A std::tuple is deemed to need move semantics if any of it's members need
// to be moved according to UseMove<>.
template <typename... Ts>
struct UseMoveSemantics<std::tuple<Ts...>>
    : public std::integral_constant<bool, any_of({UseMove<Ts>()...})> {
  static constexpr AbstractPromise::Executor::ArgumentPassingType
      argument_passing_type =
          any_of({UseMove<Ts>()...})
              ? AbstractPromise::Executor::ArgumentPassingType::kMove
              : AbstractPromise::Executor::ArgumentPassingType::kNormal;
};

// CallbackTraits extracts properties relevant to Promises from a callback.
//
// Usage example:
//
//   using Traits = CallbackTraits<
//       base::OnceCallback<PromiseResult<int, std::string>(float)>;
//
//   Traits::
//     ResolveType is int
//     RejectType is std::string
//     ArgType is float
//     ReturnType is PromiseResult<int, std::string>
//     SignatureType is PromiseResult<int, std::string>(float)
//     argument_passing_type is kNormal
template <typename T>
struct CallbackTraits;

template <typename T>
struct CallbackTraits<T()> {
  using ResolveType = typename internal::PromiseCallbackTraits<T>::ResolveType;
  using RejectType = typename internal::PromiseCallbackTraits<T>::RejectType;
  using ArgType = void;
  using ReturnType = T;
  using SignatureType = T();
  static constexpr AbstractPromise::Executor::ArgumentPassingType
      argument_passing_type =
          AbstractPromise::Executor::ArgumentPassingType::kNormal;
};

template <typename T, typename Arg>
struct CallbackTraits<T(Arg)> {
  using ResolveType = typename internal::PromiseCallbackTraits<T>::ResolveType;
  using RejectType = typename internal::PromiseCallbackTraits<T>::RejectType;
  using ArgType = Arg;
  using ReturnType = T;
  using SignatureType = T(Arg);
  static constexpr AbstractPromise::Executor::ArgumentPassingType
      argument_passing_type = UseMoveSemantics<Arg>::argument_passing_type;
};

template <typename T, typename... Args>
struct CallbackTraits<T(Args...)> {
  using ResolveType = typename internal::PromiseCallbackTraits<T>::ResolveType;
  using RejectType = typename internal::PromiseCallbackTraits<T>::RejectType;
  using ArgType =
      std::conditional_t<(sizeof...(Args) > 0), std::tuple<Args...>, void>;
  using ReturnType = T;
  using SignatureType = T(Args...);

  // If any arguments need move semantics, treat as if they all do.
  static constexpr AbstractPromise::Executor::ArgumentPassingType
      argument_passing_type =
          any_of({UseMoveSemantics<Args>::value...})
              ? AbstractPromise::Executor::ArgumentPassingType::kMove
              : AbstractPromise::Executor::ArgumentPassingType::kNormal;
};

// Adaptors for OnceCallback and RepeatingCallback
template <typename T, typename... Args>
struct CallbackTraits<OnceCallback<T(Args...)>>
    : public CallbackTraits<T(Args...)> {};

template <typename T, typename... Args>
struct CallbackTraits<RepeatingCallback<T(Args...)>>
    : public CallbackTraits<T(Args...)> {};

// Helper for combining the resolve types of two promises.
template <typename A, typename B>
struct ResolveCombinerHelper {
  using Type = A;
  static constexpr bool valid = false;
};

template <typename A>
struct ResolveCombinerHelper<A, A> {
  using Type = A;
  static constexpr bool valid = true;
};

template <typename B>
struct ResolveCombinerHelper<NoResolve, B> {
  using Type = B;
  static constexpr bool valid = true;
};

template <typename A>
struct ResolveCombinerHelper<A, NoResolve> {
  using Type = A;
  static constexpr bool valid = true;
};

template <>
struct ResolveCombinerHelper<NoResolve, NoResolve> {
  using Type = NoResolve;
  static constexpr bool valid = true;
};

// Helper for combining the reject types of two promises.
template <typename A, typename B>
struct RejectCombinerHelper {
  using Type = A;
  static constexpr bool valid = false;
};

template <typename A>
struct RejectCombinerHelper<A, A> {
  using Type = A;
  static constexpr bool valid = true;
};

template <typename B>
struct RejectCombinerHelper<NoReject, B> {
  using Type = B;
  static constexpr bool valid = true;
};

template <typename A>
struct RejectCombinerHelper<A, NoReject> {
  using Type = A;
  static constexpr bool valid = true;
};

template <>
struct RejectCombinerHelper<NoReject, NoReject> {
  using Type = NoReject;
  static constexpr bool valid = true;
};

// Helper that computes and validates the return type for combining promises.
// Essentially the promise types have to match unless there's NoResolve or
// or NoReject in which case they can be combined.
template <typename ThenReturnResolveT,
          typename ThenReturnRejectT,
          typename CatchReturnResolveT,
          typename CatchReturnRejectT>
struct PromiseCombiner {
  using ResolveHelper =
      ResolveCombinerHelper<ThenReturnResolveT, CatchReturnResolveT>;
  using RejectHelper =
      RejectCombinerHelper<ThenReturnRejectT, CatchReturnRejectT>;
  using ResolveType = typename ResolveHelper::Type;
  using RejectType = typename RejectHelper::Type;
  static constexpr bool valid = ResolveHelper::valid && RejectHelper::valid;
};

template <typename RejectStorage>
struct EmplaceInnerHelper {
  template <typename Resolve, typename Reject>
  static void Emplace(AbstractPromise* promise,
                      PromiseResult<Resolve, Reject> result) {
    promise->emplace(std::move(result.value()));
  }
};

// TODO(alexclarke): Specialize |EmplaceInnerHelper| where RejectStorage is
// base::Variant to support Promises::All.

template <typename ResolveStorage, typename RejectStorage>
struct EmplaceHelper {
  template <typename Resolve, typename Reject>
  static void Emplace(AbstractPromise* promise,
                      PromiseResult<Resolve, Reject>&& result) {
    static_assert(std::is_same<typename ResolveStorage::Type, Resolve>::value ||
                      std::is_same<NoResolve, Resolve>::value,
                  "Resolve should match ResolveStorage");
    static_assert(std::is_same<typename RejectStorage::Type, Reject>::value ||
                      std::is_same<NoReject, Reject>::value,
                  "Reject should match RejectStorage");
    EmplaceInnerHelper<RejectStorage>::Emplace(promise, std::move(result));
  }

  template <typename Resolve, typename Reject>
  static void Emplace(AbstractPromise* promise,
                      Promise<Resolve, Reject>&& result) {
    static_assert(std::is_same<typename ResolveStorage::Type, Resolve>::value ||
                      std::is_same<NoResolve, Resolve>::value,
                  "Resolve should match ResolveStorage");
    static_assert(std::is_same<typename RejectStorage::Type, Reject>::value ||
                      std::is_same<NoReject, Reject>::value,
                  "Reject should match RejectStorage");
    promise->emplace(std::move(result.abstract_promise_));
  }

  template <typename Result>
  static void Emplace(AbstractPromise* promise, Result&& result) {
    static_assert(std::is_same<typename ResolveStorage::Type, Result>::value,
                  "Result should match ResolveStorage");
    promise->emplace(in_place_type_t<Resolved<Result>>(),
                     std::forward<Result>(result));
  }

  template <typename Resolve>
  static void Emplace(AbstractPromise* promise, Resolved<Resolve>&& resolved) {
    static_assert(std::is_same<typename ResolveStorage::Type, Resolve>::value,
                  "Resolve should match ResolveStorage");
    promise->emplace(std::move(resolved));
  }

  template <typename Reject>
  static void Emplace(AbstractPromise* promise, Rejected<Reject>&& rejected) {
    static_assert(std::is_same<typename RejectStorage::Type, Reject>::value,
                  "Reject should match RejectStorage");
    promise->emplace(std::move(rejected));
  }
};

// Helper that decides whether or not to std::move arguments for a callback
// based on the type the callback specifies (i.e. we don't need to move if the
// callback requests a reference).
template <typename CbArg, typename ArgStorageType>
class ArgMoveSemanticsHelper {
 public:
  static CbArg Get(AbstractPromise* arg) {
    return GetImpl(arg, UseMoveSemantics<CbArg>());
  }

 private:
  static CbArg GetImpl(AbstractPromise* arg, std::true_type should_move) {
    return std::move(
        unique_any_cast<ArgStorageType>(&arg->TakeValue().value())->value);
  }

  static CbArg GetImpl(AbstractPromise* arg, std::false_type should_move) {
    return unique_any_cast<ArgStorageType>(&arg->value())->value;
  }
};

// Helper for running a promise callback and storing the result if any.
//
// Callback = signature of the callback to execute,
// ArgStorageType = type of the callback parameter (or void if none)
// ResolveStorage = type to use for resolve, usually Resolved<T>.
// RejectStorage = type to use for reject, usually Rejected<T>.
// TODO(alexclarke): Add support for Rejected<Variant<...>>.
template <typename Callback,
          typename ArgStorageType,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper;

// Run helper for callbacks with a single argument.
template <typename CbResult,
          typename CbArg,
          typename ArgStorageType,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper<OnceCallback<CbResult(CbArg)>,
                 ArgStorageType,
                 ResolveStorage,
                 RejectStorage> {
  using Callback = OnceCallback<CbResult(CbArg)>;

  static void Run(Callback executor,
                  AbstractPromise* arg,
                  AbstractPromise* result) {
    EmplaceHelper<ResolveStorage, RejectStorage>::Emplace(
        result, std::move(executor).Run(
                    ArgMoveSemanticsHelper<CbArg, ArgStorageType>::Get(arg)));
  }
};

// Run helper for callbacks with a single argument and void return value.
template <typename CbArg,
          typename ArgStorageType,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper<OnceCallback<void(CbArg)>,
                 ArgStorageType,
                 ResolveStorage,
                 RejectStorage> {
  using Callback = OnceCallback<void(CbArg)>;

  static void Run(Callback executor,
                  AbstractPromise* arg,
                  AbstractPromise* result) {
    static_assert(std::is_void<typename ResolveStorage::Type>::value, "");
    std::move(executor).Run(
        ArgMoveSemanticsHelper<CbArg, ArgStorageType>::Get(arg));
    result->emplace(Resolved<void>());
  }
};

// Run helper for callbacks with no arguments.
template <typename CbResult,
          typename ArgStorageType,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper<OnceCallback<CbResult()>,
                 ArgStorageType,
                 ResolveStorage,
                 RejectStorage> {
  using Callback = OnceCallback<CbResult()>;

  static void Run(Callback executor,
                  AbstractPromise* arg,
                  AbstractPromise* result) {
    EmplaceHelper<ResolveStorage, RejectStorage>::Emplace(
        result, std::move(executor).Run());
  }
};

// Run helper for callbacks with no arguments and void return type.
template <typename ArgStorageType,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper<OnceCallback<void()>,
                 ArgStorageType,
                 ResolveStorage,
                 RejectStorage> {
  static void Run(OnceCallback<void()> executor,
                  AbstractPromise* arg,
                  AbstractPromise* result) {
    static_assert(std::is_void<typename ResolveStorage::Type>::value, "");
    std::move(executor).Run();
    result->emplace(Resolved<void>());
  }
};

template <typename T>
struct UnwrapCallback;

template <typename R, typename... Args>
struct UnwrapCallback<R(Args...)> {
  using ArgsTuple = std::tuple<Args...>;
};

// Helper for getting callback arguments from a tuple, which works out if move
// semantics are needed.
template <typename Callback, typename Tuple, size_t Index>
struct TupleArgMoveSemanticsHelper {
  using CallbackArgsTuple =
      typename UnwrapCallback<typename Callback::RunType>::ArgsTuple;
  using CbArg = std::tuple_element_t<Index, CallbackArgsTuple>;

  static CbArg Get(Tuple& tuple) {
    return GetImpl(tuple, UseMoveSemantics<CbArg>());
  }

 private:
  static CbArg GetImpl(Tuple& tuple, std::true_type should_move) {
    return std::move(std::get<Index>(tuple));
  }

  static CbArg GetImpl(Tuple& tuple, std::false_type should_move) {
    return std::get<Index>(tuple);
  }
};

// Run helper for running a callbacks with the arguments unpacked from a tuple.
template <typename CbResult,
          typename... CbArgs,
          typename ResolveStorage,
          typename RejectStorage>
struct RunHelper<OnceCallback<CbResult(CbArgs...)>,
                 Resolved<std::tuple<CbArgs...>>,
                 ResolveStorage,
                 RejectStorage> {
  using Callback = OnceCallback<CbResult(CbArgs...)>;
  using StorageType = Resolved<std::tuple<CbArgs...>>;
  using IndexSequence = std::index_sequence_for<CbArgs...>;

  static void Run(Callback executor,
                  AbstractPromise* arg,
                  AbstractPromise* result) {
    AbstractPromise::ValueHandle value = arg->TakeValue();
    std::tuple<CbArgs...>& tuple =
        unique_any_cast<StorageType>(&value.value())->value;
    RunInternal(std::move(executor), tuple, result,
                std::integral_constant<bool, std::is_void<CbResult>::value>(),
                IndexSequence{});
  }

 private:
  template <typename Callback, size_t... Indices>
  static void RunInternal(Callback executor,
                          std::tuple<CbArgs...>& tuple,
                          AbstractPromise* result,
                          std::false_type void_result,
                          std::index_sequence<Indices...>) {
    EmplaceHelper<ResolveStorage, RejectStorage>::Emplace(
        std::move(executor).Run(
            TupleArgMoveSemanticsHelper<Callback, std::tuple<CbArgs...>,
                                        Indices>::Get(tuple)...));
  }

  template <typename Callback, size_t... Indices>
  static void RunInternal(Callback executor,
                          std::tuple<CbArgs...>& tuple,
                          AbstractPromise* result,
                          std::true_type void_result,
                          std::index_sequence<Indices...>) {
    std::move(executor).Run(
        TupleArgMoveSemanticsHelper<Callback, std::tuple<CbArgs...>,
                                    Indices>::Get(tuple)...);
    result->emplace(Resolved<void>());
  }
};

// Used by ManualPromiseResolver<> to generate callbacks.
template <typename T, typename... Args>
class PromiseCallbackHelper {
 public:
  using Callback = base::OnceCallback<void(Args&&...)>;
  using RepeatingCallback = base::RepeatingCallback<void(Args&&...)>;

  static Callback GetResolveCallback(scoped_refptr<AbstractPromise>& promise) {
    return base::BindOnce(
        [](scoped_refptr<AbstractPromise> promise, Args&&... args) {
          promise->emplace(Resolved<T>{std::forward<Args>(args)...});
          promise->OnResolved();
        },
        promise);
  }

  static RepeatingCallback GetRepeatingResolveCallback(
      scoped_refptr<AbstractPromise>& promise) {
    return base::BindRepeating(
        [](scoped_refptr<AbstractPromise> promise, Args&&... args) {
          promise->emplace(Resolved<T>{std::forward<Args>(args)...});
          promise->OnResolved();
        },
        promise);
  }

  static Callback GetRejectCallback(scoped_refptr<AbstractPromise>& promise) {
    return base::BindOnce(
        [](scoped_refptr<AbstractPromise> promise, Args&&... args) {
          promise->emplace(Rejected<T>{std::forward<Args>(args)...});
          promise->OnRejected();
        },
        promise);
  }

  static RepeatingCallback GetRepeatingRejectCallback(
      scoped_refptr<AbstractPromise>& promise) {
    return base::BindRepeating(
        [](scoped_refptr<AbstractPromise> promise, Args&&... args) {
          promise->emplace(Rejected<T>{std::forward<Args>(args)...});
          promise->OnRejected();
        },
        promise);
  }
};

// Validates that the argument type |CallbackArgType| of a resolve or
// reject callback is compatible with the resolve or reject type
// |PromiseType| of this Promise.
template <typename PromiseType, typename CallbackArgType>
struct IsValidPromiseArg {
  static constexpr bool value =
      std::is_same<PromiseType, std::decay_t<CallbackArgType>>::value;
};

template <typename PromiseType, typename CallbackArgType>
struct IsValidPromiseArg<PromiseType&, CallbackArgType> {
  static constexpr bool value =
      std::is_same<PromiseType&, CallbackArgType>::value;
};

// This template helps assign the reject value from a prerequisite into the
// rejection storage type.
template <typename RejectT>
struct AllPromiseRejectHelper {
  static void Reject(AbstractPromise* result,
                     const scoped_refptr<AbstractPromise>& prerequisite) {
    result->emplace(scoped_refptr<AbstractPromise>(prerequisite));
  }
};

// TODO(alexclarke): Specalize AllPromiseRejectHelper for variants.

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_HELPERS_H_
