// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstring>
#include <memory>
#include <numeric>
#include <utility>

#include "base/profiler/profile_builder.h"
#include "base/profiler/stack_sampler_impl.h"
#include "base/profiler/thread_delegate.h"
#include "base/profiler/unwinder.h"
#include "base/sampling_heap_profiler/module_cache.h"
#include "base/stl_util.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

using ::testing::ElementsAre;

class TestProfileBuilder : public ProfileBuilder {
 public:
  TestProfileBuilder(ModuleCache* module_cache) : module_cache_(module_cache) {}

  TestProfileBuilder(const TestProfileBuilder&) = delete;
  TestProfileBuilder& operator=(const TestProfileBuilder&) = delete;

  // ProfileBuilder
  ModuleCache* GetModuleCache() override { return module_cache_; }
  void RecordMetadata() override {}
  void OnSampleCompleted(std::vector<Frame> frames) override {}
  void OnProfileCompleted(TimeDelta profile_duration,
                          TimeDelta sampling_period) override {}

 private:
  ModuleCache* module_cache_;
};

// A thread delegate for use in tests that provides the expected behavior when
// operating on the supplied fake stack.
class TestThreadDelegate : public ThreadDelegate {
 public:
  class TestScopedSuspendThread : public ThreadDelegate::ScopedSuspendThread {
   public:
    TestScopedSuspendThread() = default;

    TestScopedSuspendThread(const TestScopedSuspendThread&) = delete;
    TestScopedSuspendThread& operator=(const TestScopedSuspendThread&) = delete;

    bool WasSuccessful() const override { return true; }
  };

  TestThreadDelegate(const std::vector<uintptr_t>& fake_stack,
                     // The register context will be initialized to
                     // *|thread_context| if non-null.
                     RegisterContext* thread_context = nullptr)
      : fake_stack_(fake_stack),
        thread_context_(thread_context) {}

  TestThreadDelegate(const TestThreadDelegate&) = delete;
  TestThreadDelegate& operator=(const TestThreadDelegate&) = delete;

  std::unique_ptr<ScopedSuspendThread> CreateScopedSuspendThread() override {
    return std::make_unique<TestScopedSuspendThread>();
  }

  bool GetThreadContext(RegisterContext* thread_context) override {
    if (thread_context_)
      *thread_context = *thread_context_;
    // Set the stack pointer to be consistent with the provided fake stack.
    RegisterContextStackPointer(thread_context) =
        reinterpret_cast<uintptr_t>(&fake_stack_[0]);
    RegisterContextInstructionPointer(thread_context) =
        reinterpret_cast<uintptr_t>(fake_stack_[0]);
    return true;
  }

  uintptr_t GetStackBaseAddress() const override {
    return reinterpret_cast<uintptr_t>(&fake_stack_[0] + fake_stack_.size());
  }

  bool CanCopyStack(uintptr_t stack_pointer) override { return true; }

  std::vector<uintptr_t*> GetRegistersToRewrite(
      RegisterContext* thread_context) override {
    return {&RegisterContextFramePointer(thread_context)};
  }

 private:
  // Must be a reference to retain the underlying allocation from the vector
  // passed to the constructor.
  const std::vector<uintptr_t>& fake_stack_;
  RegisterContext* thread_context_;
};

// Trivial unwinder implementation for testing.
class TestUnwinder : public Unwinder {
 public:
  TestUnwinder(size_t stack_size = 0,
               std::vector<uintptr_t>* stack_copy = nullptr,
               // Variable to fill in with the bottom address of the
               // copied stack. This will be different than
               // &(*stack_copy)[0] because |stack_copy| is a copy of the
               // copy so does not share memory with the actual copy.
               uintptr_t* stack_copy_bottom = nullptr)
      : stack_size_(stack_size),
        stack_copy_(stack_copy),
        stack_copy_bottom_(stack_copy_bottom) {}

  bool CanUnwindFrom(const Frame* current_frame) const override { return true; }

  UnwindResult TryUnwind(RegisterContext* thread_context,
                         uintptr_t stack_top,
                         ModuleCache* module_cache,
                         std::vector<Frame>* stack) const override {
    if (stack_copy_) {
      auto* bottom = reinterpret_cast<uintptr_t*>(
          RegisterContextStackPointer(thread_context));
      auto* top = bottom + stack_size_;
      *stack_copy_ = std::vector<uintptr_t>(bottom, top);
    }
    if (stack_copy_bottom_)
      *stack_copy_bottom_ = RegisterContextStackPointer(thread_context);
    return UnwindResult::COMPLETED;
  }

 private:
  size_t stack_size_;
  std::vector<uintptr_t>* stack_copy_;
  uintptr_t* stack_copy_bottom_;
};

class TestModule : public ModuleCache::Module {
 public:
  TestModule(uintptr_t base_address, size_t size, bool is_native = true)
      : base_address_(base_address), size_(size), is_native_(is_native) {}

  uintptr_t GetBaseAddress() const override { return base_address_; }
  std::string GetId() const override { return ""; }
  FilePath GetDebugBasename() const override { return FilePath(); }
  size_t GetSize() const override { return size_; }
  bool IsNative() const override { return is_native_; }

 private:
  const uintptr_t base_address_;
  const size_t size_;
  const bool is_native_;
};

// Injects a fake module covering the initial instruction pointer value, to
// avoid asking the OS to look it up. Windows doesn't return a consistent error
// code when doing so, and we DCHECK_EQ the expected error code.
void InjectModuleForContextInstructionPointer(
    const std::vector<uintptr_t>& stack,
    ModuleCache* module_cache) {
  module_cache->InjectModuleForTesting(
      std::make_unique<TestModule>(stack[0], sizeof(uintptr_t)));
}

// Returns a plausible instruction pointer value for use in tests that don't
// care about the instruction pointer value in the context, and hence don't need
// InjectModuleForContextInstructionPointer().
uintptr_t GetTestInstructionPointer() {
  return reinterpret_cast<uintptr_t>(&GetTestInstructionPointer);
}

// An unwinder fake that replays the provided outputs.
class FakeTestUnwinder : public Unwinder {
 public:
  struct Result {
    Result(bool can_unwind)
        : can_unwind(can_unwind), result(UnwindResult::UNRECOGNIZED_FRAME) {}

    Result(UnwindResult result, std::vector<uintptr_t> instruction_pointers)
        : can_unwind(true),
          result(result),
          instruction_pointers(instruction_pointers) {}

    bool can_unwind;
    UnwindResult result;
    std::vector<uintptr_t> instruction_pointers;
  };

  // Construct the unwinder with the outputs. The relevant unwinder functions
  // are expected to be invoked at least as many times as the number of values
  // specified in the arrays (except for CanUnwindFrom() which will always
  // return true if provided an empty array.
  explicit FakeTestUnwinder(std::vector<Result> results)
      : results_(std::move(results)) {}

  FakeTestUnwinder(const FakeTestUnwinder&) = delete;
  FakeTestUnwinder& operator=(const FakeTestUnwinder&) = delete;

  bool CanUnwindFrom(const Frame* current_frame) const override {
    bool can_unwind = results_[current_unwind_].can_unwind;
    // NB: If CanUnwindFrom() returns false then TryUnwind() will not be
    // invoked, so current_unwind_ is guarantee to be incremented only once for
    // each result.
    if (!can_unwind)
      ++current_unwind_;
    return can_unwind;
  }

  UnwindResult TryUnwind(RegisterContext* thread_context,
                         uintptr_t stack_top,
                         ModuleCache* module_cache,
                         std::vector<Frame>* stack) const override {
    CHECK_LT(current_unwind_, results_.size());
    const Result& current_result = results_[current_unwind_];
    ++current_unwind_;
    CHECK(current_result.can_unwind);
    for (const auto instruction_pointer : current_result.instruction_pointers)
      stack->emplace_back(
          instruction_pointer,
          module_cache->GetModuleForAddress(instruction_pointer));
    return current_result.result;
  }

 private:
  mutable size_t current_unwind_ = 0;
  std::vector<Result> results_;
};

static constexpr size_t kTestStackBufferSize = sizeof(uintptr_t) * 4;

union alignas(StackSampler::StackBuffer::kPlatformStackAlignment)
    TestStackBuffer {
  uintptr_t as_uintptr[kTestStackBufferSize / sizeof(uintptr_t)];
  uint16_t as_uint16[kTestStackBufferSize / sizeof(uint16_t)];
  uint8_t as_uint8[kTestStackBufferSize / sizeof(uint8_t)];
};

}  // namespace

TEST(StackSamplerImplTest, RewritePointerIfInOriginalStack_InStack) {
  uintptr_t original_stack[4];
  uintptr_t stack_copy[4];
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&stack_copy[2]),
            RewritePointerIfInOriginalStack(
                reinterpret_cast<uint8_t*>(&original_stack[0]),
                &original_stack[0] + base::size(original_stack),
                reinterpret_cast<uint8_t*>(&stack_copy[0]),
                reinterpret_cast<uintptr_t>(&original_stack[2])));
}

TEST(StackSamplerImplTest, RewritePointerIfInOriginalStack_NotInStack) {
  // We use this variable only for its address, which is outside of
  // original_stack.
  uintptr_t non_stack_location;
  uintptr_t original_stack[4];
  uintptr_t stack_copy[4];

  EXPECT_EQ(reinterpret_cast<uintptr_t>(&non_stack_location),
            RewritePointerIfInOriginalStack(
                reinterpret_cast<uint8_t*>(&original_stack[0]),
                &original_stack[0] + size(original_stack),
                reinterpret_cast<uint8_t*>(&stack_copy[0]),
                reinterpret_cast<uintptr_t>(&non_stack_location)));
}

TEST(StackSamplerImplTest, StackCopy) {
  TestStackBuffer original_stack;
  // Fill the stack buffer with increasing uintptr_t values.
  std::iota(&original_stack.as_uintptr[0],
            &original_stack.as_uintptr[0] + size(original_stack.as_uintptr),
            100);
  // Replace the third value with an address within the buffer.
  original_stack.as_uintptr[2] =
      reinterpret_cast<uintptr_t>(&original_stack.as_uintptr[1]);
  TestStackBuffer stack_copy;

  CopyStackContentsAndRewritePointers(
      &original_stack.as_uint8[0],
      &original_stack.as_uintptr[0] + size(original_stack.as_uintptr),
      &stack_copy.as_uintptr[0]);

  EXPECT_EQ(original_stack.as_uintptr[0], stack_copy.as_uintptr[0]);
  EXPECT_EQ(original_stack.as_uintptr[1], stack_copy.as_uintptr[1]);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&stack_copy.as_uintptr[1]),
            stack_copy.as_uintptr[2]);
  EXPECT_EQ(original_stack.as_uintptr[3], stack_copy.as_uintptr[3]);
}

TEST(StackSamplerImplTest, StackCopy_NonAlignedStackPointerCopy) {
  TestStackBuffer stack_buffer;

  // Fill the stack buffer with increasing uint16_t values.
  std::iota(&stack_buffer.as_uint16[0],
            &stack_buffer.as_uint16[0] + size(stack_buffer.as_uint16), 100);

  // Set the stack bottom to the unaligned location one uint16_t into the
  // buffer.
  uint8_t* unaligned_stack_bottom =
      reinterpret_cast<uint8_t*>(&stack_buffer.as_uint16[1]);

  // Leave extra space within the stack buffer beyond the end of the stack, but
  // preserve the platform alignment.
  const size_t extra_space = StackSampler::StackBuffer::kPlatformStackAlignment;
  uintptr_t* stack_top =
      &stack_buffer.as_uintptr[size(stack_buffer.as_uintptr) -
                               extra_space / sizeof(uintptr_t)];

  // Initialize the copy to all zeros.
  TestStackBuffer stack_copy_buffer = {{0}};

  const uint8_t* stack_copy_bottom = CopyStackContentsAndRewritePointers(
      unaligned_stack_bottom, stack_top, &stack_copy_buffer.as_uintptr[0]);

  // The stack copy bottom address is expected to be at the same offset into the
  // stack copy buffer as the unaligned stack bottom is from the stack buffer.
  // Since the buffers have the same platform stack alignment this also ensures
  // the alignment of the bottom addresses is the same.
  EXPECT_EQ(unaligned_stack_bottom - &stack_buffer.as_uint8[0],
            stack_copy_bottom - &stack_copy_buffer.as_uint8[0]);

  // The first value in the copy should not be overwritten since the stack
  // starts at the second uint16_t.
  EXPECT_EQ(0u, stack_copy_buffer.as_uint16[0]);

  // The next values up to the extra space should have been copied.
  const size_t max_index =
      size(stack_copy_buffer.as_uint16) - extra_space / sizeof(uint16_t);
  for (size_t i = 1; i < max_index; ++i)
    EXPECT_EQ(i + 100, stack_copy_buffer.as_uint16[i]);

  // None of the values in the empty space should have been copied.
  for (size_t i = max_index; i < size(stack_copy_buffer.as_uint16); ++i)
    EXPECT_EQ(0u, stack_copy_buffer.as_uint16[i]);
}

// Checks that an unaligned within-stack pointer value at the start of the stack
// is not rewritten.
TEST(StackSamplerImplTest,
     StackCopy_NonAlignedStackPointerUnalignedRewriteAtStart) {
  // Initially fill the buffer with 0s.
  TestStackBuffer stack_buffer = {{0}};

  // Set the stack bottom to the unaligned location one uint16_t into the
  // buffer.
  uint8_t* unaligned_stack_bottom =
      reinterpret_cast<uint8_t*>(&stack_buffer.as_uint16[1]);

  // Set the first unaligned pointer-sized value to an address within the stack.
  uintptr_t within_stack_pointer =
      reinterpret_cast<uintptr_t>(&stack_buffer.as_uintptr[2]);
  std::memcpy(unaligned_stack_bottom, &within_stack_pointer,
              sizeof(within_stack_pointer));

  TestStackBuffer stack_copy_buffer = {{0}};

  const uint8_t* stack_copy_bottom = CopyStackContentsAndRewritePointers(
      unaligned_stack_bottom,
      &stack_buffer.as_uintptr[0] + size(stack_buffer.as_uintptr),
      &stack_copy_buffer.as_uintptr[0]);

  uintptr_t copied_within_stack_pointer;
  std::memcpy(&copied_within_stack_pointer, stack_copy_bottom,
              sizeof(copied_within_stack_pointer));

  // The rewriting should only operate on pointer-aligned values so the
  // unaligned value should be copied verbatim.
  EXPECT_EQ(within_stack_pointer, copied_within_stack_pointer);
}

// Checks that an unaligned within-stack pointer after the start of the stack is
// not rewritten.
TEST(StackSamplerImplTest,
     StackCopy_NonAlignedStackPointerUnalignedRewriteAfterStart) {
  // Initially fill the buffer with 0s.
  TestStackBuffer stack_buffer = {{0}};

  // Set the stack bottom to the unaligned location one uint16_t into the
  // buffer.
  uint8_t* unaligned_stack_bottom =
      reinterpret_cast<uint8_t*>(&stack_buffer.as_uint16[1]);

  // Set the second unaligned pointer-sized value to an address within the
  // stack.
  uintptr_t within_stack_pointer =
      reinterpret_cast<uintptr_t>(&stack_buffer.as_uintptr[2]);
  std::memcpy(unaligned_stack_bottom + sizeof(uintptr_t), &within_stack_pointer,
              sizeof(within_stack_pointer));

  TestStackBuffer stack_copy_buffer = {{0}};

  const uint8_t* stack_copy_bottom = CopyStackContentsAndRewritePointers(
      unaligned_stack_bottom,
      &stack_buffer.as_uintptr[0] + size(stack_buffer.as_uintptr),
      &stack_copy_buffer.as_uintptr[0]);

  uintptr_t copied_within_stack_pointer;
  std::memcpy(&copied_within_stack_pointer,
              stack_copy_bottom + sizeof(uintptr_t),
              sizeof(copied_within_stack_pointer));

  // The rewriting should only operate on pointer-aligned values so the
  // unaligned value should be copied verbatim.
  EXPECT_EQ(within_stack_pointer, copied_within_stack_pointer);
}

TEST(StackSamplerImplTest, StackCopy_NonAlignedStackPointerAlignedRewrite) {
  // Initially fill the buffer with 0s.
  TestStackBuffer stack_buffer = {{0}};

  // Set the stack bottom to the unaligned location one uint16_t into the
  // buffer.
  uint8_t* unaligned_stack_bottom =
      reinterpret_cast<uint8_t*>(&stack_buffer.as_uint16[1]);

  // Set the second aligned pointer-sized value to an address within the stack.
  stack_buffer.as_uintptr[1] =
      reinterpret_cast<uintptr_t>(&stack_buffer.as_uintptr[2]);

  TestStackBuffer stack_copy_buffer = {{0}};

  CopyStackContentsAndRewritePointers(
      unaligned_stack_bottom,
      &stack_buffer.as_uintptr[0] + size(stack_buffer.as_uintptr),
      &stack_copy_buffer.as_uintptr[0]);

  // The aligned pointer should have been rewritten to point within the stack
  // copy.
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&stack_copy_buffer.as_uintptr[2]),
            stack_copy_buffer.as_uintptr[1]);
}

TEST(StackSamplerImplTest, CopyStack) {
  ModuleCache module_cache;
  const std::vector<uintptr_t> stack = {0, 1, 2, 3, 4};
  InjectModuleForContextInstructionPointer(stack, &module_cache);
  std::vector<uintptr_t> stack_copy;
  StackSamplerImpl stack_sampler_impl(
      std::make_unique<TestThreadDelegate>(stack),
      std::make_unique<TestUnwinder>(stack.size(), &stack_copy), &module_cache);

  std::unique_ptr<StackSampler::StackBuffer> stack_buffer =
      std::make_unique<StackSampler::StackBuffer>(stack.size() *
                                                  sizeof(uintptr_t));
  TestProfileBuilder profile_builder(&module_cache);
  stack_sampler_impl.RecordStackFrames(stack_buffer.get(), &profile_builder);

  EXPECT_EQ(stack, stack_copy);
}

TEST(StackSamplerImplTest, CopyStackBufferTooSmall) {
  ModuleCache module_cache;
  std::vector<uintptr_t> stack = {0, 1, 2, 3, 4};
  InjectModuleForContextInstructionPointer(stack, &module_cache);
  std::vector<uintptr_t> stack_copy;
  StackSamplerImpl stack_sampler_impl(
      std::make_unique<TestThreadDelegate>(stack),
      std::make_unique<TestUnwinder>(stack.size(), &stack_copy), &module_cache);

  std::unique_ptr<StackSampler::StackBuffer> stack_buffer =
      std::make_unique<StackSampler::StackBuffer>((stack.size() - 1) *
                                                  sizeof(uintptr_t));
  // Make the buffer different than the input stack.
  stack_buffer->buffer()[0] = 100;
  TestProfileBuilder profile_builder(&module_cache);

  stack_sampler_impl.RecordStackFrames(stack_buffer.get(), &profile_builder);

  // Use the buffer not being overwritten as a proxy for the unwind being
  // aborted.
  EXPECT_NE(stack, stack_copy);
}

TEST(StackSamplerImplTest, CopyStackAndRewritePointers) {
  ModuleCache module_cache;
  // Allocate space for the stack, then make its elements point to themselves.
  std::vector<uintptr_t> stack(2);
  stack[0] = reinterpret_cast<uintptr_t>(&stack[0]);
  stack[1] = reinterpret_cast<uintptr_t>(&stack[1]);
  InjectModuleForContextInstructionPointer(stack, &module_cache);
  std::vector<uintptr_t> stack_copy;
  uintptr_t stack_copy_bottom;
  StackSamplerImpl stack_sampler_impl(
      std::make_unique<TestThreadDelegate>(stack),
      std::make_unique<TestUnwinder>(stack.size(), &stack_copy,
                                     &stack_copy_bottom),
      &module_cache);

  std::unique_ptr<StackSampler::StackBuffer> stack_buffer =
      std::make_unique<StackSampler::StackBuffer>(stack.size() *
                                                  sizeof(uintptr_t));
  TestProfileBuilder profile_builder(&module_cache);

  stack_sampler_impl.RecordStackFrames(stack_buffer.get(), &profile_builder);

  EXPECT_THAT(stack_copy, ElementsAre(stack_copy_bottom,
                                      stack_copy_bottom + sizeof(uintptr_t)));
}

TEST(StackSamplerImplTest, RewriteRegisters) {
  ModuleCache module_cache;
  std::vector<uintptr_t> stack = {0, 1, 2};
  InjectModuleForContextInstructionPointer(stack, &module_cache);
  uintptr_t stack_copy_bottom;
  RegisterContext thread_context;
  RegisterContextFramePointer(&thread_context) =
      reinterpret_cast<uintptr_t>(&stack[1]);
  StackSamplerImpl stack_sampler_impl(
      std::make_unique<TestThreadDelegate>(stack, &thread_context),
      std::make_unique<TestUnwinder>(stack.size(), nullptr, &stack_copy_bottom),
      &module_cache);

  std::unique_ptr<StackSampler::StackBuffer> stack_buffer =
      std::make_unique<StackSampler::StackBuffer>(stack.size() *
                                                  sizeof(uintptr_t));
  TestProfileBuilder profile_builder(&module_cache);
  stack_sampler_impl.RecordStackFrames(stack_buffer.get(), &profile_builder);

  EXPECT_EQ(stack_copy_bottom + sizeof(uintptr_t),
            RegisterContextFramePointer(&thread_context));
}

TEST(StackSamplerImplTest, WalkStack_Completed) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) =
      GetTestInstructionPointer();
  module_cache.InjectModuleForTesting(std::make_unique<TestModule>(1u, 1u));
  FakeTestUnwinder native_unwinder({{UnwindResult::COMPLETED, {1u}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, &native_unwinder, nullptr);

  ASSERT_EQ(2u, stack.size());
  EXPECT_EQ(1u, stack[1].instruction_pointer);
}

TEST(StackSamplerImplTest, WalkStack_Aborted) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) =
      GetTestInstructionPointer();
  module_cache.InjectModuleForTesting(std::make_unique<TestModule>(1u, 1u));
  FakeTestUnwinder native_unwinder({{UnwindResult::ABORTED, {1u}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, &native_unwinder, nullptr);

  ASSERT_EQ(2u, stack.size());
  EXPECT_EQ(1u, stack[1].instruction_pointer);
}

TEST(StackSamplerImplTest, WalkStack_NotUnwound) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) =
      GetTestInstructionPointer();
  FakeTestUnwinder native_unwinder({{UnwindResult::UNRECOGNIZED_FRAME, {}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, &native_unwinder, nullptr);

  ASSERT_EQ(1u, stack.size());
}

TEST(StackSamplerImplTest, WalkStack_AuxUnwind) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) =
      GetTestInstructionPointer();

  // Treat the context instruction pointer as being in the aux unwinder's
  // non-native module.
  module_cache.AddNonNativeModule(
      std::make_unique<TestModule>(GetTestInstructionPointer(), 1u, false));

  FakeTestUnwinder aux_unwinder({{UnwindResult::ABORTED, {1u}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, nullptr, &aux_unwinder);

  ASSERT_EQ(2u, stack.size());
  EXPECT_EQ(GetTestInstructionPointer(), stack[0].instruction_pointer);
  EXPECT_EQ(1u, stack[1].instruction_pointer);
}

TEST(StackSamplerImplTest, WalkStack_AuxThenNative) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) = 0u;

  // Treat the context instruction pointer as being in the aux unwinder's
  // non-native module.
  module_cache.AddNonNativeModule(std::make_unique<TestModule>(0u, 1u, false));
  // Inject a fake native module for the second frame.
  module_cache.InjectModuleForTesting(std::make_unique<TestModule>(1u, 1u));

  FakeTestUnwinder aux_unwinder(
      {{{UnwindResult::UNRECOGNIZED_FRAME, {1u}}, {false}}});
  FakeTestUnwinder native_unwinder({{UnwindResult::COMPLETED, {2u}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, &native_unwinder, &aux_unwinder);

  ASSERT_EQ(3u, stack.size());
  EXPECT_EQ(0u, stack[0].instruction_pointer);
  EXPECT_EQ(1u, stack[1].instruction_pointer);
  EXPECT_EQ(2u, stack[2].instruction_pointer);
}

TEST(StackSamplerImplTest, WalkStack_NativeThenAux) {
  ModuleCache module_cache;
  RegisterContext thread_context;
  RegisterContextInstructionPointer(&thread_context) = 0u;

  // Inject fake native modules for the instruction pointer from the context and
  // the third frame.
  module_cache.InjectModuleForTesting(std::make_unique<TestModule>(0u, 1u));
  module_cache.InjectModuleForTesting(std::make_unique<TestModule>(2u, 1u));
  // Treat the second frame's pointer as being in the aux unwinder's non-native
  // module.
  module_cache.AddNonNativeModule(std::make_unique<TestModule>(1u, 1u, false));

  FakeTestUnwinder aux_unwinder(
      {{false}, {UnwindResult::UNRECOGNIZED_FRAME, {2u}}, {false}});
  FakeTestUnwinder native_unwinder({{UnwindResult::UNRECOGNIZED_FRAME, {1u}},
                                    {UnwindResult::COMPLETED, {3u}}});

  std::vector<Frame> stack = StackSamplerImpl::WalkStackForTesting(
      &module_cache, &thread_context, 0u, &native_unwinder, &aux_unwinder);

  ASSERT_EQ(4u, stack.size());
  EXPECT_EQ(0u, stack[0].instruction_pointer);
  EXPECT_EQ(1u, stack[1].instruction_pointer);
  EXPECT_EQ(2u, stack[2].instruction_pointer);
  EXPECT_EQ(3u, stack[3].instruction_pointer);
}

}  // namespace base
