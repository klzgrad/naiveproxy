/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "perfetto/ext/base/scoped_mmap.h"

#include <utility>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <sys/mman.h>
#include <unistd.h>
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#endif

namespace perfetto::base {
namespace {

ScopedPlatformHandle OpenFileForMmap(const char* fname) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  return OpenFile(fname, O_RDONLY);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // This does not use base::OpenFile to avoid getting an exclusive lock.
  return ScopedPlatformHandle(CreateFileA(fname, GENERIC_READ, FILE_SHARE_READ,
                                          nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
#else
  // mmap is not supported. Do not even open the file.
  base::ignore_result(fname);
  return ScopedPlatformHandle();
#endif
}

}  // namespace

ScopedMmap::ScopedMmap(ScopedMmap&& other) noexcept {
  *this = std::move(other);
}

ScopedMmap& ScopedMmap::operator=(ScopedMmap&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  std::swap(ptr_, other.ptr_);
  std::swap(length_, other.length_);
  std::swap(file_, other.file_);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  std::swap(map_, other.map_);
#endif
  return *this;
}

ScopedMmap::~ScopedMmap() {
  reset();
}

// static
ScopedMmap ScopedMmap::FromHandle(base::ScopedPlatformHandle file,
                                  size_t length) {
  ScopedMmap ret;
  if (!file) {
    return ret;
  }
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  void* ptr = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, *file, 0);
  if (ptr != MAP_FAILED) {
    ret.ptr_ = ptr;
    ret.length_ = length;
    ret.file_ = std::move(file);
  }
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  ScopedPlatformHandle map(
      CreateFileMapping(*file, nullptr, PAGE_READONLY, 0, 0, nullptr));
  if (!map) {
    return ret;
  }
  void* ptr = MapViewOfFile(*map, FILE_MAP_READ, 0, 0, length);
  if (ptr != nullptr) {
    ret.ptr_ = ptr;
    ret.length_ = length;
    ret.file_ = std::move(file);
    ret.map_ = std::move(map);
  }
#else
  base::ignore_result(length);
#endif
  return ret;
}

bool ScopedMmap::reset() noexcept {
  bool ret = true;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  if (ptr_ != nullptr) {
    ret = munmap(ptr_, length_) == 0;
  }
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (ptr_ != nullptr) {
    ret = UnmapViewOfFile(ptr_);
  }
  map_.reset();
#endif
  ptr_ = nullptr;
  length_ = 0;
  file_.reset();
  return ret;
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
// static
ScopedMmap ScopedMmap::InheritMmappedRange(void* data, size_t size) {
  ScopedMmap ret;
  ret.ptr_ = data;
  ret.length_ = size;
  return ret;
}
#endif

ScopedMmap ReadMmapFilePart(const char* fname, size_t length) {
  return ScopedMmap::FromHandle(OpenFileForMmap(fname), length);
}

ScopedMmap ReadMmapWholeFile(const char* fname) {
  ScopedPlatformHandle file = OpenFileForMmap(fname);
  if (!file) {
    return ScopedMmap();
  }
  std::optional<uint64_t> file_size = GetFileSize(file.get());
  if (!file_size.has_value()) {
    return ScopedMmap();
  }
  size_t size = static_cast<size_t>(*file_size);
  if (static_cast<uint64_t>(size) != *file_size) {
    return ScopedMmap();
  }
  return ScopedMmap::FromHandle(std::move(file), size);
}

}  // namespace perfetto::base
