// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_UNIQUE_ANY_H_
#define BASE_CONTAINERS_UNIQUE_ANY_H_

#include <utility>

#include "base/containers/any_internal.h"

// base::unique_any is similar to std::any except it:
// * can hold move constructible types such as std::unique_ptr<>
// * doesn't have a copy constructor or copy assignment operator
// * doesn't require exceptions or RTTI.
//
// It can be thought of as a better void * E.g.
// base::unique_any a = 123;
// EXPECT_EQ(123, base::unique_any_cast<int>(a));
//
// a = std::string("123");
// EXPECT_EQ("123", base::unique_any_cast<std::string>(a));
//
// a = make_unique_ptr("123");
// EXPECT_EQ("123", *base::unique_any_cast<std::unique_ptr<int>>(a));
//
// base::unique_any b(std::move(a));
// EXPECT_EQ("123", *base::unique_any_cast<std::unique_ptr<int>>(b));
//
// Note an incorrect base::unique_any_cast will lead to a CHECK.

namespace base {

class unique_any;

namespace internal {

template <typename T>
struct is_unique_any {
  static constexpr bool value = false;
};

template <>
struct is_unique_any<unique_any> {
  static constexpr bool value = true;
};

}  // namespace internal

// Constructs a base::unique_any of type |T| with the given arguments.
template <typename T, typename... Args>
unique_any make_unique_any(Args&&... args);

// Overload of |base::make_unique_any()| for constructing a base::unique_any
// type from an initializer list. E.g.
// base::unique_any a = base::make_any<std::vector<int>>({1, 2, 3, 4});
template <typename T, typename U, typename... Args>
unique_any make_unique_any(std::initializer_list<U> il, Args&&... args);

// Statically casts the value of a const base::unique_any to the given type.
// Unlike std::any_cast which throws an exception, this function will CHECK if
// the stored value type |any| does not match the cast. E.g.
//
//     base::unique_any a = 123;
//     int i = base::unique_any_cast<int>(a);  // i = 123
//
// NB unique_any_cast() can also be used to get a reference to the internal
// storage iff a reference type is passed as its ValueType. E.g.
//
//     base::unique_any my_any = std::vector<int>();
//     base::unique_any_cast<std::vector<int>&>(my_any).push_back(42);
//
template <typename ValueType>
ValueType unique_any_cast(const unique_any& any);

template <typename ValueType>
ValueType unique_any_cast(unique_any& any);

template <typename ValueType>
ValueType unique_any_cast(unique_any&& any);

// Overload of unique_any_cast() to statically cast the value of a const pointer
// base::unique_any to the given pointer type, or nullptr if the stored value
// type of |any| does not match the cast.
template <typename ValueType>
const ValueType* unique_any_cast(const unique_any* any) noexcept;

template <typename ValueType>
ValueType* unique_any_cast(unique_any* any) noexcept;

class BASE_EXPORT unique_any {
 private:
  template <typename T, bool UseInlineStorage>
  using ConstructHelper =
      internal::AnyInternal::ConstructHelper<T, UseInlineStorage>;

  template <typename T>
  using InlineStorageHelper = internal::AnyInternal::InlineStorageHelper<T>;

  template <typename T>
  using TypeOpsHelper = internal::AnyInternal::TypeOpsHelper<T>;

 public:
  // Constructs an empty base::unique_any.
  constexpr unique_any() noexcept {}

  // Constructs a base::unique_any with the value contained by |other| moved
  // into it.
  constexpr unique_any(unique_any&& other) noexcept
      : internal_(other.internal_.type_ops_) {
    if (internal_.type_ops_)
      internal_.type_ops_->move_fn_ptr(&other.internal_, &internal_);
  }

  template <typename T>
  struct is_move_or_copy_constructible {
    static constexpr bool value = std::is_copy_constructible<T>::value ||
                                  std::is_move_constructible<T>::value;
  };

  // Constructs a base::unique_any containing |value| as long as |T| isn't
  // base::unique_any nor base::in_place_type_t<> and |T| is move or copy
  // constructible. E.g. base::unique_any a(123);
  template <
      typename T,
      typename VT = std::decay_t<T>,
      std::enable_if_t<!internal::is_unique_any<VT>::value>* = nullptr,
      std::enable_if_t<!is_in_place_type_t<VT>::value>* = nullptr,
      std::enable_if_t<is_move_or_copy_constructible<VT>::value>* = nullptr>
  unique_any(T&& value) noexcept : internal_(&TypeOpsHelper<VT>::type_ops) {
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, std::forward<T>(value));
  }

  // Constructs a base::unique_any containing an object of type T which is
  // initialized by std::forward<Args>(args). E.g.
  // base::unique_any a(base::in_place_type_t<std::unique_ptr<int>>(), 123);
  template <
      typename T,
      typename... Args,
      typename VT = std::decay_t<T>,
      std::enable_if_t<is_move_or_copy_constructible<VT>::value &&
                       std::is_constructible<VT, Args...>::value>* = nullptr>
  explicit unique_any(in_place_type_t<T> /*tag*/, Args&&... args) noexcept
      : internal_(&TypeOpsHelper<VT>::type_ops) {
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, std::forward<Args>(args)...);
  }

  // Constructs a base::unique_any containing an object of type T which is
  // initialized with a std::initializer_list<U> and std::forward for any
  // remaining args. E.g. base::unique_any
  // a(base::in_place_type_t<std::vector<int>>(), {1, 2, 3}); base::unique_any
  // b(base::in_place_type_t<std::vector<int>>(), {1, 2, 3}, 4, 5);
  template <typename T,
            typename U,
            typename... Args,
            typename VT = std::decay_t<T>,
            std::enable_if_t<is_move_or_copy_constructible<VT>::value &&
                             std::is_constructible<VT,
                                                   std::initializer_list<U>&,
                                                   Args...>::value>* = nullptr>
  explicit unique_any(in_place_type_t<T> /*tag*/,
                      std::initializer_list<U> ilist,
                      Args&&... args) noexcept
      : internal_(&internal::AnyInternal::TypeOpsHelper<VT>::type_ops) {
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, ilist, std::forward<Args>(args)...);
  }

  ~unique_any();

  // Emplaces a value within a base::unique_any object by calling reset(),
  // initializing the contained value as if direct-non-list-initializing an
  // object of type |VT| with the arguments |std::forward<Args>(args)...|, and
  // returning a reference to the new contained value.
  // E.g.
  // base::unique_any a;
  // a.emplace<std::unique_ptr<int>>(123);
  template <
      typename T,
      typename... Args,
      typename VT = std::decay_t<T>,
      std::enable_if_t<is_move_or_copy_constructible<VT>::value &&
                       std::is_constructible<VT, Args...>::value>* = nullptr>
  VT& emplace(Args&&... args) noexcept {
    reset();
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, std::forward<Args>(args)...);
    internal_.type_ops_ = &TypeOpsHelper<VT>::type_ops;
    return *internal_.GetStorage<VT>();
  }

  // Overload of |emplace()| to emplace a value within a |base::unique_any|
  // object by calling |reset()|, initializing the contained value as if
  // direct-non-list-initializing an object of type |VT| with the arguments
  // |initializer_list, std::forward<Args>(args)...|, and returning a reference
  // to the new contained value.
  // E.g.
  // base::unique_any a;
  // a.emplace<std::vector<int>>({1, 2, 3});
  template <typename T,
            class U,
            typename... Args,
            typename VT = std::decay_t<T>,
            std::enable_if_t<is_move_or_copy_constructible<VT>::value &&
                             std::is_constructible<VT,
                                                   std::initializer_list<U>&,
                                                   Args...>::value>* = nullptr>
  VT& emplace(std::initializer_list<U> ilist, Args&&... args) noexcept {
    reset();
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, ilist, std::forward<Args>(args)...);
    internal_.type_ops_ = &TypeOpsHelper<VT>::type_ops;
    return *internal_.GetStorage<VT>();
  }

  // Assigns |t| as long as |T| is move or copy constructible and it isn't
  // base::unique_any.
  template <typename T,
            typename VT = std::decay_t<T>,
            std::enable_if_t<is_move_or_copy_constructible<VT>::value &&
                             !internal::is_unique_any<VT>::value>* = nullptr>
  unique_any& operator=(T&& t) noexcept {
    reset();
    ConstructHelper<VT, InlineStorageHelper<VT>::kUseInlineStorage>::Construct(
        &internal_, std::forward<T>(t));
    internal_.type_ops_ = &TypeOpsHelper<VT>::type_ops;
    return *this;
  }

  unique_any& operator=(unique_any&& other) noexcept {
    internal_ = std::move(other.internal_);
    return *this;
  }

  void swap(unique_any& other) noexcept {
    using std::swap;
    swap(internal_, other.internal_);
  }

  bool has_value() const noexcept { return internal_.has_value(); }

  // Note unlike std::any we return TypeId which does not require RTTI.
  TypeId type() const noexcept { return internal_.type(); }

  void reset() noexcept { internal_.reset(); }

 private:
  template <typename ValueType>
  friend ValueType unique_any_cast(const unique_any& any);

  template <typename ValueType>
  friend ValueType unique_any_cast(unique_any& any);

  template <typename ValueType>
  friend ValueType unique_any_cast(unique_any&& any);

  template <typename ValueType>
  friend const ValueType* unique_any_cast(const unique_any* any) noexcept;

  template <typename ValueType>
  friend ValueType* unique_any_cast(unique_any* any) noexcept;

  internal::AnyInternal internal_;
};

// Swaps two base::Any values. Equivalent to |x.swap(y)| where |x| and |y| are
// base::Any types.
inline void swap(unique_any& x, unique_any& y) noexcept {
  x.swap(y);
}

template <typename T, typename... Args>
unique_any make_unique_any(Args&&... args) {
  return unique_any(in_place_type_t<T>(), std::forward<Args>(args)...);
}

template <typename T, typename U, typename... Args>
unique_any make_unique_any(std::initializer_list<U> il, Args&&... args) {
  return unique_any(in_place_type_t<T>(), il, std::forward<Args>(args)...);
}

template <typename ValueType>
ValueType unique_any_cast(const unique_any& any) {
  using U = typename std::remove_cv<
      typename std::remove_reference<ValueType>::type>::type;
  static_assert(std::is_constructible<ValueType, const U&>::value,
                "Invalid ValueType");
  DCHECK(any.has_value());
  CHECK_EQ(TypeId::From<U>(), any.type());
  return *any.internal_.GetStorage<U>();
}

template <typename ValueType>
ValueType unique_any_cast(unique_any& any) {
  using U = typename std::remove_cv<
      typename std::remove_reference<ValueType>::type>::type;
  static_assert(std::is_constructible<ValueType, U&>::value,
                "Invalid ValueType");
  DCHECK(any.has_value());
  CHECK_EQ(TypeId::From<U>(), any.type());
  return *any.internal_.GetStorage<U>();
}

template <typename ValueType>
ValueType unique_any_cast(unique_any&& any) {
  using U = typename std::remove_cv<
      typename std::remove_reference<ValueType>::type>::type;
  static_assert(std::is_constructible<ValueType, U>::value,
                "Invalid ValueType");
  DCHECK(any.has_value());
  CHECK_EQ(TypeId::From<U>(), any.type());
  return std::move(*any.internal_.GetStorage<U>());
}

template <typename ValueType>
const ValueType* unique_any_cast(const unique_any* any) noexcept {
  using U = typename std::remove_cv<ValueType>::type;
  if (TypeId::From<U>() != any->type())
    return nullptr;
  return any->internal_.GetStorage<U>();
}

template <typename ValueType>
ValueType* unique_any_cast(unique_any* any) noexcept {
  using U = typename std::remove_cv<ValueType>::type;
  if (TypeId::From<U>() != any->type())
    return nullptr;
  return any->internal_.GetStorage<U>();
}

}  // namespace base

#endif  // BASE_CONTAINERS_UNIQUE_ANY_H_
