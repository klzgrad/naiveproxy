/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/profiling/common/unwind_support.h"

#include <cinttypes>

#include <procinfo/process_map.h>
#include <unwindstack/Maps.h>
#include <unwindstack/Memory.h>

#include "perfetto/ext/base/file_utils.h"

namespace perfetto {
namespace profiling {

StackOverlayMemory::StackOverlayMemory(std::shared_ptr<unwindstack::Memory> mem,
                                       uint64_t sp,
                                       const uint8_t* stack,
                                       size_t size)
    : mem_(std::move(mem)), sp_(sp), stack_end_(sp + size), stack_(stack) {}

size_t StackOverlayMemory::Read(uint64_t addr, void* dst, size_t size) {
  if (addr >= sp_ && addr + size <= stack_end_ && addr + size > sp_) {
    size_t offset = static_cast<size_t>(addr - sp_);
    memcpy(dst, stack_ + offset, size);
    return size;
  }

  return mem_->Read(addr, dst, size);
}

FDMemory::FDMemory(base::ScopedFile mem_fd) : mem_fd_(std::move(mem_fd)) {}

size_t FDMemory::Read(uint64_t addr, void* dst, size_t size) {
  ssize_t rd = pread64(*mem_fd_, dst, size, static_cast<off64_t>(addr));
  if (PERFETTO_UNLIKELY(rd == -1)) {
    PERFETTO_PLOG("Failed remote pread of %zu bytes at address %" PRIx64, size,
                  addr);
    return 0;
  }
  return static_cast<size_t>(rd);
}

FDMaps::FDMaps(base::ScopedFile fd) : fd_(std::move(fd)) {}

bool FDMaps::Parse() {
  // If the process has already exited, lseek or ReadFileDescriptor will
  // return false.
  if (lseek(*fd_, 0, SEEK_SET) == -1)
    return false;

  std::string content;
  if (!base::ReadFileDescriptor(*fd_, &content))
    return false;

  unwindstack::SharedString name("");
  std::shared_ptr<unwindstack::MapInfo> prev_map;
  return android::procinfo::ReadMapFileContent(
      &content[0], [&](const android::procinfo::MapInfo& mapinfo) {
        // Mark a device map in /dev/ and not in /dev/ashmem/ specially.
        auto flags = mapinfo.flags;
        if (strncmp(mapinfo.name.c_str(), "/dev/", 5) == 0 &&
            strncmp(mapinfo.name.c_str() + 5, "ashmem/", 7) != 0) {
          flags |= unwindstack::MAPS_FLAGS_DEVICE_MAP;
        }
        // Share the string if it matches for consecutive maps.
        if (name != mapinfo.name) {
          name = unwindstack::SharedString(mapinfo.name);
        }
        maps_.emplace_back(unwindstack::MapInfo::Create(
            prev_map, mapinfo.start, mapinfo.end, mapinfo.pgoff, flags, name));
        prev_map = maps_.back();
      });
}

void FDMaps::Reset() {
  maps_.clear();
}

UnwindingMetadata::UnwindingMetadata(base::ScopedFile maps_fd,
                                     base::ScopedFile mem_fd)
    : fd_maps(std::move(maps_fd)),
      fd_mem(std::make_shared<FDMemory>(std::move(mem_fd))) {
  if (!fd_maps.Parse())
    PERFETTO_DLOG("Failed initial maps parse");
}

void UnwindingMetadata::ReparseMaps() {
  reparses++;
  fd_maps.Reset();
  fd_maps.Parse();
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
  jit_debug.reset();
  dex_files.reset();
#endif
}

#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
unwindstack::JitDebug* UnwindingMetadata::GetJitDebug(
    unwindstack::ArchEnum arch) {
  if (jit_debug.get() == nullptr) {
    std::vector<std::string> search_libs{"libart.so", "libartd.so"};
    jit_debug = unwindstack::CreateJitDebug(arch, fd_mem, search_libs);
  }
  return jit_debug.get();
}

unwindstack::DexFiles* UnwindingMetadata::GetDexFiles(
    unwindstack::ArchEnum arch) {
  if (dex_files.get() == nullptr) {
    std::vector<std::string> search_libs{"libart.so", "libartd.so"};
    dex_files = unwindstack::CreateDexFiles(arch, fd_mem, search_libs);
  }
  return dex_files.get();
}
#endif

const std::string& UnwindingMetadata::GetBuildId(
    const unwindstack::FrameData& frame) {
  if (frame.map_info != nullptr && !frame.map_info->name().empty()) {
    return frame.map_info->GetBuildID();
  }

  return empty_string_;
}

std::string StringifyLibUnwindstackError(unwindstack::ErrorCode e) {
  switch (e) {
    case unwindstack::ERROR_NONE:
      return "NONE";
    case unwindstack::ERROR_MEMORY_INVALID:
      return "MEMORY_INVALID";
    case unwindstack::ERROR_UNWIND_INFO:
      return "UNWIND_INFO";
    case unwindstack::ERROR_UNSUPPORTED:
      return "UNSUPPORTED";
    case unwindstack::ERROR_INVALID_MAP:
      return "INVALID_MAP";
    case unwindstack::ERROR_MAX_FRAMES_EXCEEDED:
      return "MAX_FRAME_EXCEEDED";
    case unwindstack::ERROR_REPEATED_FRAME:
      return "REPEATED_FRAME";
    case unwindstack::ERROR_INVALID_ELF:
      return "INVALID_ELF";
    case unwindstack::ERROR_SYSTEM_CALL:
      return "SYSTEM_CALL";
    case unwindstack::ERROR_THREAD_DOES_NOT_EXIST:
      return "THREAD_DOES_NOT_EXIST";
    case unwindstack::ERROR_THREAD_TIMEOUT:
      return "THREAD_TIMEOUT";
    case unwindstack::ERROR_BAD_ARCH:
      return "BAD_ARCH";
    case unwindstack::ERROR_MAPS_PARSE:
      return "MAPS_PARSE";
    case unwindstack::ERROR_INVALID_PARAMETER:
      return "INVALID_PARAMETER";
    case unwindstack::ERROR_PTRACE_CALL:
      return "PTRACE_CALL";
  }
}

}  // namespace profiling
}  // namespace perfetto
