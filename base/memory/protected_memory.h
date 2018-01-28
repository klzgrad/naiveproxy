// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Protected memory is memory holding security-sensitive data intended to be
// left read-only for the majority of its lifetime to avoid being overwritten
// by attackers. ProtectedMemory is a simple wrapper around platform-specific
// APIs to set memory read-write and read-only when required. Protected memory
// should be set read-write for the minimum amount of time required.

// Variables stored in protected memory must be global variables declared in the
// PROTECTED_MEMORY_SECTION so they are set to read-only upon start-up.

#ifndef BASE_MEMORY_PROTECTED_MEMORY_H_
#define BASE_MEMORY_PROTECTED_MEMORY_H_

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"

#define PROTECTED_MEMORY_ENABLED 1

#if defined(OS_LINUX)
// Define the section read-only
__asm__(".section protected_memory, \"a\"\n\t");
#define PROTECTED_MEMORY_SECTION __attribute__((section("protected_memory")))

// Explicitly mark these variables hidden so the symbols are local to the
// currently built component. Otherwise they are created with global (external)
// linkage and component builds would break because a single pair of these
// symbols would override the rest.
__attribute__((visibility("hidden"))) extern char __start_protected_memory;
__attribute__((visibility("hidden"))) extern char __stop_protected_memory;

#elif defined(OS_MACOSX) && !defined(OS_IOS)
// The segment the section is in is defined read-only with a linker flag in
// build/config/mac/BUILD.gn
#define PROTECTED_MEMORY_SECTION \
  __attribute__((section("PROTECTED_MEMORY, protected_memory")))
extern char __start_protected_memory __asm(
    "section$start$PROTECTED_MEMORY$protected_memory");
extern char __stop_protected_memory __asm(
    "section$end$PROTECTED_MEMORY$protected_memory");

#else
#undef PROTECTED_MEMORY_ENABLED
#define PROTECTED_MEMORY_ENABLED 0
#define PROTECTED_MEMORY_SECTION
#endif

namespace base {

// Normally mutable variables are held in read-write memory and constant data
// is held in read-only memory to ensure it is not accidentally overwritten.
// In some cases we want to hold mutable variables in read-only memory, except
// when they are being written to, to ensure that they are not tampered with.
//
// ProtectedMemory is a container class intended to hold a single variable in
// read-only memory, except when explicitly set read-write. The variable can be
// set read-write by creating a scoped AutoWritableMemory object by calling
// AutoWritableMemory::Create(), the memory stays writable until the returned
// object goes out of scope and is destructed. The wrapped variable can be
// accessed using operator* and operator->.
//
// Instances of ProtectedMemory must be declared in the PROTECTED_MEMORY_SECTION
// and as global variables. Because protected memory variables are globals, the
// the same rules apply disallowing non-trivial constructors and destructors.
// Global definitions are required to avoid the linker placing statics in
// inlinable functions into a comdat section and setting the protected memory
// section read-write when they are merged.
//
// EXAMPLE:
//
//  struct Items { void* item1; };
//  static PROTECTED_MEMORY_SECTION ProtectedMemory<Items> items;
//  void InitializeItems() {
//    // Explicitly set items read-write before writing to it.
//    auto writer = AutoWritableMemory::Create(items);
//    items->item1 = /* ... */;
//    assert(items->item1 != nullptr);
//    // items is set back to read-only on the destruction of writer
//  }
//
//  using FnPtr = void (*)(void);
//  PROTECTED_MEMORY_SECTION ProtectedMemory<FnPtr> fnPtr;
//  FnPtr ResolveFnPtr(void) {
//    // The Initializer nested class is a helper class for creating a static
//    // initializer for a ProtectedMemory variable. It implicitly sets the
//    // variable read-write during initialization.
//    static ProtectedMemory<FnPtr>::Initializer(&fnPtr,
//      reinterpret_cast<FnPtr>(dlsym(/* ... */)));
//    return *fnPtr;
//  }

template <typename T>
class ProtectedMemory {
 public:
  ProtectedMemory() = default;

  // Expose direct access to the encapsulated variable
  T& operator*() { return data; }
  const T& operator*() const { return data; }
  T* operator->() { return &data; }
  const T* operator->() const { return &data; }

  // Helper class for creating simple ProtectedMemory static initializers.
  class Initializer {
   public:
    // Defined out-of-line below to break circular definition dependency between
    // ProtectedMemory and AutoWritableMemory.
    Initializer(ProtectedMemory<T>* PM, const T& Init);

    DISALLOW_IMPLICIT_CONSTRUCTORS(Initializer);
  };

 private:
  T data;

  DISALLOW_COPY_AND_ASSIGN(ProtectedMemory);
};

// DCHECK that the byte at |ptr| is read-only.
BASE_EXPORT void AssertMemoryIsReadOnly(const void* ptr);

// Abstract out platform-specific methods to get the beginning and end of the
// PROTECTED_MEMORY_SECTION. ProtectedMemoryEnd returns a pointer to the byte
// past the end of the PROTECTED_MEMORY_SECTION.
#if PROTECTED_MEMORY_ENABLED
constexpr void* ProtectedMemoryStart = &__start_protected_memory;
constexpr void* ProtectedMemoryEnd = &__stop_protected_memory;
#endif

#if defined(COMPONENT_BUILD)
namespace internal {

// For component builds we want to define a separate global writers variable
// (explained below) in every DSO that includes this header. To do that we use
// this template to define a global without duplicate symbol errors.
template <typename T>
struct DsoSpecific {
  static T value;
};
template <typename T>
T DsoSpecific<T>::value = 0;

}  // namespace internal
#endif  // defined(COMPONENT_BUILD)

// A class that sets a given ProtectedMemory variable writable while the
// AutoWritableMemory is in scope. This class implements the logic for setting
// the protected memory region read-only/read-write in a thread-safe manner.
class AutoWritableMemory {
 private:
  // 'writers' is a global holding the number of ProtectedMemory instances set
  // writable, used to avoid races setting protected memory readable/writable.
  // When this reaches zero the protected memory region is set read only.
  // Access is controlled by writers_lock.
#if defined(COMPONENT_BUILD)
  // For component builds writers is a reference to an int defined separately in
  // every DSO.
  static constexpr int& writers = internal::DsoSpecific<int>::value;
#else
  // Otherwise, we declare writers in the protected memory section to avoid the
  // scenario where an attacker could overwrite it with a large value and invoke
  // code that constructs and destructs an AutoWritableMemory. After such a call
  // protected memory would still be set writable because writers > 0.
  static int writers;
#endif  // defined(COMPONENT_BUILD)

  // Synchronizes access to the writers variable and the simultaneous actions
  // that need to happen alongside writers changes, e.g. setting the protected
  // memory region readable when writers is decremented to 0.
  static BASE_EXPORT base::LazyInstance<Lock>::Leaky writers_lock;

  // Abstract out platform-specific memory APIs. |end| points to the byte past
  // the end of the region of memory having its memory protections changed.
  BASE_EXPORT bool SetMemoryReadWrite(void* start, void* end);
  BASE_EXPORT bool SetMemoryReadOnly(void* start, void* end);

  // If this is the first writer (e.g. writers == 0) set the writers variable
  // read-write. Next, increment writers and set the requested memory writable.
  AutoWritableMemory(void* ptr, void* ptr_end) {
#if PROTECTED_MEMORY_ENABLED
    DCHECK(ptr >= ProtectedMemoryStart && ptr_end <= ProtectedMemoryEnd);

    {
      base::AutoLock auto_lock(writers_lock.Get());
      if (writers == 0) {
        AssertMemoryIsReadOnly(ptr);
#if !defined(COMPONENT_BUILD)
        AssertMemoryIsReadOnly(&writers);
        CHECK(SetMemoryReadWrite(&writers, &writers + 1));
#endif  // !defined(COMPONENT_BUILD)
      }

      writers++;
    }

    CHECK(SetMemoryReadWrite(ptr, ptr_end));
#endif  // PROTECTED_MEMORY_ENABLED
  }

 public:
  // Wrap the private constructor to create an easy-to-use interface to
  // construct AutoWritableMemory objects.
  template <typename T>
  static AutoWritableMemory Create(ProtectedMemory<T>& PM) {
    T* ptr = &*PM;
    return AutoWritableMemory(ptr, ptr + 1);
  }

  // Move constructor just increments writers
  AutoWritableMemory(AutoWritableMemory&& original) {
#if PROTECTED_MEMORY_ENABLED
    base::AutoLock auto_lock(writers_lock.Get());
    CHECK_GT(writers, 0);
    writers++;
#endif  // PROTECTED_MEMORY_ENABLED
  }

  // On destruction decrement writers, and if no other writers exist, set the
  // entire protected memory region read-only.
  ~AutoWritableMemory() {
#if PROTECTED_MEMORY_ENABLED
    base::AutoLock auto_lock(writers_lock.Get());
    CHECK_GT(writers, 0);
    writers--;

    if (writers == 0) {
      CHECK(SetMemoryReadOnly(ProtectedMemoryStart, ProtectedMemoryEnd));
#if !defined(COMPONENT_BUILD)
      AssertMemoryIsReadOnly(&writers);
#endif  // !defined(COMPONENT_BUILD)
    }
#endif  // PROTECTED_MEMORY_ENABLED
  }

  DISALLOW_IMPLICIT_CONSTRUCTORS(AutoWritableMemory);
};

template <typename T>
ProtectedMemory<T>::Initializer::Initializer(ProtectedMemory<T>* PM,
                                             const T& Init) {
  AutoWritableMemory writer = AutoWritableMemory::Create(*PM);
  **PM = Init;
}

}  // namespace base

#endif  // BASE_MEMORY_PROTECTED_MEMORY_H_
