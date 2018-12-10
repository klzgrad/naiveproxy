// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains macros and macro-like constructs (e.g., templates) that
// are commonly used throughout Chromium source. (It may also contain things
// that are closely related to things that are commonly used that belong in this
// file.)

#ifndef BASE_MACROS_H_
#define BASE_MACROS_H_

#include <stddef.h>  // For size_t.

// Distinguish mips32.
#if defined(__mips__) && (_MIPS_SIM == _ABIO32) && !defined(__mips32__)
#define __mips32__
#endif

// Distinguish mips64.
#if defined(__mips__) && (_MIPS_SIM == _ABI64) && !defined(__mips64__)
#define __mips64__
#endif

// Put this in the declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete

// Put this in the declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName) TypeName& operator=(const TypeName&) = delete

// Put this in the declarations for a class to be uncopyable and unassignable.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  DISALLOW_COPY(TypeName);                 \
  DISALLOW_ASSIGN(TypeName)

// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
// This is especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

// The arraysize(arr) macro returns the # of elements in an array arr.  The
// expression is a compile-time constant, and therefore can be used in defining
// new arrays, for example.  If you use arraysize on a pointer by mistake, you
// will get a compile-time error.  For the technical details, refer to
// http://blogs.msdn.com/b/the1/archive/2004/05/07/128242.aspx.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
//
// DEPRECATED, please use base::size(array) instead.
// TODO(https://crbug.com/837308): Replace existing arraysize usages.
template <typename T, size_t N> char (&ArraySizeHelper(T (&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

// Used to explicitly mark the return value of a function as unused. If you are
// really sure you don't want to do anything with the return value of a function
// that has been marked WARN_UNUSED_RESULT, wrap it with this. Example:
//
//   std::unique_ptr<MyType> my_var = ...;
//   if (TakeOwnership(my_var.get()) == SUCCESS)
//     ignore_result(my_var.release());
//
template<typename T>
inline void ignore_result(const T&) {
}

namespace base {

// Use these to declare and define a static local variable (static T;) so that
// it is leaked so that its destructors are not called at exit.  This is
// thread-safe.
//
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! DEPRECATED !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Please don't use this macro. Use a function-local static of type
// base::NoDestructor<T> instead:
//
// Factory& Factory::GetInstance() {
//   static base::NoDestructor<Factory> instance;
//   return *instance;
// }
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#define CR_DEFINE_STATIC_LOCAL(type, name, arguments) \
  static type& name = *new type arguments

// Workaround for MSVC, which expands __VA_ARGS__ as one macro argument. To
// work around this bug, wrap the entire expression in this macro...
#define CR_EXPAND_ARG(arg) arg

}  // base

#endif  // BASE_MACROS_H_
