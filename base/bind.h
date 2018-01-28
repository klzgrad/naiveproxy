// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_BIND_H_
#define BASE_BIND_H_

#include <utility>

#include "base/bind_internal.h"

// -----------------------------------------------------------------------------
// Usage documentation
// -----------------------------------------------------------------------------
//
// See //docs/callback.md for documentation.
//
//
// -----------------------------------------------------------------------------
// Implementation notes
// -----------------------------------------------------------------------------
//
// If you're reading the implementation, before proceeding further, you should
// read the top comment of base/bind_internal.h for a definition of common
// terms and concepts.

namespace base {

namespace internal {

// IsOnceCallback<T> is a std::true_type if |T| is a OnceCallback.
template <typename T>
struct IsOnceCallback : std::false_type {};

template <typename Signature>
struct IsOnceCallback<OnceCallback<Signature>> : std::true_type {};

// Helper to assert that parameter |i| of type |Arg| can be bound, which means:
// - |Arg| can be retained internally as |Storage|.
// - |Arg| can be forwarded as |Unwrapped| to |Param|.
template <size_t i,
          typename Arg,
          typename Storage,
          typename Unwrapped,
          typename Param>
struct AssertConstructible {
 private:
  static constexpr bool param_is_forwardable =
      std::is_constructible<Param, Unwrapped>::value;
  // Unlike the check for binding into storage below, the check for
  // forwardability drops the const qualifier for repeating callbacks. This is
  // to try to catch instances where std::move()--which forwards as a const
  // reference with repeating callbacks--is used instead of base::Passed().
  static_assert(
      param_is_forwardable ||
          !std::is_constructible<Param, std::decay_t<Unwrapped>&&>::value,
      "Bound argument |i| is move-only but will be forwarded by copy. "
      "Ensure |Arg| is bound using base::Passed(), not std::move().");
  static_assert(
      param_is_forwardable,
      "Bound argument |i| of type |Arg| cannot be forwarded as "
      "|Unwrapped| to the bound functor, which declares it as |Param|.");

  static constexpr bool arg_is_storable =
      std::is_constructible<Storage, Arg>::value;
  static_assert(arg_is_storable ||
                    !std::is_constructible<Storage, std::decay_t<Arg>&&>::value,
                "Bound argument |i| is move-only but will be bound by copy. "
                "Ensure |Arg| is mutable and bound using std::move().");
  static_assert(arg_is_storable,
                "Bound argument |i| of type |Arg| cannot be converted and "
                "bound as |Storage|.");
};

// Takes three same-length TypeLists, and applies AssertConstructible for each
// triples.
template <typename Index,
          typename Args,
          typename UnwrappedTypeList,
          typename ParamsList>
struct AssertBindArgsValidity;

template <size_t... Ns,
          typename... Args,
          typename... Unwrapped,
          typename... Params>
struct AssertBindArgsValidity<std::index_sequence<Ns...>,
                              TypeList<Args...>,
                              TypeList<Unwrapped...>,
                              TypeList<Params...>>
    : AssertConstructible<Ns, Args, std::decay_t<Args>, Unwrapped, Params>... {
  static constexpr bool ok = true;
};

// The implementation of TransformToUnwrappedType below.
template <bool is_once, typename T>
struct TransformToUnwrappedTypeImpl;

template <typename T>
struct TransformToUnwrappedTypeImpl<true, T> {
  using StoredType = std::decay_t<T>;
  using ForwardType = StoredType&&;
  using Unwrapped = decltype(Unwrap(std::declval<ForwardType>()));
};

template <typename T>
struct TransformToUnwrappedTypeImpl<false, T> {
  using StoredType = std::decay_t<T>;
  using ForwardType = const StoredType&;
  using Unwrapped = decltype(Unwrap(std::declval<ForwardType>()));
};

// Transform |T| into `Unwrapped` type, which is passed to the target function.
// Example:
//   In is_once == true case,
//     `int&&` -> `int&&`,
//     `const int&` -> `int&&`,
//     `OwnedWrapper<int>&` -> `int*&&`.
//   In is_once == false case,
//     `int&&` -> `const int&`,
//     `const int&` -> `const int&`,
//     `OwnedWrapper<int>&` -> `int* const &`.
template <bool is_once, typename T>
using TransformToUnwrappedType =
    typename TransformToUnwrappedTypeImpl<is_once, T>::Unwrapped;

// Transforms |Args| into `Unwrapped` types, and packs them into a TypeList.
// If |is_method| is true, tries to dereference the first argument to support
// smart pointers.
template <bool is_once, bool is_method, typename... Args>
struct MakeUnwrappedTypeListImpl {
  using Type = TypeList<TransformToUnwrappedType<is_once, Args>...>;
};

// Performs special handling for this pointers.
// Example:
//   int* -> int*,
//   std::unique_ptr<int> -> int*.
template <bool is_once, typename Receiver, typename... Args>
struct MakeUnwrappedTypeListImpl<is_once, true, Receiver, Args...> {
  using UnwrappedReceiver = TransformToUnwrappedType<is_once, Receiver>;
  using Type = TypeList<decltype(&*std::declval<UnwrappedReceiver>()),
                        TransformToUnwrappedType<is_once, Args>...>;
};

template <bool is_once, bool is_method, typename... Args>
using MakeUnwrappedTypeList =
    typename MakeUnwrappedTypeListImpl<is_once, is_method, Args...>::Type;

}  // namespace internal

// Bind as OnceCallback.
template <typename Functor, typename... Args>
inline OnceCallback<MakeUnboundRunType<Functor, Args...>>
BindOnce(Functor&& functor, Args&&... args) {
  static_assert(!internal::IsOnceCallback<std::decay_t<Functor>>() ||
                    (std::is_rvalue_reference<Functor&&>() &&
                     !std::is_const<std::remove_reference_t<Functor>>()),
                "BindOnce requires non-const rvalue for OnceCallback binding."
                " I.e.: base::BindOnce(std::move(callback)).");

  // This block checks if each |args| matches to the corresponding params of the
  // target function. This check does not affect the behavior of Bind, but its
  // error message should be more readable.
  using Helper = internal::BindTypeHelper<Functor, Args...>;
  using FunctorTraits = typename Helper::FunctorTraits;
  using BoundArgsList = typename Helper::BoundArgsList;
  using UnwrappedArgsList =
      internal::MakeUnwrappedTypeList<true, FunctorTraits::is_method,
                                      Args&&...>;
  using BoundParamsList = typename Helper::BoundParamsList;
  static_assert(internal::AssertBindArgsValidity<
                    std::make_index_sequence<Helper::num_bounds>, BoundArgsList,
                    UnwrappedArgsList, BoundParamsList>::ok,
                "The bound args need to be convertible to the target params.");

  using BindState = internal::MakeBindStateType<Functor, Args...>;
  using UnboundRunType = MakeUnboundRunType<Functor, Args...>;
  using Invoker = internal::Invoker<BindState, UnboundRunType>;
  using CallbackType = OnceCallback<UnboundRunType>;

  // Store the invoke func into PolymorphicInvoke before casting it to
  // InvokeFuncStorage, so that we can ensure its type matches to
  // PolymorphicInvoke, to which CallbackType will cast back.
  using PolymorphicInvoke = typename CallbackType::PolymorphicInvoke;
  PolymorphicInvoke invoke_func = &Invoker::RunOnce;

  using InvokeFuncStorage = internal::BindStateBase::InvokeFuncStorage;
  return CallbackType(new BindState(
      reinterpret_cast<InvokeFuncStorage>(invoke_func),
      std::forward<Functor>(functor),
      std::forward<Args>(args)...));
}

// Bind as RepeatingCallback.
template <typename Functor, typename... Args>
inline RepeatingCallback<MakeUnboundRunType<Functor, Args...>>
BindRepeating(Functor&& functor, Args&&... args) {
  static_assert(
      !internal::IsOnceCallback<std::decay_t<Functor>>(),
      "BindRepeating cannot bind OnceCallback. Use BindOnce with std::move().");

  // This block checks if each |args| matches to the corresponding params of the
  // target function. This check does not affect the behavior of Bind, but its
  // error message should be more readable.
  using Helper = internal::BindTypeHelper<Functor, Args...>;
  using FunctorTraits = typename Helper::FunctorTraits;
  using BoundArgsList = typename Helper::BoundArgsList;
  using UnwrappedArgsList =
      internal::MakeUnwrappedTypeList<false, FunctorTraits::is_method,
                                      Args&&...>;
  using BoundParamsList = typename Helper::BoundParamsList;
  static_assert(internal::AssertBindArgsValidity<
                    std::make_index_sequence<Helper::num_bounds>, BoundArgsList,
                    UnwrappedArgsList, BoundParamsList>::ok,
                "The bound args need to be convertible to the target params.");

  using BindState = internal::MakeBindStateType<Functor, Args...>;
  using UnboundRunType = MakeUnboundRunType<Functor, Args...>;
  using Invoker = internal::Invoker<BindState, UnboundRunType>;
  using CallbackType = RepeatingCallback<UnboundRunType>;

  // Store the invoke func into PolymorphicInvoke before casting it to
  // InvokeFuncStorage, so that we can ensure its type matches to
  // PolymorphicInvoke, to which CallbackType will cast back.
  using PolymorphicInvoke = typename CallbackType::PolymorphicInvoke;
  PolymorphicInvoke invoke_func = &Invoker::Run;

  using InvokeFuncStorage = internal::BindStateBase::InvokeFuncStorage;
  return CallbackType(new BindState(
      reinterpret_cast<InvokeFuncStorage>(invoke_func),
      std::forward<Functor>(functor),
      std::forward<Args>(args)...));
}

// Unannotated Bind.
// TODO(tzik): Deprecate this and migrate to OnceCallback and
// RepeatingCallback, once they get ready.
template <typename Functor, typename... Args>
inline Callback<MakeUnboundRunType<Functor, Args...>>
Bind(Functor&& functor, Args&&... args) {
  return BindRepeating(std::forward<Functor>(functor),
                       std::forward<Args>(args)...);
}

// Special cases for binding to a base::Callback without extra bound arguments.
template <typename Signature>
OnceCallback<Signature> BindOnce(OnceCallback<Signature> closure) {
  return closure;
}

template <typename Signature>
RepeatingCallback<Signature> BindRepeating(
    RepeatingCallback<Signature> closure) {
  return closure;
}

template <typename Signature>
Callback<Signature> Bind(Callback<Signature> closure) {
  return closure;
}

}  // namespace base

#endif  // BASE_BIND_H_
