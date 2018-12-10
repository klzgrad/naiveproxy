// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper routines to call function pointers stored in protected memory with
// Control Flow Integrity indirect call checking disabled. Some indirect calls,
// e.g. dynamically resolved symbols in another DSO, can not be accounted for by
// CFI-icall. These routines allow those symbols to be called without CFI-icall
// checking safely by ensuring that they are placed in protected memory.

#ifndef BASE_MEMORY_PROTECTED_MEMORY_CFI_H_
#define BASE_MEMORY_PROTECTED_MEMORY_CFI_H_

#include <utility>

#include "base/cfi_buildflags.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/protected_memory.h"
#include "build/build_config.h"

#if BUILDFLAG(CFI_ICALL_CHECK) && !PROTECTED_MEMORY_ENABLED
#error "CFI-icall enabled for platform without protected memory support"
#endif  // BUILDFLAG(CFI_ICALL_CHECK) && !PROTECTED_MEMORY_ENABLED

namespace base {
namespace internal {

// This class is used to exempt calls to function pointers stored in
// ProtectedMemory from cfi-icall checking. It's not secure to use directly, it
// should only be used by the UnsanitizedCfiCall() functions below. Given an
// UnsanitizedCfiCall object, you can use operator() to call the encapsulated
// function pointer without cfi-icall checking.
template <typename FunctionType>
class UnsanitizedCfiCall {
 public:
  explicit UnsanitizedCfiCall(FunctionType function) : function_(function) {}
  UnsanitizedCfiCall(UnsanitizedCfiCall&&) = default;

  template <typename... Args>
  NO_SANITIZE("cfi-icall")
  auto operator()(Args&&... args) {
    return function_(std::forward<Args>(args)...);
  }

 private:
  FunctionType function_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(UnsanitizedCfiCall);
};

}  // namespace internal

// These functions can be used to call function pointers in ProtectedMemory
// without cfi-icall checking. They are intended to be used to create an
// UnsanitizedCfiCall object and immediately call it. UnsanitizedCfiCall objects
// should not initialized directly or stored because they hold a function
// pointer that will be called without CFI-icall checking in mutable memory. The
// functions can be used as shown below:

// ProtectedMemory<void (*)(int)> p;
// UnsanitizedCfiCall(p)(5); /* In place of (*p)(5); */

template <typename T>
auto UnsanitizedCfiCall(const ProtectedMemory<T>& PM) {
#if PROTECTED_MEMORY_ENABLED
  DCHECK(&PM >= ProtectedMemoryStart && &PM < ProtectedMemoryEnd);
#endif  // PROTECTED_MEMORY_ENABLED
  return internal::UnsanitizedCfiCall<T>(*PM);
}

// struct S { void (*fp)(int); } s;
// ProtectedMemory<S> p;
// UnsanitizedCfiCall(p, &S::fp)(5); /* In place of p->fp(5); */

template <typename T, typename Member>
auto UnsanitizedCfiCall(const ProtectedMemory<T>& PM, Member member) {
#if PROTECTED_MEMORY_ENABLED
  DCHECK(&PM >= ProtectedMemoryStart && &PM < ProtectedMemoryEnd);
#endif  // PROTECTED_MEMORY_ENABLED
  return internal::UnsanitizedCfiCall<decltype(*PM.*member)>(*PM.*member);
}

}  // namespace base

#endif  // BASE_MEMORY_PROTECTED_MEMORY_CFI_H_
