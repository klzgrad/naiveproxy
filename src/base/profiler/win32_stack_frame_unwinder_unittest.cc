// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/win32_stack_frame_unwinder.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

class TestUnwindFunctions : public Win32StackFrameUnwinder::UnwindFunctions {
 public:
  TestUnwindFunctions();

  PRUNTIME_FUNCTION LookupFunctionEntry(DWORD64 program_counter,
                                        PDWORD64 image_base) override;
  void VirtualUnwind(DWORD64 image_base,
                     DWORD64 program_counter,
                     PRUNTIME_FUNCTION runtime_function,
                     CONTEXT* context) override;
  ScopedModuleHandle GetModuleForProgramCounter(
      DWORD64 program_counter) override;

  // Instructs GetModuleForProgramCounter to return null on the next call.
  void SetUnloadedModule();

  // These functions set whether the next frame will have a RUNTIME_FUNCTION.
  void SetHasRuntimeFunction(CONTEXT* context);
  void SetNoRuntimeFunction(CONTEXT* context);

 private:
  enum { kImageBaseIncrement = 1 << 20 };

  static RUNTIME_FUNCTION* const kInvalidRuntimeFunction;

  bool module_is_loaded_;
  DWORD64 expected_program_counter_;
  DWORD64 next_image_base_;
  DWORD64 expected_image_base_;
  RUNTIME_FUNCTION* next_runtime_function_;
  std::vector<RUNTIME_FUNCTION> runtime_functions_;

  DISALLOW_COPY_AND_ASSIGN(TestUnwindFunctions);
};

RUNTIME_FUNCTION* const TestUnwindFunctions::kInvalidRuntimeFunction =
    reinterpret_cast<RUNTIME_FUNCTION*>(static_cast<uintptr_t>(-1));

TestUnwindFunctions::TestUnwindFunctions()
    : module_is_loaded_(true),
      expected_program_counter_(0),
      next_image_base_(kImageBaseIncrement),
      expected_image_base_(0),
      next_runtime_function_(kInvalidRuntimeFunction) {
}

PRUNTIME_FUNCTION TestUnwindFunctions::LookupFunctionEntry(
    DWORD64 program_counter,
    PDWORD64 image_base) {
  EXPECT_EQ(expected_program_counter_, program_counter);
  *image_base = expected_image_base_ = next_image_base_;
  next_image_base_ += kImageBaseIncrement;
  RUNTIME_FUNCTION* return_value = next_runtime_function_;
  next_runtime_function_ = kInvalidRuntimeFunction;
  return return_value;
}

void TestUnwindFunctions::VirtualUnwind(DWORD64 image_base,
                                        DWORD64 program_counter,
                                        PRUNTIME_FUNCTION runtime_function,
                                        CONTEXT* context) {
  ASSERT_NE(kInvalidRuntimeFunction, runtime_function)
      << "expected call to SetHasRuntimeFunction() or SetNoRuntimeFunction() "
      << "before invoking TryUnwind()";
  EXPECT_EQ(expected_image_base_, image_base);
  expected_image_base_ = 0;
  EXPECT_EQ(expected_program_counter_, program_counter);
  expected_program_counter_ = 0;
  // This function should only be called when LookupFunctionEntry returns
  // a RUNTIME_FUNCTION.
  EXPECT_EQ(&runtime_functions_.back(), runtime_function);
}

ScopedModuleHandle TestUnwindFunctions::GetModuleForProgramCounter(
    DWORD64 program_counter) {
  bool return_non_null_value = module_is_loaded_;
  module_is_loaded_ = true;
  return ScopedModuleHandle(return_non_null_value ?
                            ModuleHandleTraits::kNonNullModuleForTesting :
                            nullptr);
}

void TestUnwindFunctions::SetUnloadedModule() {
  module_is_loaded_ = false;
}

void TestUnwindFunctions::SetHasRuntimeFunction(CONTEXT* context) {
  RUNTIME_FUNCTION runtime_function = {};
  runtime_function.BeginAddress = 16;
  runtime_function.EndAddress = runtime_function.BeginAddress + 256;
  runtime_functions_.push_back(runtime_function);
  next_runtime_function_ = &runtime_functions_.back();

  expected_program_counter_ = context->Rip =
      next_image_base_ + runtime_function.BeginAddress + 8;
}

void TestUnwindFunctions::SetNoRuntimeFunction(CONTEXT* context) {
  expected_program_counter_ = context->Rip = 100;
  next_runtime_function_ = nullptr;
}

}  // namespace

class Win32StackFrameUnwinderTest : public testing::Test {
 protected:
  Win32StackFrameUnwinderTest() {}

  // This exists so that Win32StackFrameUnwinder's constructor can be private
  // with a single friend declaration of this test fixture.
  std::unique_ptr<Win32StackFrameUnwinder> CreateUnwinder();

  // Weak pointer to the unwind functions used by last created unwinder.
  TestUnwindFunctions* unwind_functions_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Win32StackFrameUnwinderTest);
};

std::unique_ptr<Win32StackFrameUnwinder>
Win32StackFrameUnwinderTest::CreateUnwinder() {
  std::unique_ptr<TestUnwindFunctions> unwind_functions(
      new TestUnwindFunctions);
  unwind_functions_ = unwind_functions.get();
  return WrapUnique(
      new Win32StackFrameUnwinder(std::move(unwind_functions)));
}

// Checks the case where all frames have unwind information.
TEST_F(Win32StackFrameUnwinderTest, FramesWithUnwindInfo) {
  std::unique_ptr<Win32StackFrameUnwinder> unwinder = CreateUnwinder();
  CONTEXT context = {0};
  ScopedModuleHandle module;

  unwind_functions_->SetHasRuntimeFunction(&context);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_TRUE(module.IsValid());

  unwind_functions_->SetHasRuntimeFunction(&context);
  module.Set(nullptr);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_TRUE(module.IsValid());

  unwind_functions_->SetHasRuntimeFunction(&context);
  module.Set(nullptr);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_TRUE(module.IsValid());
}

// Checks that an instruction pointer in an unloaded module fails to unwind.
TEST_F(Win32StackFrameUnwinderTest, UnloadedModule) {
  std::unique_ptr<Win32StackFrameUnwinder> unwinder = CreateUnwinder();
  CONTEXT context = {0};
  ScopedModuleHandle module;

  unwind_functions_->SetUnloadedModule();
  EXPECT_FALSE(unwinder->TryUnwind(&context, &module));
}

// Checks that the CONTEXT's stack pointer gets popped when the top frame has no
// unwind information.
TEST_F(Win32StackFrameUnwinderTest, FrameAtTopWithoutUnwindInfo) {
  std::unique_ptr<Win32StackFrameUnwinder> unwinder = CreateUnwinder();
  CONTEXT context = {0};
  ScopedModuleHandle module;
  DWORD64 next_ip = 0x0123456789abcdef;
  DWORD64 original_rsp = reinterpret_cast<DWORD64>(&next_ip);
  context.Rsp = original_rsp;

  unwind_functions_->SetNoRuntimeFunction(&context);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_EQ(next_ip, context.Rip);
  EXPECT_EQ(original_rsp + 8, context.Rsp);
  EXPECT_TRUE(module.IsValid());

  unwind_functions_->SetHasRuntimeFunction(&context);
  module.Set(nullptr);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_TRUE(module.IsValid());

  unwind_functions_->SetHasRuntimeFunction(&context);
  module.Set(nullptr);
  EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
  EXPECT_TRUE(module.IsValid());
}

// Checks that a frame below the top of the stack with missing unwind info
// terminates the unwinding.
TEST_F(Win32StackFrameUnwinderTest, FrameBelowTopWithoutUnwindInfo) {
  {
    // First stack, with a bad function below the top of the stack.
    std::unique_ptr<Win32StackFrameUnwinder> unwinder = CreateUnwinder();
    CONTEXT context = {0};
    ScopedModuleHandle module;
    unwind_functions_->SetHasRuntimeFunction(&context);
    EXPECT_TRUE(unwinder->TryUnwind(&context, &module));
    EXPECT_TRUE(module.IsValid());

    unwind_functions_->SetNoRuntimeFunction(&context);
    EXPECT_FALSE(unwinder->TryUnwind(&context, &module));
  }
}

}  // namespace base
