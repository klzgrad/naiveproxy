/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/tracing/ipc/shared_memory_windows.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <memory>
#include <random>

#include <windows.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {

// static
std::unique_ptr<SharedMemoryWindows> SharedMemoryWindows::Create(size_t size,
                                                                 Flags flags) {
  base::ScopedPlatformHandle shmem_handle;
  std::random_device rnd_dev;
  uint64_t rnd_key = (static_cast<uint64_t>(rnd_dev()) << 32) | rnd_dev();
  std::string key = "perfetto_shm_" + base::Uint64ToHexStringNoPrefix(rnd_key);

  SECURITY_ATTRIBUTES security_attributes = {};
  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  if (flags & Flags::kInheritableHandles)
    security_attributes.bInheritHandle = TRUE;

  shmem_handle.reset(CreateFileMappingA(
      INVALID_HANDLE_VALUE,  // Use paging file.
      &security_attributes, PAGE_READWRITE,
      static_cast<DWORD>(size >> 32),  // maximum object size (high-order DWORD)
      static_cast<DWORD>(size),        // maximum object size (low-order DWORD)
      key.c_str()));

  if (!shmem_handle) {
    PERFETTO_PLOG("CreateFileMapping() call failed");
    return nullptr;
  }
  void* start =
      MapViewOfFile(*shmem_handle, FILE_MAP_ALL_ACCESS, /*offsetHigh=*/0,
                    /*offsetLow=*/0, size);
  if (!start) {
    PERFETTO_PLOG("MapViewOfFile() failed");
    return nullptr;
  }

  return std::unique_ptr<SharedMemoryWindows>(new SharedMemoryWindows(
      start, size, std::move(key), std::move(shmem_handle)));
}

// static
std::unique_ptr<SharedMemoryWindows> SharedMemoryWindows::Attach(
    const std::string& key) {
  base::ScopedPlatformHandle shmem_handle;
  shmem_handle.reset(
      OpenFileMappingA(FILE_MAP_ALL_ACCESS, /*inherit=*/false, key.c_str()));
  if (!shmem_handle) {
    PERFETTO_PLOG("Failed to OpenFileMapping()");
    return nullptr;
  }

  void* start =
      MapViewOfFile(*shmem_handle, FILE_MAP_ALL_ACCESS, /*offsetHigh=*/0,
                    /*offsetLow=*/0, /*dwNumberOfBytesToMap=*/0);
  if (!start) {
    PERFETTO_PLOG("MapViewOfFile() failed");
    return nullptr;
  }

  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(start, &info, sizeof(info))) {
    PERFETTO_PLOG("VirtualQuery() failed");
    return nullptr;
  }
  size_t size = info.RegionSize;
  return std::unique_ptr<SharedMemoryWindows>(
      new SharedMemoryWindows(start, size, key, std::move(shmem_handle)));
}

// static
std::unique_ptr<SharedMemoryWindows> SharedMemoryWindows::AttachToHandleWithKey(
    base::ScopedPlatformHandle shmem_handle,
    const std::string& key) {
  void* start =
      MapViewOfFile(*shmem_handle, FILE_MAP_ALL_ACCESS, /*offsetHigh=*/0,
                    /*offsetLow=*/0, /*dwNumberOfBytesToMap=*/0);
  if (!start) {
    PERFETTO_PLOG("MapViewOfFile() failed");
    return nullptr;
  }

  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(start, &info, sizeof(info))) {
    PERFETTO_PLOG("VirtualQuery() failed");
    return nullptr;
  }
  size_t size = info.RegionSize;

  return std::unique_ptr<SharedMemoryWindows>(
      new SharedMemoryWindows(start, size, key, std::move(shmem_handle)));
}

SharedMemoryWindows::SharedMemoryWindows(void* start,
                                         size_t size,
                                         std::string key,
                                         base::ScopedPlatformHandle handle)
    : start_(start),
      size_(size),
      key_(std::move(key)),
      handle_(std::move(handle)) {}

SharedMemoryWindows::~SharedMemoryWindows() {
  if (start_)
    UnmapViewOfFile(start_);
}

SharedMemoryWindows::Factory::~Factory() = default;

std::unique_ptr<SharedMemory> SharedMemoryWindows::Factory::CreateSharedMemory(
    size_t size) {
  return SharedMemoryWindows::Create(size);
}

}  // namespace perfetto

#endif  // !OS_WIN
