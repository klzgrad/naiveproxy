// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_SCOPED_REFPTR_H_
#define BASE_MEMORY_SCOPED_REFPTR_H_

#include <stddef.h>

#include <iosfwd>
#include <type_traits>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"

template <class T>
class scoped_refptr;

namespace base {

template <class, typename>
class RefCounted;
template <class, typename>
class RefCountedThreadSafe;

template <typename T>
scoped_refptr<T> AdoptRef(T* t);

namespace subtle {

enum AdoptRefTag { kAdoptRefTag };
enum StartRefCountFromZeroTag { kStartRefCountFromZeroTag };
enum StartRefCountFromOneTag { kStartRefCountFromOneTag };

template <typename T, typename U, typename V>
constexpr bool IsRefCountPreferenceOverridden(const T*,
                                              const RefCounted<U, V>*) {
  return !std::is_same<std::decay_t<decltype(T::kRefCountPreference)>,
                       std::decay_t<decltype(U::kRefCountPreference)>>::value;
}

template <typename T, typename U, typename V>
constexpr bool IsRefCountPreferenceOverridden(
    const T*,
    const RefCountedThreadSafe<U, V>*) {
  return !std::is_same<std::decay_t<decltype(T::kRefCountPreference)>,
                       std::decay_t<decltype(U::kRefCountPreference)>>::value;
}

constexpr bool IsRefCountPreferenceOverridden(...) {
  return false;
}

}  // namespace subtle

// Creates a scoped_refptr from a raw pointer without incrementing the reference
// count. Use this only for a newly created object whose reference count starts
// from 1 instead of 0.
template <typename T>
scoped_refptr<T> AdoptRef(T* obj) {
  using Tag = std::decay_t<decltype(T::kRefCountPreference)>;
  static_assert(std::is_same<subtle::StartRefCountFromOneTag, Tag>::value,
                "Use AdoptRef only for the reference count starts from one.");

  DCHECK(obj);
  DCHECK(obj->HasOneRef());
  obj->Adopted();
  return scoped_refptr<T>(obj, subtle::kAdoptRefTag);
}

namespace subtle {

template <typename T>
scoped_refptr<T> AdoptRefIfNeeded(T* obj, StartRefCountFromZeroTag) {
  return scoped_refptr<T>(obj);
}

template <typename T>
scoped_refptr<T> AdoptRefIfNeeded(T* obj, StartRefCountFromOneTag) {
  return AdoptRef(obj);
}

}  // namespace subtle

// Constructs an instance of T, which is a ref counted type, and wraps the
// object into a scoped_refptr<T>.
template <typename T, typename... Args>
scoped_refptr<T> MakeRefCounted(Args&&... args) {
  T* obj = new T(std::forward<Args>(args)...);
  return subtle::AdoptRefIfNeeded(obj, T::kRefCountPreference);
}

// Takes an instance of T, which is a ref counted type, and wraps the object
// into a scoped_refptr<T>.
template <typename T>
scoped_refptr<T> WrapRefCounted(T* t) {
  return scoped_refptr<T>(t);
}

}  // namespace base

//
// A smart pointer class for reference counted objects.  Use this class instead
// of calling AddRef and Release manually on a reference counted object to
// avoid common memory leaks caused by forgetting to Release an object
// reference.  Sample usage:
//
//   class MyFoo : public RefCounted<MyFoo> {
//    ...
//    private:
//     friend class RefCounted<MyFoo>;  // Allow destruction by RefCounted<>.
//     ~MyFoo();                        // Destructor must be private/protected.
//   };
//
//   void some_function() {
//     scoped_refptr<MyFoo> foo = new MyFoo();
//     foo->Method(param);
//     // |foo| is released when this function returns
//   }
//
//   void some_other_function() {
//     scoped_refptr<MyFoo> foo = new MyFoo();
//     ...
//     foo = nullptr;  // explicitly releases |foo|
//     ...
//     if (foo)
//       foo->Method(param);
//   }
//
// The above examples show how scoped_refptr<T> acts like a pointer to T.
// Given two scoped_refptr<T> classes, it is also possible to exchange
// references between the two objects, like so:
//
//   {
//     scoped_refptr<MyFoo> a = new MyFoo();
//     scoped_refptr<MyFoo> b;
//
//     b.swap(a);
//     // now, |b| references the MyFoo object, and |a| references nullptr.
//   }
//
// To make both |a| and |b| in the above example reference the same MyFoo
// object, simply use the assignment operator:
//
//   {
//     scoped_refptr<MyFoo> a = new MyFoo();
//     scoped_refptr<MyFoo> b;
//
//     b = a;
//     // now, |a| and |b| each own a reference to the same MyFoo object.
//   }
//
template <class T>
class scoped_refptr {
 public:
  typedef T element_type;

  scoped_refptr() {}

  scoped_refptr(T* p) : ptr_(p) {
    if (ptr_)
      AddRef(ptr_);
  }

  // Copy constructor.
  scoped_refptr(const scoped_refptr<T>& r) : ptr_(r.ptr_) {
    if (ptr_)
      AddRef(ptr_);
  }

  // Copy conversion constructor.
  template <typename U,
            typename = typename std::enable_if<
                std::is_convertible<U*, T*>::value>::type>
  scoped_refptr(const scoped_refptr<U>& r) : ptr_(r.get()) {
    if (ptr_)
      AddRef(ptr_);
  }

  // Move constructor. This is required in addition to the conversion
  // constructor below in order for clang to warn about pessimizing moves.
  scoped_refptr(scoped_refptr&& r) : ptr_(r.get()) { r.ptr_ = nullptr; }

  // Move conversion constructor.
  template <typename U,
            typename = typename std::enable_if<
                std::is_convertible<U*, T*>::value>::type>
  scoped_refptr(scoped_refptr<U>&& r) : ptr_(r.get()) {
    r.ptr_ = nullptr;
  }

  ~scoped_refptr() {
    static_assert(!base::subtle::IsRefCountPreferenceOverridden(
                      static_cast<T*>(nullptr), static_cast<T*>(nullptr)),
                  "It's unsafe to override the ref count preference."
                  " Please remove REQUIRE_ADOPTION_FOR_REFCOUNTED_TYPE"
                  " from subclasses.");
    if (ptr_)
      Release(ptr_);
  }

  T* get() const { return ptr_; }

  T& operator*() const {
    DCHECK(ptr_);
    return *ptr_;
  }

  T* operator->() const {
    DCHECK(ptr_);
    return ptr_;
  }

  scoped_refptr<T>& operator=(T* p) {
    // AddRef first so that self assignment should work
    if (p)
      AddRef(p);
    T* old_ptr = ptr_;
    ptr_ = p;
    if (old_ptr)
      Release(old_ptr);
    return *this;
  }

  scoped_refptr<T>& operator=(const scoped_refptr<T>& r) {
    return *this = r.ptr_;
  }

  template <typename U>
  scoped_refptr<T>& operator=(const scoped_refptr<U>& r) {
    return *this = r.get();
  }

  scoped_refptr<T>& operator=(scoped_refptr<T>&& r) {
    scoped_refptr<T> tmp(std::move(r));
    tmp.swap(*this);
    return *this;
  }

  template <typename U>
  scoped_refptr<T>& operator=(scoped_refptr<U>&& r) {
    // We swap with a temporary variable to guarantee that |ptr_| is released
    // immediately. A naive implementation which swaps |this| and |r| would
    // unintentionally extend the lifetime of |ptr_| to at least the lifetime of
    // |r|.
    scoped_refptr<T> tmp(std::move(r));
    tmp.swap(*this);
    return *this;
  }

  void swap(scoped_refptr<T>& r) {
    T* tmp = ptr_;
    ptr_ = r.ptr_;
    r.ptr_ = tmp;
  }

  explicit operator bool() const { return ptr_ != nullptr; }

  template <typename U>
  bool operator==(const scoped_refptr<U>& rhs) const {
    return ptr_ == rhs.get();
  }

  template <typename U>
  bool operator!=(const scoped_refptr<U>& rhs) const {
    return !operator==(rhs);
  }

  template <typename U>
  bool operator<(const scoped_refptr<U>& rhs) const {
    return ptr_ < rhs.get();
  }

 protected:
  T* ptr_ = nullptr;

 private:
  template <typename U>
  friend scoped_refptr<U> base::AdoptRef(U*);

  scoped_refptr(T* p, base::subtle::AdoptRefTag) : ptr_(p) {}

  // Friend required for move constructors that set r.ptr_ to null.
  template <typename U>
  friend class scoped_refptr;

  // Non-inline helpers to allow:
  //     class Opaque;
  //     extern template class scoped_refptr<Opaque>;
  // Otherwise the compiler will complain that Opaque is an incomplete type.
  static void AddRef(T* ptr);
  static void Release(T* ptr);
};

// static
template <typename T>
void scoped_refptr<T>::AddRef(T* ptr) {
  ptr->AddRef();
}

// static
template <typename T>
void scoped_refptr<T>::Release(T* ptr) {
  ptr->Release();
}

template <typename T, typename U>
bool operator==(const scoped_refptr<T>& lhs, const U* rhs) {
  return lhs.get() == rhs;
}

template <typename T, typename U>
bool operator==(const T* lhs, const scoped_refptr<U>& rhs) {
  return lhs == rhs.get();
}

template <typename T>
bool operator==(const scoped_refptr<T>& lhs, std::nullptr_t null) {
  return !static_cast<bool>(lhs);
}

template <typename T>
bool operator==(std::nullptr_t null, const scoped_refptr<T>& rhs) {
  return !static_cast<bool>(rhs);
}

template <typename T, typename U>
bool operator!=(const scoped_refptr<T>& lhs, const U* rhs) {
  return !operator==(lhs, rhs);
}

template <typename T, typename U>
bool operator!=(const T* lhs, const scoped_refptr<U>& rhs) {
  return !operator==(lhs, rhs);
}

template <typename T>
bool operator!=(const scoped_refptr<T>& lhs, std::nullptr_t null) {
  return !operator==(lhs, null);
}

template <typename T>
bool operator!=(std::nullptr_t null, const scoped_refptr<T>& rhs) {
  return !operator==(null, rhs);
}

template <typename T>
std::ostream& operator<<(std::ostream& out, const scoped_refptr<T>& p) {
  return out << p.get();
}

template <typename T>
void swap(scoped_refptr<T>& lhs, scoped_refptr<T>& rhs) {
  lhs.swap(rhs);
}

#endif  // BASE_MEMORY_SCOPED_REFPTR_H_
