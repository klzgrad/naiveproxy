// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/memory.h"

#include <stddef.h>

#include <new>

#include "base/allocator/allocator_shim.h"
#include "base/allocator/features.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/process/internal_linux.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"

#if defined(USE_TCMALLOC)
#include "third_party/tcmalloc/chromium/src/gperftools/tcmalloc.h"
#endif

namespace base {

size_t g_oom_size = 0U;

namespace {

void OnNoMemorySize(size_t size) {
  g_oom_size = size;

  if (size != 0)
    LOG(FATAL) << "Out of memory, size = " << size;
  LOG(FATAL) << "Out of memory.";
}

void OnNoMemory() {
  OnNoMemorySize(0);
}

}  // namespace

void EnableTerminationOnHeapCorruption() {
  // On Linux, there nothing to do AFAIK.
}

void EnableTerminationOnOutOfMemory() {
  // Set the new-out of memory handler.
  std::set_new_handler(&OnNoMemory);
  // If we're using glibc's allocator, the above functions will override
  // malloc and friends and make them die on out of memory.

#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  allocator::SetCallNewHandlerOnMallocFailure(true);
#elif defined(USE_TCMALLOC)
  // For tcmalloc, we need to tell it to behave like new.
  tc_set_new_mode(1);
#endif
}

// NOTE: This is not the only version of this function in the source:
// the setuid sandbox (in process_util_linux.c, in the sandbox source)
// also has its own C version.
bool AdjustOOMScore(ProcessId process, int score) {
  if (score < 0 || score > kMaxOomScore)
    return false;

  FilePath oom_path(internal::GetProcPidDir(process));

  // Attempt to write the newer oom_score_adj file first.
  FilePath oom_file = oom_path.AppendASCII("oom_score_adj");
  if (PathExists(oom_file)) {
    std::string score_str = IntToString(score);
    DVLOG(1) << "Adjusting oom_score_adj of " << process << " to "
             << score_str;
    int score_len = static_cast<int>(score_str.length());
    return (score_len == WriteFile(oom_file, score_str.c_str(), score_len));
  }

  // If the oom_score_adj file doesn't exist, then we write the old
  // style file and translate the oom_adj score to the range 0-15.
  oom_file = oom_path.AppendASCII("oom_adj");
  if (PathExists(oom_file)) {
    // Max score for the old oom_adj range.  Used for conversion of new
    // values to old values.
    const int kMaxOldOomScore = 15;

    int converted_score = score * kMaxOldOomScore / kMaxOomScore;
    std::string score_str = IntToString(converted_score);
    DVLOG(1) << "Adjusting oom_adj of " << process << " to " << score_str;
    int score_len = static_cast<int>(score_str.length());
    return (score_len == WriteFile(oom_file, score_str.c_str(), score_len));
  }

  return false;
}

bool UncheckedMalloc(size_t size, void** result) {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  *result = allocator::UncheckedAlloc(size);
#elif defined(MEMORY_TOOL_REPLACES_ALLOCATOR) || \
    (!defined(LIBC_GLIBC) && !defined(USE_TCMALLOC))
  *result = malloc(size);
#elif defined(LIBC_GLIBC) && !defined(USE_TCMALLOC)
  *result = __libc_malloc(size);
#elif defined(USE_TCMALLOC)
  *result = tc_malloc_skip_new_handler(size);
#endif
  return *result != nullptr;
}

}  // namespace base
