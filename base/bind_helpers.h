// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This defines a set of argument wrappers and related factory methods that
// can be used specify the refcounting and reference semantics of arguments
// that are bound by the Bind() function in base/bind.h.
//
// It also defines a set of simple functions and utilities that people want
// when using Callback<> and Bind().
//
//
// ARGUMENT BINDING WRAPPERS
//
// The wrapper functions are base::Unretained(), base::Owned(), base::Passed(),
// base::ConstRef(), and base::IgnoreResult().
//
// Unretained() allows Bind() to bind a non-refcounted class, and to disable
// refcounting on arguments that are refcounted objects.
//
// Owned() transfers ownership of an object to the Callback resulting from
// bind; the object will be deleted when the Callback is deleted.
//
// Passed() is for transferring movable-but-not-copyable types (eg. unique_ptr)
// through a Callback. Logically, this signifies a destructive transfer of
// the state of the argument into the target function.  Invoking
// Callback::Run() twice on a Callback that was created with a Passed()
// argument will CHECK() because the first invocation would have already
// transferred ownership to the target function.
//
// RetainedRef() accepts a ref counted object and retains a reference to it.
// When the callback is called, the object is passed as a raw pointer.
//
// ConstRef() allows binding a constant reference to an argument rather
// than a copy.
//
// IgnoreResult() is used to adapt a function or Callback with a return type to
// one with a void return. This is most useful if you have a function with,
// say, a pesky ignorable bool return that you want to use with PostTask or
// something else that expect a Callback with a void return.
//
// EXAMPLE OF Unretained():
//
//   class Foo {
//    public:
//     void func() { cout << "Foo:f" << endl; }
//   };
//
//   // In some function somewhere.
//   Foo foo;
//   Closure foo_callback =
//       Bind(&Foo::func, Unretained(&foo));
//   foo_callback.Run();  // Prints "Foo:f".
//
// Without the Unretained() wrapper on |&foo|, the above call would fail
// to compile because Foo does not support the AddRef() and Release() methods.
//
//
// EXAMPLE OF Owned():
//
//   void foo(int* arg) { cout << *arg << endl }
//
//   int* pn = new int(1);
//   Closure foo_callback = Bind(&foo, Owned(pn));
//
//   foo_callback.Run();  // Prints "1"
//   foo_callback.Run();  // Prints "1"
//   *n = 2;
//   foo_callback.Run();  // Prints "2"
//
//   foo_callback.Reset();  // |pn| is deleted.  Also will happen when
//                          // |foo_callback| goes out of scope.
//
// Without Owned(), someone would have to know to delete |pn| when the last
// reference to the Callback is deleted.
//
// EXAMPLE OF RetainedRef():
//
//    void foo(RefCountedBytes* bytes) {}
//
//    scoped_refptr<RefCountedBytes> bytes = ...;
//    Closure callback = Bind(&foo, base::RetainedRef(bytes));
//    callback.Run();
//
// Without RetainedRef, the scoped_refptr would try to implicitly convert to
// a raw pointer and fail compilation:
//
//    Closure callback = Bind(&foo, bytes); // ERROR!
//
//
// EXAMPLE OF ConstRef():
//
//   void foo(int arg) { cout << arg << endl }
//
//   int n = 1;
//   Closure no_ref = Bind(&foo, n);
//   Closure has_ref = Bind(&foo, ConstRef(n));
//
//   no_ref.Run();  // Prints "1"
//   has_ref.Run();  // Prints "1"
//
//   n = 2;
//   no_ref.Run();  // Prints "1"
//   has_ref.Run();  // Prints "2"
//
// Note that because ConstRef() takes a reference on |n|, |n| must outlive all
// its bound callbacks.
//
//
// EXAMPLE OF IgnoreResult():
//
//   int DoSomething(int arg) { cout << arg << endl; }
//
//   // Assign to a Callback with a void return type.
//   Callback<void(int)> cb = Bind(IgnoreResult(&DoSomething));
//   cb->Run(1);  // Prints "1".
//
//   // Prints "1" on |ml|.
//   ml->PostTask(FROM_HERE, Bind(IgnoreResult(&DoSomething), 1);
//
//
// EXAMPLE OF Passed():
//
//   void TakesOwnership(std::unique_ptr<Foo> arg) { }
//   std::unique_ptr<Foo> CreateFoo() { return std::unique_ptr<Foo>(new Foo());
//   }
//
//   std::unique_ptr<Foo> f(new Foo());
//
//   // |cb| is given ownership of Foo(). |f| is now NULL.
//   // You can use std::move(f) in place of &f, but it's more verbose.
//   Closure cb = Bind(&TakesOwnership, Passed(&f));
//
//   // Run was never called so |cb| still owns Foo() and deletes
//   // it on Reset().
//   cb.Reset();
//
//   // |cb| is given a new Foo created by CreateFoo().
//   cb = Bind(&TakesOwnership, Passed(CreateFoo()));
//
//   // |arg| in TakesOwnership() is given ownership of Foo(). |cb|
//   // no longer owns Foo() and, if reset, would not delete Foo().
//   cb.Run();  // Foo() is now transferred to |arg| and deleted.
//   cb.Run();  // This CHECK()s since Foo() already been used once.
//
// Passed() is particularly useful with PostTask() when you are transferring
// ownership of an argument into a task, but don't necessarily know if the
// task will always be executed. This can happen if the task is cancellable
// or if it is posted to a TaskRunner.
//
//
// SIMPLE FUNCTIONS AND UTILITIES.
//
//   DoNothing() - Useful for creating a Closure that does nothing when called.
//   DeletePointer<T>() - Useful for creating a Closure that will delete a
//                        pointer when invoked. Only use this when necessary.
//                        In most cases MessageLoop::DeleteSoon() is a better
//                        fit.

#ifndef BASE_BIND_HELPERS_H_
#define BASE_BIND_HELPERS_H_

#include <stddef.h>

#include <type_traits>
#include <utility>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"

namespace base {

template <typename T>
struct IsWeakReceiver;

template <typename>
struct BindUnwrapTraits;

namespace internal {

template <typename Functor, typename SFINAE = void>
struct FunctorTraits;

template <typename T>
class UnretainedWrapper {
 public:
  explicit UnretainedWrapper(T* o) : ptr_(o) {}
  T* get() const { return ptr_; }
 private:
  T* ptr_;
};

template <typename T>
class ConstRefWrapper {
 public:
  explicit ConstRefWrapper(const T& o) : ptr_(&o) {}
  const T& get() const { return *ptr_; }
 private:
  const T* ptr_;
};

template <typename T>
class RetainedRefWrapper {
 public:
  explicit RetainedRefWrapper(T* o) : ptr_(o) {}
  explicit RetainedRefWrapper(scoped_refptr<T> o) : ptr_(std::move(o)) {}
  T* get() const { return ptr_.get(); }
 private:
  scoped_refptr<T> ptr_;
};

template <typename T>
struct IgnoreResultHelper {
  explicit IgnoreResultHelper(T functor) : functor_(std::move(functor)) {}
  explicit operator bool() const { return !!functor_; }

  T functor_;
};

// An alternate implementation is to avoid the destructive copy, and instead
// specialize ParamTraits<> for OwnedWrapper<> to change the StorageType to
// a class that is essentially a std::unique_ptr<>.
//
// The current implementation has the benefit though of leaving ParamTraits<>
// fully in callback_internal.h as well as avoiding type conversions during
// storage.
template <typename T>
class OwnedWrapper {
 public:
  explicit OwnedWrapper(T* o) : ptr_(o) {}
  ~OwnedWrapper() { delete ptr_; }
  T* get() const { return ptr_; }
  OwnedWrapper(OwnedWrapper&& other) {
    ptr_ = other.ptr_;
    other.ptr_ = NULL;
  }

 private:
  mutable T* ptr_;
};

// PassedWrapper is a copyable adapter for a scoper that ignores const.
//
// It is needed to get around the fact that Bind() takes a const reference to
// all its arguments.  Because Bind() takes a const reference to avoid
// unnecessary copies, it is incompatible with movable-but-not-copyable
// types; doing a destructive "move" of the type into Bind() would violate
// the const correctness.
//
// This conundrum cannot be solved without either C++11 rvalue references or
// a O(2^n) blowup of Bind() templates to handle each combination of regular
// types and movable-but-not-copyable types.  Thus we introduce a wrapper type
// that is copyable to transmit the correct type information down into
// BindState<>. Ignoring const in this type makes sense because it is only
// created when we are explicitly trying to do a destructive move.
//
// Two notes:
//  1) PassedWrapper supports any type that has a move constructor, however
//     the type will need to be specifically whitelisted in order for it to be
//     bound to a Callback. We guard this explicitly at the call of Passed()
//     to make for clear errors. Things not given to Passed() will be forwarded
//     and stored by value which will not work for general move-only types.
//  2) is_valid_ is distinct from NULL because it is valid to bind a "NULL"
//     scoper to a Callback and allow the Callback to execute once.
template <typename T>
class PassedWrapper {
 public:
  explicit PassedWrapper(T&& scoper)
      : is_valid_(true), scoper_(std::move(scoper)) {}
  PassedWrapper(PassedWrapper&& other)
      : is_valid_(other.is_valid_), scoper_(std::move(other.scoper_)) {}
  T Take() const {
    CHECK(is_valid_);
    is_valid_ = false;
    return std::move(scoper_);
  }

 private:
  mutable bool is_valid_;
  mutable T scoper_;
};

template <typename T>
using Unwrapper = BindUnwrapTraits<std::decay_t<T>>;

template <typename T>
auto Unwrap(T&& o) -> decltype(Unwrapper<T>::Unwrap(std::forward<T>(o))) {
  return Unwrapper<T>::Unwrap(std::forward<T>(o));
}

// IsWeakMethod is a helper that determine if we are binding a WeakPtr<> to a
// method.  It is used internally by Bind() to select the correct
// InvokeHelper that will no-op itself in the event the WeakPtr<> for
// the target object is invalidated.
//
// The first argument should be the type of the object that will be received by
// the method.
template <bool is_method, typename... Args>
struct IsWeakMethod : std::false_type {};

template <typename T, typename... Args>
struct IsWeakMethod<true, T, Args...> : IsWeakReceiver<T> {};

// Packs a list of types to hold them in a single type.
template <typename... Types>
struct TypeList {};

// Used for DropTypeListItem implementation.
template <size_t n, typename List>
struct DropTypeListItemImpl;

// Do not use enable_if and SFINAE here to avoid MSVC2013 compile failure.
template <size_t n, typename T, typename... List>
struct DropTypeListItemImpl<n, TypeList<T, List...>>
    : DropTypeListItemImpl<n - 1, TypeList<List...>> {};

template <typename T, typename... List>
struct DropTypeListItemImpl<0, TypeList<T, List...>> {
  using Type = TypeList<T, List...>;
};

template <>
struct DropTypeListItemImpl<0, TypeList<>> {
  using Type = TypeList<>;
};

// A type-level function that drops |n| list item from given TypeList.
template <size_t n, typename List>
using DropTypeListItem = typename DropTypeListItemImpl<n, List>::Type;

// Used for TakeTypeListItem implementation.
template <size_t n, typename List, typename... Accum>
struct TakeTypeListItemImpl;

// Do not use enable_if and SFINAE here to avoid MSVC2013 compile failure.
template <size_t n, typename T, typename... List, typename... Accum>
struct TakeTypeListItemImpl<n, TypeList<T, List...>, Accum...>
    : TakeTypeListItemImpl<n - 1, TypeList<List...>, Accum..., T> {};

template <typename T, typename... List, typename... Accum>
struct TakeTypeListItemImpl<0, TypeList<T, List...>, Accum...> {
  using Type = TypeList<Accum...>;
};

template <typename... Accum>
struct TakeTypeListItemImpl<0, TypeList<>, Accum...> {
  using Type = TypeList<Accum...>;
};

// A type-level function that takes first |n| list item from given TypeList.
// E.g. TakeTypeListItem<3, TypeList<A, B, C, D>> is evaluated to
// TypeList<A, B, C>.
template <size_t n, typename List>
using TakeTypeListItem = typename TakeTypeListItemImpl<n, List>::Type;

// Used for ConcatTypeLists implementation.
template <typename List1, typename List2>
struct ConcatTypeListsImpl;

template <typename... Types1, typename... Types2>
struct ConcatTypeListsImpl<TypeList<Types1...>, TypeList<Types2...>> {
  using Type = TypeList<Types1..., Types2...>;
};

// A type-level function that concats two TypeLists.
template <typename List1, typename List2>
using ConcatTypeLists = typename ConcatTypeListsImpl<List1, List2>::Type;

// Used for MakeFunctionType implementation.
template <typename R, typename ArgList>
struct MakeFunctionTypeImpl;

template <typename R, typename... Args>
struct MakeFunctionTypeImpl<R, TypeList<Args...>> {
  // MSVC 2013 doesn't support Type Alias of function types.
  // Revisit this after we update it to newer version.
  typedef R Type(Args...);
};

// A type-level function that constructs a function type that has |R| as its
// return type and has TypeLists items as its arguments.
template <typename R, typename ArgList>
using MakeFunctionType = typename MakeFunctionTypeImpl<R, ArgList>::Type;

// Used for ExtractArgs and ExtractReturnType.
template <typename Signature>
struct ExtractArgsImpl;

template <typename R, typename... Args>
struct ExtractArgsImpl<R(Args...)> {
  using ReturnType = R;
  using ArgsList = TypeList<Args...>;
};

// A type-level function that extracts function arguments into a TypeList.
// E.g. ExtractArgs<R(A, B, C)> is evaluated to TypeList<A, B, C>.
template <typename Signature>
using ExtractArgs = typename ExtractArgsImpl<Signature>::ArgsList;

// A type-level function that extracts the return type of a function.
// E.g. ExtractReturnType<R(A, B, C)> is evaluated to R.
template <typename Signature>
using ExtractReturnType = typename ExtractArgsImpl<Signature>::ReturnType;

}  // namespace internal

template <typename T>
static inline internal::UnretainedWrapper<T> Unretained(T* o) {
  return internal::UnretainedWrapper<T>(o);
}

template <typename T>
static inline internal::RetainedRefWrapper<T> RetainedRef(T* o) {
  return internal::RetainedRefWrapper<T>(o);
}

template <typename T>
static inline internal::RetainedRefWrapper<T> RetainedRef(scoped_refptr<T> o) {
  return internal::RetainedRefWrapper<T>(std::move(o));
}

template <typename T>
static inline internal::ConstRefWrapper<T> ConstRef(const T& o) {
  return internal::ConstRefWrapper<T>(o);
}

template <typename T>
static inline internal::OwnedWrapper<T> Owned(T* o) {
  return internal::OwnedWrapper<T>(o);
}

// We offer 2 syntaxes for calling Passed().  The first takes an rvalue and
// is best suited for use with the return value of a function or other temporary
// rvalues. The second takes a pointer to the scoper and is just syntactic sugar
// to avoid having to write Passed(std::move(scoper)).
//
// Both versions of Passed() prevent T from being an lvalue reference. The first
// via use of enable_if, and the second takes a T* which will not bind to T&.
template <typename T,
          std::enable_if_t<!std::is_lvalue_reference<T>::value>* = nullptr>
static inline internal::PassedWrapper<T> Passed(T&& scoper) {
  return internal::PassedWrapper<T>(std::move(scoper));
}
template <typename T>
static inline internal::PassedWrapper<T> Passed(T* scoper) {
  return internal::PassedWrapper<T>(std::move(*scoper));
}

template <typename T>
static inline internal::IgnoreResultHelper<T> IgnoreResult(T data) {
  return internal::IgnoreResultHelper<T>(std::move(data));
}

BASE_EXPORT void DoNothing();

template<typename T>
void DeletePointer(T* obj) {
  delete obj;
}

// An injection point to control |this| pointer behavior on a method invocation.
// If IsWeakReceiver<> is true_type for |T| and |T| is used for a receiver of a
// method, base::Bind cancels the method invocation if the receiver is tested as
// false.
// E.g. Foo::bar() is not called:
//   struct Foo : base::SupportsWeakPtr<Foo> {
//     void bar() {}
//   };
//
//   WeakPtr<Foo> oo = nullptr;
//   base::Bind(&Foo::bar, oo).Run();
template <typename T>
struct IsWeakReceiver : std::false_type {};

template <typename T>
struct IsWeakReceiver<internal::ConstRefWrapper<T>> : IsWeakReceiver<T> {};

template <typename T>
struct IsWeakReceiver<WeakPtr<T>> : std::true_type {};

// An injection point to control how bound objects passed to the target
// function. BindUnwrapTraits<>::Unwrap() is called for each bound objects right
// before the target function is invoked.
template <typename>
struct BindUnwrapTraits {
  template <typename T>
  static T&& Unwrap(T&& o) { return std::forward<T>(o); }
};

template <typename T>
struct BindUnwrapTraits<internal::UnretainedWrapper<T>> {
  static T* Unwrap(const internal::UnretainedWrapper<T>& o) {
    return o.get();
  }
};

template <typename T>
struct BindUnwrapTraits<internal::ConstRefWrapper<T>> {
  static const T& Unwrap(const internal::ConstRefWrapper<T>& o) {
    return o.get();
  }
};

template <typename T>
struct BindUnwrapTraits<internal::RetainedRefWrapper<T>> {
  static T* Unwrap(const internal::RetainedRefWrapper<T>& o) {
    return o.get();
  }
};

template <typename T>
struct BindUnwrapTraits<internal::OwnedWrapper<T>> {
  static T* Unwrap(const internal::OwnedWrapper<T>& o) {
    return o.get();
  }
};

template <typename T>
struct BindUnwrapTraits<internal::PassedWrapper<T>> {
  static T Unwrap(const internal::PassedWrapper<T>& o) {
    return o.Take();
  }
};

// CallbackCancellationTraits allows customization of Callback's cancellation
// semantics. By default, callbacks are not cancellable. A specialization should
// set is_cancellable = true and implement an IsCancelled() that returns if the
// callback should be cancelled.
template <typename Functor, typename BoundArgsTuple, typename SFINAE = void>
struct CallbackCancellationTraits {
  static constexpr bool is_cancellable = false;
};

// Specialization for method bound to weak pointer receiver.
template <typename Functor, typename... BoundArgs>
struct CallbackCancellationTraits<
    Functor,
    std::tuple<BoundArgs...>,
    std::enable_if_t<
        internal::IsWeakMethod<internal::FunctorTraits<Functor>::is_method,
                               BoundArgs...>::value>> {
  static constexpr bool is_cancellable = true;

  template <typename Receiver, typename... Args>
  static bool IsCancelled(const Functor&,
                          const Receiver& receiver,
                          const Args&...) {
    return !receiver;
  }
};

// Specialization for a nested bind.
template <typename Signature, typename... BoundArgs>
struct CallbackCancellationTraits<OnceCallback<Signature>,
                                  std::tuple<BoundArgs...>> {
  static constexpr bool is_cancellable = true;

  template <typename Functor>
  static bool IsCancelled(const Functor& functor, const BoundArgs&...) {
    return functor.IsCancelled();
  }
};

template <typename Signature, typename... BoundArgs>
struct CallbackCancellationTraits<RepeatingCallback<Signature>,
                                  std::tuple<BoundArgs...>> {
  static constexpr bool is_cancellable = true;

  template <typename Functor>
  static bool IsCancelled(const Functor& functor, const BoundArgs&...) {
    return functor.IsCancelled();
  }
};

}  // namespace base

#endif  // BASE_BIND_HELPERS_H_
