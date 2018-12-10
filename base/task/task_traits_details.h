// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_TRAITS_DETAILS_H_
#define BASE_TASK_TASK_TRAITS_DETAILS_H_

#include <tuple>
#include <type_traits>
#include <utility>

namespace base {
namespace trait_helpers {

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
//     ValueType:
//         The return type of GetValueFromArgListImpl().
//
//     ArgType:
//         The type of the argument from which GetValueFromArgListImpl() derives
//         its return value.
//
//     ValueType GetValueFromArg(ArgType):
//         Converts an argument of type ArgType into a value returned by
//         GetValueFromArgListImpl().
//
// |getter| may provide:
//     ValueType GetDefaultValue():
//         Returns the value returned by GetValueFromArgListImpl() if none of
//         its arguments is of type ArgType. If this method is not provided,
//         compilation will fail when no argument of type ArgType is provided.
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

template <typename ArgType>
struct RequiredEnumArgGetter {
  using ValueType = ArgType;
  constexpr ValueType GetValueFromArg(ArgType arg) const { return arg; }
};

// Tests whether a given trait type is valid or invalid by testing whether it is
// convertible to the provided ValidTraits type. To use, define a ValidTraits
// type like this:
//
// struct ValidTraits {
//   ValidTraits(MyTrait) {}
//   ...
// };
template <class ValidTraits>
struct ValidTraitTester {
  template <class TraitType>
  struct IsValid : std::is_convertible<TraitType, ValidTraits> {};

  template <class TraitType>
  struct IsInvalid
      : std::conditional_t<std::is_convertible<TraitType, ValidTraits>::value,
                           std::false_type,
                           std::true_type> {};
};

// Tests if a given trait type is valid according to the provided ValidTraits.
template <class ValidTraits, class TraitType>
struct IsValidTrait
    : ValidTraitTester<ValidTraits>::template IsValid<TraitType> {};

// Tests whether multiple given traits types are all valid according to the
// provided ValidTraits.
template <class ValidTraits, class...>
struct AreValidTraits : std::true_type {};

template <class ValidTraits, class NextType, class... Rest>
struct AreValidTraits<ValidTraits, NextType, Rest...>
    : std::conditional<IsValidTrait<ValidTraits, NextType>::value,
                       AreValidTraits<ValidTraits, Rest...>,
                       std::false_type>::type {};

// Helper struct that recursively builds up an index_sequence containing all
// those indexes of elements in Args for which the |Predicate<Arg>::value| is
// true.
template <template <class> class Predicate,
          std::size_t CurrentIndex,
          class Output,
          class... Args>
struct SelectIndicesHelper;

template <template <class> class Predicate,
          std::size_t CurrentIndex,
          std::size_t... Indices,
          class First,
          class... Rest>
struct SelectIndicesHelper<Predicate,
                           CurrentIndex,
                           std::index_sequence<Indices...>,
                           First,
                           Rest...>
    : std::conditional_t<
          Predicate<First>::value,
          // Push the index into the sequence and recurse.
          SelectIndicesHelper<Predicate,
                              CurrentIndex + 1,
                              std::index_sequence<Indices..., CurrentIndex>,
                              Rest...>,
          // Skip the index and recurse.
          SelectIndicesHelper<Predicate,
                              CurrentIndex + 1,
                              std::index_sequence<Indices...>,
                              Rest...>> {};

template <template <class> class Predicate,
          std::size_t CurrentIndex,
          class Sequence>
struct SelectIndicesHelper<Predicate, CurrentIndex, Sequence> {
  using type = Sequence;
};

// Selects the indices of elements in the |Args| list for which
// |Predicate<Arg>::value| is |true|.
template <template <class> class Predicate, class... Args>
using SelectIndices =
    typename SelectIndicesHelper<Predicate, 0, std::index_sequence<>, Args...>::
        type;

}  // namespace trait_helpers
}  // namespace base

#endif  // BASE_TASK_TASK_TRAITS_DETAILS_H_
