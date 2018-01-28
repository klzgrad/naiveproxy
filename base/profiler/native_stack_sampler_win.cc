// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/native_stack_sampler.h"

#include <objbase.h>
#include <windows.h>
#include <stddef.h>
#include <winternl.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/profiler/win32_stack_frame_unwinder.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/win/pe_image.h"
#include "base/win/scoped_handle.h"

namespace base {

// Stack recording functions --------------------------------------------------

namespace {

// The thread environment block internal type.
struct TEB {
  NT_TIB Tib;
  // Rest of struct is ignored.
};

// Returns the thread environment block pointer for |thread_handle|.
const TEB* GetThreadEnvironmentBlock(HANDLE thread_handle) {
  // Define the internal types we need to invoke NtQueryInformationThread.
  enum THREAD_INFORMATION_CLASS { ThreadBasicInformation };

  struct CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
  };

  struct THREAD_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    TEB* Teb;
    CLIENT_ID ClientId;
    KAFFINITY AffinityMask;
    LONG Priority;
    LONG BasePriority;
  };

  using NtQueryInformationThreadFunction =
      NTSTATUS (WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, PVOID, ULONG,
                         PULONG);

  const NtQueryInformationThreadFunction nt_query_information_thread =
      reinterpret_cast<NtQueryInformationThreadFunction>(
          ::GetProcAddress(::GetModuleHandle(L"ntdll.dll"),
                           "NtQueryInformationThread"));
  if (!nt_query_information_thread)
    return nullptr;

  THREAD_BASIC_INFORMATION basic_info = {0};
  NTSTATUS status =
      nt_query_information_thread(thread_handle, ThreadBasicInformation,
                                  &basic_info, sizeof(THREAD_BASIC_INFORMATION),
                                  nullptr);
  if (status != 0)
    return nullptr;

  return basic_info.Teb;
}

#if defined(_WIN64)
// If the value at |pointer| points to the original stack, rewrite it to point
// to the corresponding location in the copied stack.
void RewritePointerIfInOriginalStack(uintptr_t top, uintptr_t bottom,
                                     void* stack_copy, const void** pointer) {
  const uintptr_t value = reinterpret_cast<uintptr_t>(*pointer);
  if (value >= bottom && value < top) {
    *pointer = reinterpret_cast<const void*>(
        static_cast<unsigned char*>(stack_copy) + (value - bottom));
  }
}
#endif

void CopyMemoryFromStack(void* to, const void* from, size_t length)
    NO_SANITIZE("address") {
#if defined(ADDRESS_SANITIZER)
  // The following loop is an inlined version of memcpy. The code must be
  // inlined to avoid instrumentation when using ASAN (memory sanitizer). The
  // stack profiler is generating false positive when walking the stack.
  for (size_t pos = 0; pos < length; ++pos)
    reinterpret_cast<char*>(to)[pos] = reinterpret_cast<const char*>(from)[pos];
#else
  std::memcpy(to, from, length);
#endif
}

// Rewrites possible pointers to locations within the stack to point to the
// corresponding locations in the copy, and rewrites the non-volatile registers
// in |context| likewise. This is necessary to handle stack frames with dynamic
// stack allocation, where a pointer to the beginning of the dynamic allocation
// area is stored on the stack and/or in a non-volatile register.
//
// Eager rewriting of anything that looks like a pointer to the stack, as done
// in this function, does not adversely affect the stack unwinding. The only
// other values on the stack the unwinding depends on are return addresses,
// which should not point within the stack memory. The rewriting is guaranteed
// to catch all pointers because the stacks are guaranteed by the ABI to be
// sizeof(void*) aligned.
//
// Note: this function must not access memory in the original stack as it may
// have been changed or deallocated by this point. This is why |top| and
// |bottom| are passed as uintptr_t.
void RewritePointersToStackMemory(uintptr_t top, uintptr_t bottom,
                                  CONTEXT* context, void* stack_copy) {
#if defined(_WIN64)
  DWORD64 CONTEXT::* const nonvolatile_registers[] = {
    &CONTEXT::R12,
    &CONTEXT::R13,
    &CONTEXT::R14,
    &CONTEXT::R15,
    &CONTEXT::Rdi,
    &CONTEXT::Rsi,
    &CONTEXT::Rbx,
    &CONTEXT::Rbp,
    &CONTEXT::Rsp
  };

  // Rewrite pointers in the context.
  for (size_t i = 0; i < arraysize(nonvolatile_registers); ++i) {
    DWORD64* const reg = &(context->*nonvolatile_registers[i]);
    RewritePointerIfInOriginalStack(top, bottom, stack_copy,
                                    reinterpret_cast<const void**>(reg));
  }

  // Rewrite pointers on the stack.
  const void** start = reinterpret_cast<const void**>(stack_copy);
  const void** end = reinterpret_cast<const void**>(
      reinterpret_cast<char*>(stack_copy) + (top - bottom));
  for (const void** loc = start; loc < end; ++loc)
    RewritePointerIfInOriginalStack(top, bottom, stack_copy, loc);
#endif
}

// Movable type representing a recorded stack frame.
struct RecordedFrame {
  RecordedFrame() {}

  RecordedFrame(RecordedFrame&& other)
      : instruction_pointer(other.instruction_pointer),
        module(std::move(other.module)) {
  }

  RecordedFrame& operator=(RecordedFrame&& other) {
    instruction_pointer = other.instruction_pointer;
    module = std::move(other.module);
    return *this;
  }

  const void* instruction_pointer;
  ScopedModuleHandle module;

 private:
  DISALLOW_COPY_AND_ASSIGN(RecordedFrame);
};

// Walks the stack represented by |context| from the current frame downwards,
// recording the instruction pointer and associated module for each frame in
// |stack|.
void RecordStack(CONTEXT* context, std::vector<RecordedFrame>* stack) {
#ifdef _WIN64
  DCHECK(stack->empty());

  // Reserve enough memory for most stacks, to avoid repeated
  // allocations. Approximately 99.9% of recorded stacks are 128 frames or
  // fewer.
  stack->reserve(128);

  Win32StackFrameUnwinder frame_unwinder;
  while (context->Rip) {
    const void* instruction_pointer =
        reinterpret_cast<const void*>(context->Rip);
    ScopedModuleHandle module;
    if (!frame_unwinder.TryUnwind(context, &module))
      return;
    RecordedFrame frame;
    frame.instruction_pointer = instruction_pointer;
    frame.module = std::move(module);
    stack->push_back(std::move(frame));
  }
#endif
}

// Gets the unique build ID for a module. Windows build IDs are created by a
// concatenation of a GUID and AGE fields found in the headers of a module. The
// GUID is stored in the first 16 bytes and the AGE is stored in the last 4
// bytes. Returns the empty string if the function fails to get the build ID.
//
// Example:
// dumpbin chrome.exe /headers | find "Format:"
//   ... Format: RSDS, {16B2A428-1DED-442E-9A36-FCE8CBD29726}, 10, ...
//
// The resulting buildID string of this instance of chrome.exe is
// "16B2A4281DED442E9A36FCE8CBD2972610".
//
// Note that the AGE field is encoded in decimal, not hex.
std::string GetBuildIDForModule(HMODULE module_handle) {
  GUID guid;
  DWORD age;
  win::PEImage(module_handle).GetDebugId(&guid, &age);
  const int kGUIDSize = 39;
  std::wstring build_id;
  int result =
      ::StringFromGUID2(guid, WriteInto(&build_id, kGUIDSize), kGUIDSize);
  if (result != kGUIDSize)
    return std::string();
  RemoveChars(build_id, L"{}-", &build_id);
  build_id += StringPrintf(L"%d", age);
  return WideToUTF8(build_id);
}

// ScopedDisablePriorityBoost -------------------------------------------------

// Disables priority boost on a thread for the lifetime of the object.
class ScopedDisablePriorityBoost {
 public:
  ScopedDisablePriorityBoost(HANDLE thread_handle);
  ~ScopedDisablePriorityBoost();

 private:
  HANDLE thread_handle_;
  BOOL got_previous_boost_state_;
  BOOL boost_state_was_disabled_;

  DISALLOW_COPY_AND_ASSIGN(ScopedDisablePriorityBoost);
};

ScopedDisablePriorityBoost::ScopedDisablePriorityBoost(HANDLE thread_handle)
    : thread_handle_(thread_handle),
      got_previous_boost_state_(false),
      boost_state_was_disabled_(false) {
  got_previous_boost_state_ =
      ::GetThreadPriorityBoost(thread_handle_, &boost_state_was_disabled_);
  if (got_previous_boost_state_) {
    // Confusingly, TRUE disables priority boost.
    ::SetThreadPriorityBoost(thread_handle_, TRUE);
  }
}

ScopedDisablePriorityBoost::~ScopedDisablePriorityBoost() {
  if (got_previous_boost_state_)
    ::SetThreadPriorityBoost(thread_handle_, boost_state_was_disabled_);
}

// ScopedSuspendThread --------------------------------------------------------

// Suspends a thread for the lifetime of the object.
class ScopedSuspendThread {
 public:
  ScopedSuspendThread(HANDLE thread_handle);
  ~ScopedSuspendThread();

  bool was_successful() const { return was_successful_; }

 private:
  HANDLE thread_handle_;
  bool was_successful_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSuspendThread);
};

ScopedSuspendThread::ScopedSuspendThread(HANDLE thread_handle)
    : thread_handle_(thread_handle),
      was_successful_(::SuspendThread(thread_handle) !=
                      static_cast<DWORD>(-1)) {}

ScopedSuspendThread::~ScopedSuspendThread() {
  if (!was_successful_)
    return;

  // Disable the priority boost that the thread would otherwise receive on
  // resume. We do this to avoid artificially altering the dynamics of the
  // executing application any more than we already are by suspending and
  // resuming the thread.
  //
  // Note that this can racily disable a priority boost that otherwise would
  // have been given to the thread, if the thread is waiting on other wait
  // conditions at the time of SuspendThread and those conditions are satisfied
  // before priority boost is reenabled. The measured length of this window is
  // ~100us, so this should occur fairly rarely.
  ScopedDisablePriorityBoost disable_priority_boost(thread_handle_);
  bool resume_thread_succeeded =
      ::ResumeThread(thread_handle_) != static_cast<DWORD>(-1);
  CHECK(resume_thread_succeeded) << "ResumeThread failed: " << GetLastError();
}

// Tests whether |stack_pointer| points to a location in the guard page.
//
// IMPORTANT NOTE: This function is invoked while the target thread is
// suspended so it must not do any allocation from the default heap, including
// indirectly via use of DCHECK/CHECK or other logging statements. Otherwise
// this code can deadlock on heap locks in the default heap acquired by the
// target thread before it was suspended.
bool PointsToGuardPage(uintptr_t stack_pointer) {
  MEMORY_BASIC_INFORMATION memory_info;
  SIZE_T result = ::VirtualQuery(reinterpret_cast<LPCVOID>(stack_pointer),
                                 &memory_info,
                                 sizeof(memory_info));
  return result != 0 && (memory_info.Protect & PAGE_GUARD);
}

// Suspends the thread with |thread_handle|, copies its stack and resumes the
// thread, then records the stack frames and associated modules into |stack|.
//
// IMPORTANT NOTE: No allocations from the default heap may occur in the
// ScopedSuspendThread scope, including indirectly via use of DCHECK/CHECK or
// other logging statements. Otherwise this code can deadlock on heap locks in
// the default heap acquired by the target thread before it was suspended.
void SuspendThreadAndRecordStack(
    HANDLE thread_handle,
    const void* base_address,
    void* stack_copy_buffer,
    size_t stack_copy_buffer_size,
    std::vector<RecordedFrame>* stack,
    NativeStackSampler::AnnotateCallback annotator,
    StackSamplingProfiler::Sample* sample,
    NativeStackSamplerTestDelegate* test_delegate) {
  DCHECK(stack->empty());

  CONTEXT thread_context = {0};
  thread_context.ContextFlags = CONTEXT_FULL;
  // The stack bounds are saved to uintptr_ts for use outside
  // ScopedSuspendThread, as the thread's memory is not safe to dereference
  // beyond that point.
  const uintptr_t top = reinterpret_cast<uintptr_t>(base_address);
  uintptr_t bottom = 0u;

  {
    ScopedSuspendThread suspend_thread(thread_handle);

    if (!suspend_thread.was_successful())
      return;

    if (!::GetThreadContext(thread_handle, &thread_context))
      return;
#if defined(_WIN64)
    bottom = thread_context.Rsp;
#else
    bottom = thread_context.Esp;
#endif

    if ((top - bottom) > stack_copy_buffer_size)
      return;

    // Dereferencing a pointer in the guard page in a thread that doesn't own
    // the stack results in a STATUS_GUARD_PAGE_VIOLATION exception and a crash.
    // This occurs very rarely, but reliably over the population.
    if (PointsToGuardPage(bottom))
      return;

    (*annotator)(sample);

    CopyMemoryFromStack(stack_copy_buffer,
                        reinterpret_cast<const void*>(bottom), top - bottom);
  }

  if (test_delegate)
    test_delegate->OnPreStackWalk();

  RewritePointersToStackMemory(top, bottom, &thread_context, stack_copy_buffer);

  RecordStack(&thread_context, stack);
}

// NativeStackSamplerWin ------------------------------------------------------

class NativeStackSamplerWin : public NativeStackSampler {
 public:
  NativeStackSamplerWin(win::ScopedHandle thread_handle,
                        AnnotateCallback annotator,
                        NativeStackSamplerTestDelegate* test_delegate);
  ~NativeStackSamplerWin() override;

  // StackSamplingProfiler::NativeStackSampler:
  void ProfileRecordingStarting(
      std::vector<StackSamplingProfiler::Module>* modules) override;
  void RecordStackSample(StackBuffer* stack_buffer,
                         StackSamplingProfiler::Sample* sample) override;
  void ProfileRecordingStopped(StackBuffer* stack_buffer) override;

 private:
  // Attempts to query the module filename, base address, and id for
  // |module_handle|, and store them in |module|. Returns true if it succeeded.
  static bool GetModuleForHandle(HMODULE module_handle,
                                 StackSamplingProfiler::Module* module);

  // Gets the index for the Module corresponding to |module_handle| in
  // |modules|, adding it if it's not already present. Returns
  // StackSamplingProfiler::Frame::kUnknownModuleIndex if no Module can be
  // determined for |module|.
  size_t GetModuleIndex(HMODULE module_handle,
                        std::vector<StackSamplingProfiler::Module>* modules);

  // Copies the information represented by |stack| into |sample| and |modules|.
  void CopyToSample(const std::vector<RecordedFrame>& stack,
                    StackSamplingProfiler::Sample* sample,
                    std::vector<StackSamplingProfiler::Module>* modules);

  win::ScopedHandle thread_handle_;

  const AnnotateCallback annotator_;

  NativeStackSamplerTestDelegate* const test_delegate_;

  // The stack base address corresponding to |thread_handle_|.
  const void* const thread_stack_base_address_;

  // Weak. Points to the modules associated with the profile being recorded
  // between ProfileRecordingStarting() and ProfileRecordingStopped().
  std::vector<StackSamplingProfiler::Module>* current_modules_;

  // Maps a module handle to the corresponding Module's index within
  // current_modules_.
  std::map<HMODULE, size_t> profile_module_index_;

  DISALLOW_COPY_AND_ASSIGN(NativeStackSamplerWin);
};

NativeStackSamplerWin::NativeStackSamplerWin(
    win::ScopedHandle thread_handle,
    AnnotateCallback annotator,
    NativeStackSamplerTestDelegate* test_delegate)
    : thread_handle_(thread_handle.Take()),
      annotator_(annotator),
      test_delegate_(test_delegate),
      thread_stack_base_address_(
          GetThreadEnvironmentBlock(thread_handle_.Get())->Tib.StackBase) {
  DCHECK(annotator_);
}

NativeStackSamplerWin::~NativeStackSamplerWin() {
}

void NativeStackSamplerWin::ProfileRecordingStarting(
    std::vector<StackSamplingProfiler::Module>* modules) {
  current_modules_ = modules;
  profile_module_index_.clear();
}

void NativeStackSamplerWin::RecordStackSample(
    StackBuffer* stack_buffer,
    StackSamplingProfiler::Sample* sample) {
  DCHECK(stack_buffer);
  DCHECK(current_modules_);

  std::vector<RecordedFrame> stack;
  SuspendThreadAndRecordStack(thread_handle_.Get(), thread_stack_base_address_,
                              stack_buffer->buffer(), stack_buffer->size(),
                              &stack, annotator_, sample, test_delegate_);
  CopyToSample(stack, sample, current_modules_);
}

void NativeStackSamplerWin::ProfileRecordingStopped(StackBuffer* stack_buffer) {
  current_modules_ = nullptr;
}

// static
bool NativeStackSamplerWin::GetModuleForHandle(
    HMODULE module_handle,
    StackSamplingProfiler::Module* module) {
  wchar_t module_name[MAX_PATH];
  DWORD result_length =
      GetModuleFileName(module_handle, module_name, arraysize(module_name));
  if (result_length == 0)
    return false;

  module->filename = base::FilePath(module_name);

  module->base_address = reinterpret_cast<uintptr_t>(module_handle);

  module->id = GetBuildIDForModule(module_handle);
  if (module->id.empty())
    return false;

  return true;
}

size_t NativeStackSamplerWin::GetModuleIndex(
    HMODULE module_handle,
    std::vector<StackSamplingProfiler::Module>* modules) {
  if (!module_handle)
    return StackSamplingProfiler::Frame::kUnknownModuleIndex;

  auto loc = profile_module_index_.find(module_handle);
  if (loc == profile_module_index_.end()) {
    StackSamplingProfiler::Module module;
    if (!GetModuleForHandle(module_handle, &module))
      return StackSamplingProfiler::Frame::kUnknownModuleIndex;
    modules->push_back(module);
    loc = profile_module_index_.insert(std::make_pair(
        module_handle, modules->size() - 1)).first;
  }

  return loc->second;
}

void NativeStackSamplerWin::CopyToSample(
    const std::vector<RecordedFrame>& stack,
    StackSamplingProfiler::Sample* sample,
    std::vector<StackSamplingProfiler::Module>* modules) {
  sample->frames.clear();
  sample->frames.reserve(stack.size());

  for (const RecordedFrame& frame : stack) {
    sample->frames.push_back(StackSamplingProfiler::Frame(
        reinterpret_cast<uintptr_t>(frame.instruction_pointer),
        GetModuleIndex(frame.module.Get(), modules)));
  }
}

}  // namespace

std::unique_ptr<NativeStackSampler> NativeStackSampler::Create(
    PlatformThreadId thread_id,
    AnnotateCallback annotator,
    NativeStackSamplerTestDelegate* test_delegate) {
#if _WIN64
  // Get the thread's handle.
  HANDLE thread_handle = ::OpenThread(
      THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
      FALSE,
      thread_id);

  if (thread_handle) {
    return std::unique_ptr<NativeStackSampler>(new NativeStackSamplerWin(
        win::ScopedHandle(thread_handle), annotator, test_delegate));
  }
#endif
  return std::unique_ptr<NativeStackSampler>();
}

size_t NativeStackSampler::GetStackBufferSize() {
  // The default Win32 reserved stack size is 1 MB and Chrome Windows threads
  // currently always use the default, but this allows for expansion if it
  // occurs. The size beyond the actual stack size consists of unallocated
  // virtual memory pages so carries little cost (just a bit of wasted address
  // space).
  return 2 << 20;  // 2 MiB
}

}  // namespace base
