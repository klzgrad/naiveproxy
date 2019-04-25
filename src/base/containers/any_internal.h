// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_ANY_INTERNAL_H_
#define BASE_CONTAINERS_ANY_INTERNAL_H_

#include <memory>
#include <utility>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/template_util.h"
#include "base/type_id.h"

namespace base {

namespace internal {

// Common non-templated implementation details for base::unique_any. If we ever
// want to support base::any this, file could be easily modified to do so
// (TypeOps would need to support copying).
class BASE_EXPORT AnyInternal {
 public:
  constexpr AnyInternal() noexcept : type_ops_(nullptr), union_({}) {}

  AnyInternal(AnyInternal&& other) noexcept
      : type_ops_(other.type_ops_), union_({}) {
    if (other.type_ops_)
      other.type_ops_->move_fn_ptr(&other, this);
  }

  ~AnyInternal();

  struct TypeOps;

  constexpr explicit AnyInternal(const TypeOps* type_ops)
      : type_ops_(type_ops), union_({}) {}

  void operator=(AnyInternal&& other) noexcept {
    reset();
    if (other.type_ops_)
      other.type_ops_->move_fn_ptr(&other, this);
    type_ops_ = other.type_ops_;
  }

  constexpr bool has_value() const noexcept { return !!type_ops_; }

  TypeId type() const noexcept {
    if (!type_ops_)
      return TypeId::From<void>();

    return type_ops_->type_fn_ptr();
  }

  void reset() noexcept;

  template <typename T, bool UseInlineStorage>
  struct ConstructHelper;

  template <bool UseInlineStorage>
  struct GetStorageHelper;

  template <typename T, bool UseInlineStorage, bool HasMoveConstructor>
  struct MoveHelper;

  template <typename T, bool UseInlineStorage>
  struct DeleteHelper;

  // Where possible we use the small object allocation optimization to avoid
  // heap allocations.
  struct OutlineAlloc {
    void* value;  // Holds a T

    template <typename T>
    T& value_as() {
      return *static_cast<T*>(value);
    }

    template <typename T>
    const T& value_as() const {
      return *static_cast<const T*>(value);
    }
  };

  struct alignas(sizeof(void*)) InlineAlloc {
    // Holds a T if small.
    char bytes[sizeof(void*)];

    template <typename T>
    T& value_as() {
      return *reinterpret_cast<T*>(bytes);
    }

    template <typename T>
    const T& value_as() const {
      return *reinterpret_cast<const T*>(bytes);
    }
  };

  template <typename T>
  struct InlineStorageHelper {
    static constexpr bool kUseInlineStorage =
        (sizeof(T) <= sizeof(InlineAlloc));

    static_assert(
        std::alignment_of<T>::value <= sizeof(T),
        "Type T has alignment requirements that preclude it's storage inline.");
  };

  template <typename T>
  constexpr T* GetStorage() {
    return static_cast<T*>(
        GetStorageHelper<InlineStorageHelper<T>::kUseInlineStorage>::GetStorage(
            *this));
  }

  template <typename T>
  constexpr const T* GetStorage() const {
    return static_cast<const T*>(
        GetStorageHelper<InlineStorageHelper<T>::kUseInlineStorage>::GetStorage(
            *this));
  }

  using TypeIdFunctionPtr = TypeId (*)();
  using MoveFunctionPtr = void (*)(AnyInternal* src, AnyInternal* dest);
  using DeleteFunctionPtr = void (*)(AnyInternal* object);

  // Similar to a virtual function but we don't need a dynamic memory
  // allocation. One possible design alternative would be to fold these methods
  // into T and use T in InlineAlloc (which would now have to
  // be bigger to accommodate the vtable pointer).
  struct TypeOps {
    // TODO(alexclarke): If TypeId can be constexpr store TypeId here directly.
    TypeIdFunctionPtr type_fn_ptr;
    MoveFunctionPtr move_fn_ptr;
    DeleteFunctionPtr delete_fn_ptr;
  };

  template <typename T>
  struct TypeOpsHelper {
    static constexpr TypeOps type_ops = {
        &TypeId::From<T>,
        &AnyInternal::MoveHelper<T,
                                 InlineStorageHelper<T>::kUseInlineStorage,
                                 std::is_move_constructible<T>::value>::Move,
        &AnyInternal::
            DeleteHelper<T, InlineStorageHelper<T>::kUseInlineStorage>::Delete};
  };

  // Null if the instance has no value.
  const TypeOps* type_ops_;

  union {
    OutlineAlloc outline_alloc;
    InlineAlloc inline_alloc;
  } union_;
};

// static
template <typename T>
const AnyInternal::TypeOps AnyInternal::TypeOpsHelper<T>::type_ops;

template <typename T>
struct AnyInternal::ConstructHelper<T, /* UseInlineStorage */ true> {
  template <typename... Args>
  static void Construct(AnyInternal* dest, Args&&... args) noexcept {
    new (dest->union_.inline_alloc.bytes) T(std::forward<Args>(args)...);
  }
};

template <typename T>
struct AnyInternal::ConstructHelper<T, /* UseInlineStorage */ false> {
  template <typename... Args>
  static void Construct(AnyInternal* dest, Args&&... args) noexcept {
    dest->union_.outline_alloc.value = new T(std::forward<Args>(args)...);
  }
};

template <>
struct AnyInternal::GetStorageHelper</* UseInlineStorage */ true> {
  static void* GetStorage(AnyInternal& any) {
    return &any.union_.inline_alloc.bytes;
  }

  static const void* GetStorage(const AnyInternal& any) {
    return &any.union_.inline_alloc.bytes;
  }
};

template <>
struct AnyInternal::GetStorageHelper</* UseInlineStorage */ false> {
  static void* GetStorage(AnyInternal& any) {
    return any.union_.outline_alloc.value;
  }

  static const void* GetStorage(const AnyInternal& any) {
    return any.union_.outline_alloc.value;
  }
};

template <typename T>
struct AnyInternal::
    MoveHelper<T, /* UseInlineStorage */ true, /* HasMoveConstructor */ true> {
  static void Move(AnyInternal* src, AnyInternal* dest) {
    DCHECK_NE(src, dest);
    new (dest->union_.inline_alloc.bytes)
        T(std::move(src->union_.inline_alloc.value_as<T>()));
  }
};

template <typename T>
struct AnyInternal::
    MoveHelper<T, /* UseInlineStorage */ false, /* HasMoveConstructor */ true> {
  static void Move(AnyInternal* src, AnyInternal* dest) {
    DCHECK_NE(src, dest);
    dest->union_.outline_alloc.value =
        new T(std::move(src->union_.outline_alloc.value_as<T>()));
  }
};

template <typename T>
struct AnyInternal::
    MoveHelper<T, /* UseInlineStorage */ true, /* HasMoveConstructor */ false> {
  static void Move(AnyInternal* src, AnyInternal* dest) {
    DCHECK_NE(src, dest);
    // Fall back to the copy constructor.
    new (dest->union_.inline_alloc.bytes)
        T(src->union_.inline_alloc.value_as<T>());
  }
};

template <typename T>
struct AnyInternal::MoveHelper<T,
                               /* UseInlineStorage */ false,
                               /* HasMoveConstructor */ false> {
  static void Move(AnyInternal* src, AnyInternal* dest) {
    DCHECK_NE(src, dest);
    // Fall back to the copy constructor.
    dest->union_.outline_alloc.value =
        new T(src->union_.outline_alloc.value_as<T>());
  }
};

template <typename T>
struct AnyInternal::DeleteHelper<T, /* UseInlineStorage */ true> {
  static void Delete(AnyInternal* any) {
    reinterpret_cast<T*>(&any->union_.inline_alloc.bytes)->~T();
  }
};

template <typename T>
struct AnyInternal::DeleteHelper<T, /* UseInlineStorage */ false> {
  static void Delete(AnyInternal* any) {
    delete static_cast<T*>(any->union_.outline_alloc.value);
  }
};

}  // namespace internal
}  // namespace base

#endif  // BASE_CONTAINERS_ANY_INTERNAL_H_
