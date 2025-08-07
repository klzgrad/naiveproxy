/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/kallsyms/lazy_kernel_symbolizer.h"

#include <string>

#include <sys/file.h>
#include <unistd.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "src/kallsyms/kernel_symbol_map.h"

namespace perfetto {
namespace {

const char kKallsymsPath[] = "/proc/kallsyms";
const char kPtrRestrictPath[] = "/proc/sys/kernel/kptr_restrict";
const char kEnvName[] = "ANDROID_FILE__proc_kallsyms";

size_t ParseInheritedAndroidKallsyms(KernelSymbolMap* symbol_map) {
  const char* fd_str = getenv(kEnvName);
  auto inherited_fd = base::CStringToInt32(fd_str ? fd_str : "");
  // Note: this is also the early exit for non-platform builds.
  if (!inherited_fd.has_value()) {
    PERFETTO_DLOG("Failed to parse %s (%s)", kEnvName, fd_str ? fd_str : "N/A");
    return 0;
  }

  // We've inherited a special fd for kallsyms from init, but we might be
  // sharing the underlying open file description with a concurrent process.
  // Even if we use pread() for reading at absolute offsets, the underlying
  // kernel seqfile is stateful and remembers where the last read stopped. In
  // the worst case, two concurrent readers will cause a quadratic slowdown
  // since the kernel reconstructs the seqfile from the beginning whenever two
  // reads are not consequent.
  // The chosen approach is to use provisional file locks to coordinate access.
  // However we cannot use the special fd for locking, since the locks are based
  // on the underlying open file description (in other words, both sharers will
  // think they own the same lock). Therefore we open /proc/kallsyms again
  // purely for locking purposes.
  base::ScopedFile fd_for_lock = base::OpenFile(kKallsymsPath, O_RDONLY);
  if (!fd_for_lock) {
    PERFETTO_PLOG("Failed to open kallsyms for locking.");
    return 0;
  }

  // Blocking lock since the only possible contention is
  // traced_probes<->traced_perf, which will both lock only for the duration of
  // the parse. Worst case, the task watchdog will restart the process.
  //
  // Lock goes away when |fd_for_lock| gets closed at end of scope.
  if (flock(*fd_for_lock, LOCK_EX) != 0) {
    PERFETTO_PLOG("Unexpected error in flock(kallsyms).");
    return 0;
  }

  return symbol_map->Parse(*inherited_fd);
}

// This class takes care of temporarily lowering the kptr_restrict sysctl.
// Otherwise the symbol addresses in /proc/kallsyms will be zeroed out on most
// Linux configurations.
//
// On Android platform builds, this is solved by inheriting a kallsyms fd from
// init, with symbols being visible as that is evaluated at the time of the
// initial open().
//
// On Linux and standalone builds, we rely on this class in combination with
// either:
// - the sysctls (kptr_restrict, perf_event_paranoid) or this process'
//   capabilitied to be sufficient for addresses to be visible.
// - this process to be running as root / CAP_SYS_ADMIN, in which case this
//   class will attempt to temporarily override kptr_restrict ourselves.
class ScopedKptrUnrestrict {
 public:
  ScopedKptrUnrestrict();   // Lowers kptr_restrict if necessary.
  ~ScopedKptrUnrestrict();  // Restores the initial kptr_restrict.

 private:
  static void WriteKptrRestrict(const std::string&);

  std::string initial_value_;
};

ScopedKptrUnrestrict::ScopedKptrUnrestrict() {
  if (LazyKernelSymbolizer::CanReadKernelSymbolAddresses()) {
    // Symbols already visible, don't touch anything.
    return;
  }

  bool read_res = base::ReadFile(kPtrRestrictPath, &initial_value_);
  if (!read_res) {
    PERFETTO_PLOG("Failed to read %s", kPtrRestrictPath);
    return;
  }

  // Progressively lower kptr_restrict until we can read kallsyms.
  for (int value = atoi(initial_value_.c_str()); value > 0; --value) {
    WriteKptrRestrict(std::to_string(value));
    if (LazyKernelSymbolizer::CanReadKernelSymbolAddresses())
      return;
  }
}

ScopedKptrUnrestrict::~ScopedKptrUnrestrict() {
  if (initial_value_.empty())
    return;
  WriteKptrRestrict(initial_value_);
}

void ScopedKptrUnrestrict::WriteKptrRestrict(const std::string& value) {
  // Note: kptr_restrict requires O_WRONLY. O_RDWR won't work.
  PERFETTO_DCHECK(!value.empty());
  base::ScopedFile fd = base::OpenFile(kPtrRestrictPath, O_WRONLY);
  auto wsize = write(*fd, value.c_str(), value.size());
  if (wsize <= 0) {
    PERFETTO_PLOG("Failed to set %s to %s", kPtrRestrictPath, value.c_str());
  }
}

}  // namespace

LazyKernelSymbolizer::LazyKernelSymbolizer() = default;
LazyKernelSymbolizer::~LazyKernelSymbolizer() = default;

KernelSymbolMap* LazyKernelSymbolizer::GetOrCreateKernelSymbolMap() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (symbol_map_)
    return symbol_map_.get();

  symbol_map_ = std::make_unique<KernelSymbolMap>();

  // Android platform builds: we have an fd from init.
  size_t num_syms = ParseInheritedAndroidKallsyms(symbol_map_.get());
  if (num_syms) {
    return symbol_map_.get();
  }

  // Otherwise, try reading the file directly, temporarily lowering
  // kptr_restrict if we're running with sufficient privileges.
  ScopedKptrUnrestrict kptr_unrestrict;
  auto fd = base::OpenFile(kKallsymsPath, O_RDONLY);
  symbol_map_->Parse(*fd);
  return symbol_map_.get();
}

void LazyKernelSymbolizer::Destroy() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  symbol_map_.reset();
  base::MaybeReleaseAllocatorMemToOS();  // For Scudo, b/170217718.
}

// static
bool LazyKernelSymbolizer::CanReadKernelSymbolAddresses(
    const char* ksyms_path_for_testing) {
  auto* path = ksyms_path_for_testing ? ksyms_path_for_testing : kKallsymsPath;
  base::ScopedFile fd = base::OpenFile(path, O_RDONLY);
  if (!fd) {
    PERFETTO_PLOG("open(%s) failed", kKallsymsPath);
    return false;
  }
  // Don't just use fscanf() as that might read the whole file (b/36473442).
  char buf[4096];
  auto rsize_signed = base::Read(*fd, buf, sizeof(buf) - 1);
  if (rsize_signed <= 0) {
    PERFETTO_PLOG("read(%s) failed", kKallsymsPath);
    return false;
  }
  size_t rsize = static_cast<size_t>(rsize_signed);
  buf[rsize] = '\0';

  // Iterate over the first page of kallsyms. If we find any non-zero address
  // call it success. If all addresses are 0, pessimistically assume
  // kptr_restrict is still restricted.
  // We cannot look only at the first line because on some devices
  // /proc/kallsyms can look like this (note the zeros in the first two addrs):
  // 0000000000000000 A fixed_percpu_data
  // 0000000000000000 A __per_cpu_start
  // 0000000000001000 A cpu_debug_store
  bool reading_addr = true;
  bool addr_is_zero = true;
  for (size_t i = 0; i < rsize; i++) {
    const char c = buf[i];
    if (reading_addr) {
      const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      if (is_hex) {
        addr_is_zero = addr_is_zero && c == '0';
      } else {
        if (!addr_is_zero)
          return true;
        reading_addr = false;  // Will consume the rest of the line until \n.
      }
    } else if (c == '\n') {
      reading_addr = true;
    }  // if (!reading_addr)
  }  // for char in buf

  return false;
}

}  // namespace perfetto
