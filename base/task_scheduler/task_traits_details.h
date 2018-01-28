// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_TASK_TRAITS_DETAILS_H_
#define BASE_TASK_SCHEDULER_TASK_TRAITS_DETAILS_H_

#include <type_traits>
#include <utility>

namespace base {
namespace internal {

// HasArgOfType<CheckedType, ArgTypes...>::value is true iff a type in ArgTypes
// matches CheckedType.
template <class...>
struct HasArgOfType : std::false_type {};
template <class CheckedType, class FirstArgType, class... ArgTypes>
struct HasArgOfType<CheckedType, FirstArgType, ArgTypes...>
    : std::conditional<std::is_same<CheckedType, FirstArgType>::value,
                       std::true_type,
                       HasArgOfType<CheckedType, ArgTypes...>>::type {};

// When the following call is made:
//    GetValueFromArgListImpl(CallFirstTag(), GetterType(), args...);
// If |args| is empty, the compiler selects the first overload. This overload
// returns getter.GetDefaultValue(). If |args| is not empty, the compiler
// prefers using the second overload because the type of the first argument
// matches exactly. This overload returns getter.GetValueFromArg(first_arg),
// where |first_arg| is the first element in |args|. If
// getter.GetValueFromArg(first_arg) isn't defined, the compiler uses the third
// overload instead. This overload discards the first argument in |args| and
// makes a recursive call to GetValueFromArgListImpl() with CallFirstTag() as
// first argument.

// Tag dispatching.
struct CallSecondTag {};
struct CallFirstTag : CallSecondTag {};

// Overload 1: Default value.
template <class GetterType>
constexpr typename GetterType::ValueType GetValueFromArgListImpl(
    CallFirstTag,
    GetterType getter) {
  return getter.GetDefaultValue();
}

// Overload 2: Get value from first argument. Check that no argument in |args|
// has the same type as |first_arg|.
template <class GetterType,
          class FirstArgType,
          class... ArgTypes,
          class TestGetValueFromArgDefined =
              decltype(std::declval<GetterType>().GetValueFromArg(
                  std::declval<FirstArgType>()))>
constexpr typename GetterType::ValueType GetValueFromArgListImpl(
    CallFirstTag,
    GetterType getter,
    const FirstArgType& first_arg,
    const ArgTypes&... args) {
  static_assert(!HasArgOfType<FirstArgType, ArgTypes...>::value,
                "Multiple arguments of the same type were provided to the "
                "constructor of TaskTraits.");
  return getter.GetValueFromArg(first_arg);
}

// Overload 3: Discard first argument.
template <class GetterType, class FirstArgType, class... ArgTypes>
constexpr typename GetterType::ValueType GetValueFromArgListImpl(
    CallSecondTag,
    GetterType getter,
    const FirstArgType&,
    const ArgTypes&... args) {
  return GetValueFromArgListImpl(CallFirstTag(), getter, args...);
}

// If there is an argument |arg_of_type| of type Getter::ArgType in |args|,
// returns getter.GetValueFromArg(arg_of_type). If there are more than one
// argument of type Getter::ArgType in |args|, generates a compile-time error.
// Otherwise, returns getter.GetDefaultValue().
//
// |getter| must provide:
//
// ValueType:
//     The return type of GetValueFromArgListImpl().
//
// ArgType:
//     The type of the argument from which GetValueFromArgListImpl() derives its
//     return value.
//
// ValueType GetValueFromArg(ArgType):
//     Converts an argument of type ArgType into a value returned by
//     GetValueFromArgListImpl().
//
// ValueType GetDefaultValue():
//     Returns the value returned by GetValueFromArgListImpl() if none of its
//     arguments is of type ArgType.
template <class GetterType, class... ArgTypes>
constexpr typename GetterType::ValueType GetValueFromArgList(
    GetterType getter,
    const ArgTypes&... args) {
  return GetValueFromArgListImpl(CallFirstTag(), getter, args...);
}

template <typename ArgType>
struct BooleanArgGetter {
  using ValueType = bool;
  constexpr ValueType GetValueFromArg(ArgType) const { return true; }
  constexpr ValueType GetDefaultValue() const { return false; }
};

template <typename ArgType, ArgType DefaultValue>
struct EnumArgGetter {
  using ValueType = ArgType;
  constexpr ValueType GetValueFromArg(ArgType arg) const { return arg; }
  constexpr ValueType GetDefaultValue() const { return DefaultValue; }
};

// Allows instantiation of multiple types in one statement. Used to prevent
// instantiation of the constructor of TaskTraits with inappropriate argument
// types.
template <class...>
struct InitTypes {};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_TASK_TRAITS_DETAILS_H_
