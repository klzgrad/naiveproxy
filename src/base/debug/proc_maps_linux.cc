// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "base/debug/proc_maps_linux.h"

#include <fcntl.h>
#include <stddef.h>

#include <unordered_map>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/memory/page_size.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
#include <inttypes.h>
#endif

namespace base::debug {

MappedMemoryRegion::MappedMemoryRegion() = default;
MappedMemoryRegion::MappedMemoryRegion(const MappedMemoryRegion&) = default;
MappedMemoryRegion::MappedMemoryRegion(MappedMemoryRegion&&) noexcept = default;
MappedMemoryRegion& MappedMemoryRegion::operator=(MappedMemoryRegion&) =
    default;
MappedMemoryRegion& MappedMemoryRegion::operator=(
    MappedMemoryRegion&&) noexcept = default;

// Scans |proc_maps| starting from |pos| returning true if the gate VMA was
// found, otherwise returns false.
static bool ContainsGateVMA(std::string* proc_maps, size_t pos) {
#if defined(ARCH_CPU_ARM_FAMILY)
  // The gate VMA on ARM kernels is the interrupt vectors page.
  return proc_maps->find(" [vectors]\n", pos) != std::string::npos;
#elif defined(ARCH_CPU_X86_64)
  // The gate VMA on x86 64-bit kernels is the virtual system call page.
  return proc_maps->find(" [vsyscall]\n", pos) != std::string::npos;
#else
  // Otherwise assume there is no gate VMA in which case we shouldn't
  // get duplicate entires.
  return false;
#endif
}

bool ReadProcMaps(std::string* proc_maps) {
  // seq_file only writes out a page-sized amount on each call. Refer to header
  // file for details.
  const size_t read_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

  base::ScopedFD fd(HANDLE_EINTR(open("/proc/self/maps", O_RDONLY)));
  if (!fd.is_valid()) {
    DPLOG(ERROR) << "Couldn't open /proc/self/maps";
    return false;
  }
  proc_maps->clear();

  while (true) {
    // To avoid a copy, resize |proc_maps| so read() can write directly into it.
    // Compute |buffer| afterwards since resize() may reallocate.
    size_t pos = proc_maps->size();
    proc_maps->resize(pos + read_size);
    void* buffer = &(*proc_maps)[pos];

    ssize_t bytes_read = HANDLE_EINTR(read(fd.get(), buffer, read_size));
    if (bytes_read < 0) {
      DPLOG(ERROR) << "Couldn't read /proc/self/maps";
      proc_maps->clear();
      return false;
    }

    // ... and don't forget to trim off excess bytes.
    proc_maps->resize(pos + static_cast<size_t>(bytes_read));

    if (bytes_read == 0) {
      break;
    }

    // The gate VMA is handled as a special case after seq_file has finished
    // iterating through all entries in the virtual memory table.
    //
    // Unfortunately, if additional entries are added at this point in time
    // seq_file gets confused and the next call to read() will return duplicate
    // entries including the gate VMA again.
    //
    // Avoid this by searching for the gate VMA and breaking early.
    if (ContainsGateVMA(proc_maps, pos)) {
      break;
    }
  }

  return true;
}

bool ParseProcMaps(const std::string& input,
                   std::vector<MappedMemoryRegion>* regions_out) {
  CHECK(regions_out);
  std::vector<MappedMemoryRegion> regions;

  // This isn't async safe nor terribly efficient, but it doesn't need to be at
  // this point in time.
  std::vector<std::string> lines =
      SplitString(input, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  for (size_t i = 0; i < lines.size(); ++i) {
    // Due to splitting on '\n' the last line should be empty.
    if (i == lines.size() - 1) {
      if (!lines[i].empty()) {
        DLOG(WARNING) << "Last line not empty";
        return false;
      }
      break;
    }

    MappedMemoryRegion region;
    const char* line = lines[i].c_str();
    char permissions[5] = {};  // Ensure NUL-terminated string.
    uint8_t dev_major = 0;
    uint8_t dev_minor = 0;
    long inode = 0;
    int path_index = 0;

    // Sample format from man 5 proc:
    //
    // address           perms offset  dev   inode   pathname
    // 08048000-08056000 r-xp 00000000 03:0c 64593   /usr/sbin/gpm
    //
    // The final %n term captures the offset in the input string, which is used
    // to determine the path name. It *does not* increment the return value.
    // Refer to man 3 sscanf for details.
    if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4c %llx %hhx:%hhx %ld %n",
               &region.start, &region.end, permissions, &region.offset,
               &dev_major, &dev_minor, &inode, &path_index) < 7) {
      DPLOG(WARNING) << "sscanf failed for line: " << line;
      return false;
    }

    region.inode = inode;
    region.dev_major = dev_major;
    region.dev_minor = dev_minor;

    region.permissions = 0;

    if (permissions[0] == 'r') {
      region.permissions |= MappedMemoryRegion::READ;
    } else if (permissions[0] != '-') {
      return false;
    }

    if (permissions[1] == 'w') {
      region.permissions |= MappedMemoryRegion::WRITE;
    } else if (permissions[1] != '-') {
      return false;
    }

    if (permissions[2] == 'x') {
      region.permissions |= MappedMemoryRegion::EXECUTE;
    } else if (permissions[2] != '-') {
      return false;
    }

    if (permissions[3] == 'p') {
      region.permissions |= MappedMemoryRegion::PRIVATE;
    } else if (permissions[3] != 's' &&
               permissions[3] != 'S') {  // Shared memory.
      return false;
    }

    // Pushing then assigning saves us a string copy.
    regions.push_back(region);
    regions.back().path.assign(line + path_index);
  }

  regions_out->swap(regions);
  return true;
}

std::optional<SmapsRollup> ParseSmapsRollup(const std::string& buffer) {
  std::vector<std::string> lines =
      SplitString(buffer, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  std::unordered_map<std::string, size_t> tmp;
  for (const auto& line : lines) {
    // This should be more than enough space for any output we get (but we also
    // verify the size below).
    std::string key;
    key.resize(100);
    size_t val;
    if (sscanf(line.c_str(), "%99s %" PRIuS " kB", key.data(), &val) == 2) {
      // sscanf writes a nul-byte at the end of the result, so |strlen| is safe
      // here. |resize| does not count the length of the nul-byte, and we want
      // to trim off the trailing colon at the end, so we use |strlen - 1| here.
      key.resize(strlen(key.c_str()) - 1);
      tmp[key] = val * 1024;
    }
  }

  SmapsRollup smaps_rollup;

  smaps_rollup.rss = tmp["Rss"];
  smaps_rollup.pss = tmp["Pss"];
  smaps_rollup.pss_anon = tmp["Pss_Anon"];
  smaps_rollup.pss_file = tmp["Pss_File"];
  smaps_rollup.pss_shmem = tmp["Pss_Shmem"];
  smaps_rollup.private_dirty = tmp["Private_Dirty"];
  smaps_rollup.swap = tmp["Swap"];
  smaps_rollup.swap_pss = tmp["SwapPss"];

  return smaps_rollup;
}

std::optional<SmapsRollup> ReadAndParseSmapsRollup() {
  const size_t read_size = base::GetPageSize();

  base::ScopedFD fd(HANDLE_EINTR(open("/proc/self/smaps_rollup", O_RDONLY)));
  if (!fd.is_valid()) {
    DPLOG(ERROR) << "Couldn't open /proc/self/smaps_rollup";
    return std::nullopt;
  }

  std::string buffer;
  buffer.resize(read_size);

  ssize_t bytes_read = HANDLE_EINTR(
      read(fd.get(), static_cast<void*>(buffer.data()), read_size));
  if (bytes_read < 0) {
    DPLOG(ERROR) << "Couldn't read /proc/self/smaps_rollup";
    return std::nullopt;
  }

  // We expect to read a few hundred bytes, which should be significantly less
  // the page size.
  DCHECK(static_cast<size_t>(bytes_read) < read_size);

  buffer.resize(static_cast<size_t>(bytes_read));

  return ParseSmapsRollup(buffer);
}

std::optional<SmapsRollup> ParseSmapsRollupForTesting(
    const std::string& smaps_rollup) {
  return ParseSmapsRollup(smaps_rollup);
}

}  // namespace base::debug
