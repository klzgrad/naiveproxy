// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/chrome_unwinder_android.h"

#include "base/android/library_loader/anchor_functions.h"
#include "base/debug/elf_reader.h"
#include "base/debug/proc_maps_linux.h"
#include "base/no_destructor.h"
#include "base/numerics/checked_math.h"
#include "base/profiler/native_unwinder.h"
#include "base/profiler/profile_builder.h"
#include "base/sampling_heap_profiler/module_cache.h"
#include "build/build_config.h"

extern "C" {
// The address of |__executable_start| gives the start address of the
// executable or shared library. This value is used to find the offset address
// of the instruction in binary from PC.
extern char __executable_start;
}

namespace base {

namespace {

const std::string& GetChromeModuleId() {
  static const base::NoDestructor<std::string> build_id([] {
#if defined(OFFICIAL_BUILD)
    base::debug::ElfBuildIdBuffer build_id;
    size_t build_id_length =
        base::debug::ReadElfBuildId(&__executable_start, true, build_id);
    DCHECK_GT(build_id_length, 0u);
    // Append 0 for the age value.
    return std::string(build_id, build_id_length) + "0";
#else
    // Local chromium builds don't have an ELF build-id note. A synthetic
    // build id is provided. https://crbug.com/870919
    return std::string("CCCCCCCCDB511330464892F0B600B4D60");
#endif
  }());
  return *build_id;
}

StringPiece GetChromeLibraryName() {
  static const StringPiece library_name([] {
    Optional<StringPiece> library_name =
        base::debug::ReadElfLibraryName(&__executable_start);
    DCHECK(library_name);
    return *library_name;
  }());
  return library_name;
}

class ChromeModule : public ModuleCache::Module {
 public:
  ChromeModule() : build_id_(GetChromeModuleId()) {}
  ~ChromeModule() override = default;

  uintptr_t GetBaseAddress() const override {
    return reinterpret_cast<uintptr_t>(&__executable_start);
  }

  std::string GetId() const override { return build_id_; }

  FilePath GetDebugBasename() const override {
    return FilePath(GetChromeLibraryName());
  }

  // Gets the size of the module.
  size_t GetSize() const override {
    return base::android::kEndOfText - GetBaseAddress();
  }

  // True if this is a native module.
  bool IsNative() const override { return false; }

  std::string build_id_;
};

}  // namespace

ChromeUnwinderAndroid::ChromeUnwinderAndroid(const ArmCFITable* cfi_table)
    : cfi_table_(cfi_table) {
  DCHECK(cfi_table_);
}

ChromeUnwinderAndroid::~ChromeUnwinderAndroid() = default;

void ChromeUnwinderAndroid::AddNonNativeModules(ModuleCache* module_cache) {
  auto chrome_module = std::make_unique<ChromeModule>();
  chrome_module_id_ = chrome_module->GetId();
  module_cache->AddNonNativeModule(std::move(chrome_module));
}

bool ChromeUnwinderAndroid::CanUnwindFrom(const Frame* current_frame) const {
  // AddNonNativeModules() should be called first.
  DCHECK(!chrome_module_id_.empty());
  return current_frame->module &&
         current_frame->module->GetId() == chrome_module_id_;
}

UnwindResult ChromeUnwinderAndroid::TryUnwind(RegisterContext* thread_context,
                                              uintptr_t stack_top,
                                              ModuleCache* module_cache,
                                              std::vector<Frame>* stack) const {
  DCHECK(CanUnwindFrom(&stack->back()));
  do {
    const ModuleCache::Module* module = stack->back().module;

    uintptr_t pc = RegisterContextInstructionPointer(thread_context);
    DCHECK_GE(pc, module->GetBaseAddress());
    uintptr_t func_addr = pc - module->GetBaseAddress();

    auto entry = cfi_table_->FindEntryForAddress(func_addr);
    if (!entry)
      return UnwindResult::ABORTED;
    if (!Step(thread_context, stack_top, *entry))
      return UnwindResult::ABORTED;
    stack->emplace_back(RegisterContextInstructionPointer(thread_context),
                        module_cache->GetModuleForAddress(
                            RegisterContextInstructionPointer(thread_context)));
  } while (CanUnwindFrom(&stack->back()));
  return UnwindResult::UNRECOGNIZED_FRAME;
}

void ChromeUnwinderAndroid::SetExpectedChromeModuleIdForTesting(
    const std::string& chrome_module_id) {
  chrome_module_id_ = chrome_module_id;
}

// static
bool ChromeUnwinderAndroid::Step(RegisterContext* thread_context,
                                 uintptr_t stack_top,
                                 const ArmCFITable::FrameEntry& entry) {
  CHECK_NE(RegisterContextStackPointer(thread_context), 0U);
  CHECK_LE(RegisterContextStackPointer(thread_context), stack_top);
  if (entry.cfa_offset == 0) {
    uintptr_t pc = RegisterContextInstructionPointer(thread_context);
    uintptr_t return_address = static_cast<uintptr_t>(thread_context->arm_lr);

    if (pc == return_address)
      return false;

    RegisterContextInstructionPointer(thread_context) = return_address;
  } else {
    // The rules for unwinding using the CFI information are:
    // SP_prev = SP_cur + cfa_offset and
    // PC_prev = * (SP_prev - ra_offset).
    auto new_sp =
        CheckedNumeric<uintptr_t>(RegisterContextStackPointer(thread_context)) +
        CheckedNumeric<uint16_t>(entry.cfa_offset);
    if (!new_sp.AssignIfValid(&RegisterContextStackPointer(thread_context)) ||
        RegisterContextStackPointer(thread_context) >= stack_top) {
      return false;
    }

    if (entry.ra_offset > entry.cfa_offset)
      return false;

    // Underflow is prevented because |ra_offset| <= |cfa_offset|.
    uintptr_t ip_address = (new_sp - CheckedNumeric<uint16_t>(entry.ra_offset))
                               .ValueOrDie<uintptr_t>();
    RegisterContextInstructionPointer(thread_context) =
        *reinterpret_cast<uintptr_t*>(ip_address);
  }
  return true;
}

}  // namespace base
