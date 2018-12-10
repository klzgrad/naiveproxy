// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_WIN32_STACK_FRAME_UNWINDER_H_
#define BASE_PROFILER_WIN32_STACK_FRAME_UNWINDER_H_

#include <windows.h>

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/win/scoped_handle.h"

namespace base {

#if !defined(_WIN64)
// Allows code to compile for x86. Actual support for x86 will require either
// refactoring these interfaces or separate architecture-specific interfaces.
struct RUNTIME_FUNCTION {
  DWORD BeginAddress;
  DWORD EndAddress;
};
using PRUNTIME_FUNCTION = RUNTIME_FUNCTION*;
#endif  // !defined(_WIN64)

// Traits class to adapt GenericScopedHandle for HMODULES.
class ModuleHandleTraits : public win::HandleTraits {
 public:
  using Handle = HMODULE;

  static bool BASE_EXPORT CloseHandle(HMODULE handle);
  static bool BASE_EXPORT IsHandleValid(HMODULE handle);
  static HMODULE BASE_EXPORT NullHandle();

  BASE_EXPORT static const HMODULE kNonNullModuleForTesting;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleHandleTraits);
};

// HMODULE is not really a handle, and has reference count semantics, so the
// standard VerifierTraits does not apply.
using ScopedModuleHandle =
    win::GenericScopedHandle<ModuleHandleTraits, win::DummyVerifierTraits>;

// Instances of this class are expected to be created and destroyed for each
// stack unwinding. This class is not used while the target thread is suspended,
// so may allocate from the default heap.
class BASE_EXPORT Win32StackFrameUnwinder {
 public:
  // Interface for Win32 unwind-related functionality this class depends
  // on. Provides a seam for testing.
  class BASE_EXPORT UnwindFunctions {
   public:
    virtual ~UnwindFunctions();

    virtual PRUNTIME_FUNCTION LookupFunctionEntry(DWORD64 program_counter,
                                                  PDWORD64 image_base) = 0;
    virtual void VirtualUnwind(DWORD64 image_base,
                               DWORD64 program_counter,
                               PRUNTIME_FUNCTION runtime_function,
                               CONTEXT* context) = 0;

    // Returns the module containing |program_counter|. Can return null if the
    // module has been unloaded.
    virtual ScopedModuleHandle GetModuleForProgramCounter(
        DWORD64 program_counter) = 0;

   protected:
    UnwindFunctions();

   private:
    DISALLOW_COPY_AND_ASSIGN(UnwindFunctions);
  };

  Win32StackFrameUnwinder();
  ~Win32StackFrameUnwinder();

  // Attempts to unwind the frame represented by the stack and instruction
  // pointers in |context|. If successful, updates |context| and provides the
  // module associated with the frame in |module|.
  bool TryUnwind(CONTEXT* context, ScopedModuleHandle* module);

 private:
  // This function is for internal and test purposes only.
  Win32StackFrameUnwinder(std::unique_ptr<UnwindFunctions> unwind_functions);
  friend class Win32StackFrameUnwinderTest;

  // State associated with each stack unwinding.
  bool at_top_frame_;
  bool unwind_info_present_for_all_frames_;

  std::unique_ptr<UnwindFunctions> unwind_functions_;

  DISALLOW_COPY_AND_ASSIGN(Win32StackFrameUnwinder);
};

}  // namespace base

#endif  // BASE_PROFILER_WIN32_STACK_FRAME_UNWINDER_H_
