// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and use spans.
#pragma allow_unsafe_buffers
#endif

#include "base/android/orderfile/orderfile_instrumentation.h"

#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "base/android/library_loader/anchor_functions.h"
#include "base/android/orderfile/orderfile_buildflags.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"

#if BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)
#include <sstream>

#include "base/time/time.h"
#include "base/trace_event/memory_dump_manager.h"   // no-presubmit-check
#include "base/trace_event/memory_dump_provider.h"  // no-presubmit-check
#endif  // BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)

#if !BUILDFLAG(SUPPORTS_CODE_ORDERING)
#error Requires code ordering support (arm/arm64/x86/x86_64).
#endif  // !BUILDFLAG(SUPPORTS_CODE_ORDERING)

// Must be applied to all functions within this file.
#define NO_INSTRUMENT_FUNCTION __attribute__((no_instrument_function))

namespace base::android::orderfile {

namespace {
// Constants used for StartDelayedDump().
constexpr int kDelayInSeconds = 30;
constexpr int kInitialDelayInSeconds = kPhases == 1 ? kDelayInSeconds : 5;

// This is defined in content/public/common/content_switches.h, which is not
// accessible in ::base.
constexpr const char kProcessTypeSwitch[] = "type";

// These are large overestimates, which is not an issue, as the data is
// allocated in .bss, and on linux doesn't take any actual memory when it's not
// touched.
constexpr size_t kBitfieldSize = 1 << 22;
constexpr size_t kMaxTextSizeInBytes = kBitfieldSize * (4 * 32);
constexpr size_t kMaxElements = 1 << 20;

// Data required to log reached offsets.
struct LogData {
  std::atomic<uint32_t> offsets[kBitfieldSize];
  std::atomic<size_t> ordered_offsets[kMaxElements];
  std::atomic<size_t> index;
};

LogData g_data[kPhases];
std::atomic<int> g_data_index;

// Number of unexpected addresses, that is addresses that are not within [start,
// end) bounds for the executable code.
//
// This should be exactly 0, since the start and end of .text should be known
// perfectly by the linker, but it does happen. See crbug.com/1186598.
std::atomic<int> g_unexpected_addresses;

#if BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)
// Dump offsets when a memory dump is requested. Used only if
// switches::kDevtoolsInstrumentationDumping is set.
class OrderfileMemoryDumpHook : public base::trace_event::MemoryDumpProvider {
  NO_INSTRUMENT_FUNCTION bool OnMemoryDump(
      const base::trace_event::MemoryDumpArgs& args,
      base::trace_event::ProcessMemoryDump* pmd) override {
    // Disable instrumentation now to cut down on orderfile pollution.
    if (!Disable()) {
      return true;  // A dump has already been started.
    }
    std::stringstream process_type_str;
    Dump(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        kProcessTypeSwitch));
    return true;  // If something goes awry, a fatal error will be created
                  // internally.
  }
};
#endif  // BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)

// |RecordAddress()| adds an element to a concurrent bitset and to a concurrent
// append-only list of offsets.
//
// Ordering:
// Two consecutive calls to |RecordAddress()| from the same thread will be
// ordered in the same way in the result, as written by
// |StopAndDumpToFile()|. The result will contain exactly one instance of each
// unique offset relative to |kStartOfText| passed to |RecordAddress()|.
//
// Implementation:
// The "set" part is implemented with a bitfield, |g_offset|. The insertion
// order is recorded in |g_ordered_offsets|.
// This is not a class to make sure there isn't a static constructor, as it
// would cause issue with an instrumented static constructor calling this code.
//
// Limitations:
// - Only records offsets to addresses between |kStartOfText| and |kEndOfText|.
// - Capacity of the set is limited by |kMaxElements|.
// - Some insertions at the end of collection may be lost.

// Records that |address| has been reached, if recording is enabled.
// To avoid infinite recursion, this *must* *never* call any instrumented
// function, unless |Disable()| is called first.
template <bool for_testing>
__attribute__((always_inline, no_instrument_function)) void RecordAddress(
    size_t address) {
  int index = g_data_index.load(std::memory_order_relaxed);
  if (index >= kPhases) {
    return;
  }

  const size_t start =
      for_testing ? kStartOfTextForTesting : base::android::kStartOfText;
  const size_t end =
      for_testing ? kEndOfTextForTesting : base::android::kEndOfText;
  if (address < start || address > end) [[unlikely]] {
    if (!AreAnchorsSane()) {
      // Something is really wrong with the anchors, and this is likely to be
      // triggered from within a static constructor, where logging is likely to
      // deadlock.  By crashing immediately we at least have a chance to get a
      // stack trace from the system to give some clue about the nature of the
      // problem.
      ImmediateCrash();
    }

    // Observing return addresses outside of the intended range indicates a
    // potentially serious problem in the way the build is set up. However, a
    // small number of unexpected addresses is tolerable for production builds.
    // It seems useful to allow a limited number of out-of-range addresses to
    // let the orderfile_generator guess the root causes. See
    // crbug.com/330761384, crbug.com/352317042.
    if (g_unexpected_addresses.fetch_add(1, std::memory_order_relaxed) < 10) {
      return;
    }

    Disable();
    LOG(FATAL) << "Too many unexpected addresses! start = " << std::hex << start
               << " end = " << end << " address = " << address;
  }

  size_t offset = address - start;
  static_assert(sizeof(int) == 4,
                "Collection and processing code assumes that sizeof(int) == 4");
  size_t offset_index = offset / 4;

  auto* offsets = g_data[index].offsets;
  // Atomically set the corresponding bit in the array.
  std::atomic<uint32_t>* element = offsets + (offset_index / 32);
  // First, a racy check. This saves a CAS if the bit is already set, and
  // allows the cache line to remain shared acoss CPUs in this case.
  uint32_t value = element->load(std::memory_order_relaxed);
  uint32_t mask = 1 << (offset_index % 32);
  if (value & mask) {
    return;
  }

  auto before = element->fetch_or(mask, std::memory_order_relaxed);
  if (before & mask) {
    return;
  }

  // We were the first one to set the element, record it in the ordered
  // elements list.
  // Use relaxed ordering, as the value is not published, or used for
  // synchronization.
  auto* ordered_offsets = g_data[index].ordered_offsets;
  auto& ordered_offsets_index = g_data[index].index;
  size_t insertion_index =
      ordered_offsets_index.fetch_add(1, std::memory_order_relaxed);
  if (insertion_index >= kMaxElements) [[unlikely]] {
    Disable();
    LOG(FATAL) << "Too many reached offsets";
  }
  ordered_offsets[insertion_index].store(offset, std::memory_order_relaxed);
}

NO_INSTRUMENT_FUNCTION bool DumpToFile(const base::FilePath& path,
                                       const LogData& data) {
  auto file =
      base::File(path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    PLOG(ERROR) << "Could not open " << path;
    return false;
  }

  if (data.index == 0) {
    LOG(ERROR) << "No entries to dump";
    return false;
  }

  size_t count = data.index - 1;
  for (size_t i = 0; i < count; i++) {
    // |g_ordered_offsets| is initialized to 0, so a 0 in the middle of it
    // indicates a case where the index was incremented, but the write is not
    // visible in this thread yet. Safe to skip, also because the function at
    // the start of text is never called.
    auto offset = data.ordered_offsets[i].load(std::memory_order_relaxed);
    if (!offset) {
      continue;
    }
    auto offset_str = base::StringPrintf("%" PRIuS "\n", offset);
    if (!file.WriteAtCurrentPosAndCheck(base::as_byte_span(offset_str))) {
      // If the file could be opened, but writing has failed, it's likely that
      // data was partially written. Producing incomplete profiling data would
      // lead to a poorly performing orderfile, but might not be otherwised
      // noticed. So we crash instead.
      LOG(FATAL) << "Error writing profile data";
    }
  }
  return true;
}

// Stops recording, and outputs the data to |path|.
NO_INSTRUMENT_FUNCTION void StopAndDumpToFile(int pid,
                                              uint64_t start_ns_since_epoch,
                                              const std::string& tag) {
  Disable();

  for (int phase = 0; phase < kPhases; phase++) {
    std::string tag_str;
    if (!tag.empty()) {
      tag_str = base::StringPrintf("%s-", tag.c_str());
    }
    auto path = base::StringPrintf(
        "/data/local/tmp/chrome/orderfile/profile-hitmap-%s%d-%" PRIu64
        ".txt_%d",
        tag_str.c_str(), pid, start_ns_since_epoch, phase);
    if (!DumpToFile(base::FilePath(path), g_data[phase])) {
      LOG(ERROR) << "Problem with dump " << phase << " (" << tag << ")";
    }
  }

  int unexpected_addresses =
      g_unexpected_addresses.load(std::memory_order_relaxed);
  if (unexpected_addresses != 0) {
    LOG(WARNING) << "Got " << unexpected_addresses << " unexpected addresses!";
  }
}

}  // namespace

// After a call to Disable(), any function can be called, as reentrancy into the
// instrumentation function will be mitigated.
NO_INSTRUMENT_FUNCTION bool Disable() {
  auto old_phase = g_data_index.exchange(kPhases, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  return old_phase != kPhases;
}

NO_INSTRUMENT_FUNCTION void SanityChecks() {
  CHECK_LT(base::android::kEndOfText - base::android::kStartOfText,
           kMaxTextSizeInBytes);
  CHECK(base::android::IsOrderingSane());
}

NO_INSTRUMENT_FUNCTION bool SwitchToNextPhaseOrDump(
    int pid,
    uint64_t start_ns_since_epoch,
    const std::string& tag) {
  int before = g_data_index.fetch_add(1, std::memory_order_relaxed);
  if (before + 1 == kPhases) {
    StopAndDumpToFile(pid, start_ns_since_epoch, tag);
    return true;
  }
  return false;
}

NO_INSTRUMENT_FUNCTION void StartDelayedDump() {
  // Using std::thread and not using base::TimeTicks() in order to to not call
  // too many base:: symbols that would pollute the reached symbol dumps.
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
    PLOG(FATAL) << "clock_gettime.";
  }
  uint64_t start_ns_since_epoch =
      static_cast<uint64_t>(ts.tv_sec) * 1000 * 1000 * 1000 + ts.tv_nsec;
  int pid = getpid();
  std::string tag = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      kProcessTypeSwitch);

#if BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)
  static auto* g_orderfile_memory_dump_hook = new OrderfileMemoryDumpHook();
  base::trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(
      g_orderfile_memory_dump_hook, "Orderfile", nullptr);
#endif  // BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)

  std::thread([pid, start_ns_since_epoch, tag] {
    sleep(kInitialDelayInSeconds);
#if BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)
    SwitchToNextPhaseOrDump(pid, start_ns_since_epoch, tag);
// Return, letting devtools tracing handle any post-startup phases.
#else
    while (!SwitchToNextPhaseOrDump(pid, start_ns_since_epoch, tag))
      sleep(kDelayInSeconds);
#endif  // BUILDFLAG(DEVTOOLS_INSTRUMENTATION_DUMPING)
  }).detach();
}

NO_INSTRUMENT_FUNCTION void Dump(const std::string& tag) {
  // As profiling has been disabled, none of the uses of ::base symbols below
  // will enter the symbol dump.
  StopAndDumpToFile(
      getpid(), (base::Time::Now() - base::Time::UnixEpoch()).InNanoseconds(),
      tag);
}

NO_INSTRUMENT_FUNCTION void ResetForTesting() {
  Disable();
  g_data_index = 0;
  for (int i = 0; i < kPhases; i++) {
    memset(reinterpret_cast<uint32_t*>(g_data[i].offsets), 0,
           sizeof(uint32_t) * kBitfieldSize);
    memset(reinterpret_cast<uint32_t*>(g_data[i].ordered_offsets), 0,
           sizeof(uint32_t) * kMaxElements);
    g_data[i].index.store(0);
  }

  g_unexpected_addresses.store(0, std::memory_order_relaxed);
}

NO_INSTRUMENT_FUNCTION void RecordAddressForTesting(size_t address) {
  return RecordAddress<true>(address);
}

NO_INSTRUMENT_FUNCTION std::vector<size_t> GetOrderedOffsetsForTesting() {
  std::vector<size_t> result;
  size_t max_index = g_data[0].index.load(std::memory_order_relaxed);
  for (size_t i = 0; i < max_index; ++i) {
    auto value = g_data[0].ordered_offsets[i].load(std::memory_order_relaxed);
    if (value) {
      result.push_back(value);
    }
  }
  return result;
}

}  // namespace base::android::orderfile

extern "C" {

NO_INSTRUMENT_FUNCTION void __cyg_profile_func_enter_bare() {
  base::android::orderfile::RecordAddress<false>(
      reinterpret_cast<size_t>(__builtin_return_address(0)));
}

}  // extern "C"
