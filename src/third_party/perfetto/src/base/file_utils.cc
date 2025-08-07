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

#include "perfetto/ext/base/file_utils.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/platform_handle.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/platform.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#include <direct.h>
#include <io.h>
#include <stringapiset.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_SET_FILE_PERMISSIONS
#include <fcntl.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace perfetto {
namespace base {
namespace {
constexpr size_t kBufSize = 2048;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
// Wrap FindClose to: (1) make the return unix-style; (2) deal with stdcall.
int CloseFindHandle(HANDLE h) {
  return FindClose(h) ? 0 : -1;
}

std::optional<std::wstring> ToUtf16(const std::string str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str.data(),
                                static_cast<int>(str.size()), nullptr, 0);
  if (len < 0) {
    return std::nullopt;
  }
  std::vector<wchar_t> tmp;
  tmp.resize(static_cast<std::vector<wchar_t>::size_type>(len));
  len =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
                          tmp.data(), static_cast<int>(tmp.size()));
  if (len < 0) {
    return std::nullopt;
  }
  PERFETTO_CHECK(static_cast<std::vector<wchar_t>::size_type>(len) ==
                 tmp.size());
  return std::wstring(tmp.data(), tmp.size());
}

#endif

}  // namespace

ssize_t Read(int fd, void* dst, size_t dst_size) {
  ssize_t ret;
  platform::BeforeMaybeBlockingSyscall();
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  ret = _read(fd, dst, static_cast<unsigned>(dst_size));
#else
  ret = PERFETTO_EINTR(read(fd, dst, dst_size));
#endif
  platform::AfterMaybeBlockingSyscall();
  return ret;
}

bool ReadFileDescriptor(int fd, std::string* out) {
  // Do not override existing data in string.
  size_t i = out->size();

  struct stat buf{};
  if (fstat(fd, &buf) != -1) {
    if (buf.st_size > 0)
      out->resize(i + static_cast<size_t>(buf.st_size));
  }

  ssize_t bytes_read;
  for (;;) {
    if (out->size() < i + kBufSize)
      out->resize(out->size() + kBufSize);

    bytes_read = Read(fd, &((*out)[i]), kBufSize);
    if (bytes_read > 0) {
      i += static_cast<size_t>(bytes_read);
    } else {
      out->resize(i);
      return bytes_read == 0;
    }
  }
}

bool ReadPlatformHandle(PlatformHandle h, std::string* out) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Do not override existing data in string.
  size_t i = out->size();

  for (;;) {
    if (out->size() < i + kBufSize)
      out->resize(out->size() + kBufSize);
    DWORD bytes_read = 0;
    auto res = ::ReadFile(h, &((*out)[i]), kBufSize, &bytes_read, nullptr);
    if (res && bytes_read > 0) {
      i += static_cast<size_t>(bytes_read);
    } else {
      out->resize(i);
      const bool is_eof = res && bytes_read == 0;
      auto err = res ? 0 : GetLastError();
      // The "Broken pipe" error on Windows is slightly different than Unix:
      // On Unix: a "broken pipe" error can happen only on the writer side. On
      // the reader there is no broken pipe, just a EOF.
      // On windows: the reader also sees a broken pipe error.
      // Here we normalize on the Unix behavior, treating broken pipe as EOF.
      return is_eof || err == ERROR_BROKEN_PIPE;
    }
  }
#else
  return ReadFileDescriptor(h, out);
#endif
}

bool ReadFileStream(FILE* f, std::string* out) {
  return ReadFileDescriptor(fileno(f), out);
}

bool ReadFile(const std::string& path, std::string* out) {
  base::ScopedFile fd = base::OpenFile(path, O_RDONLY);
  if (!fd)
    return false;

  return ReadFileDescriptor(*fd, out);
}

ssize_t WriteAll(int fd, const void* buf, size_t count) {
  size_t written = 0;
  while (written < count) {
    // write() on windows takes an unsigned int size.
    uint32_t bytes_left = static_cast<uint32_t>(
        std::min(count - written, static_cast<size_t>(UINT32_MAX)));
    platform::BeforeMaybeBlockingSyscall();
    ssize_t wr = PERFETTO_EINTR(
        write(fd, static_cast<const char*>(buf) + written, bytes_left));
    platform::AfterMaybeBlockingSyscall();
    if (wr == 0)
      break;
    if (wr < 0)
      return wr;
    written += static_cast<size_t>(wr);
  }
  return static_cast<ssize_t>(written);
}

ssize_t WriteAllHandle(PlatformHandle h, const void* buf, size_t count) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  DWORD wsize = 0;
  if (::WriteFile(h, buf, static_cast<DWORD>(count), &wsize, nullptr)) {
    return wsize;
  } else {
    return -1;
  }
#else
  return WriteAll(h, buf, count);
#endif
}

bool FlushFile(int fd) {
  PERFETTO_DCHECK(fd != 0);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return !PERFETTO_EINTR(fdatasync(fd));
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return !PERFETTO_EINTR(_commit(fd));
#else
  return !PERFETTO_EINTR(fsync(fd));
#endif
}

bool Mkdir(const std::string& path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return _mkdir(path.c_str()) == 0;
#else
  return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool Rmdir(const std::string& path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return _rmdir(path.c_str()) == 0;
#else
  return rmdir(path.c_str()) == 0;
#endif
}

int CloseFile(int fd) {
  return close(fd);
}

ScopedFile OpenFile(const std::string& path, int flags, FileOpenMode mode) {
  // If a new file might be created, ensure that the permissions for the new
  // file are explicitly specified.
  PERFETTO_CHECK((flags & O_CREAT) == 0 || mode != kFileModeInvalid);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Always use O_BINARY on Windows, to avoid silly EOL translations.
  ScopedFile fd(_open(path.c_str(), flags | O_BINARY, mode));
#else
  // Always open a ScopedFile with O_CLOEXEC so we can safely fork and exec.
  ScopedFile fd(open(path.c_str(), flags | O_CLOEXEC, mode));
#endif
  return fd;
}

ScopedFstream OpenFstream(const char* path, const char* mode) {
  ScopedFstream file;
// On Windows fopen interprets filename using the ANSI or OEM codepage but
// sqlite3_value_text returns a UTF-8 string. To make sure we interpret the
// filename correctly we use _wfopen and a UTF-16 string on windows.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  auto w_path = ToUtf16(path);
  auto w_mode = ToUtf16(mode);
  if (w_path && w_mode) {
    file.reset(_wfopen(w_path->c_str(), w_mode->c_str()));
  }
#else
  file.reset(fopen(path, mode));
#endif
  return file;
}

bool FileExists(const std::string& path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return _access(path.c_str(), 0) == 0;
#else
  return access(path.c_str(), F_OK) == 0;
#endif
}

// Declared in base/platform_handle.h.
int ClosePlatformHandle(PlatformHandle handle) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Make the return value UNIX-style.
  return CloseHandle(handle) ? 0 : -1;
#else
  return close(handle);
#endif
}

base::Status ListFilesRecursive(const std::string& dir_path,
                                std::vector<std::string>& output) {
  std::string root_dir_path = dir_path;
  if (root_dir_path.back() == '\\') {
    root_dir_path.back() = '/';
  } else if (root_dir_path.back() != '/') {
    root_dir_path.push_back('/');
  }

  // dir_queue contains full paths to the directories. The paths include the
  // root_dir_path at the beginning and the trailing slash at the end.
  std::deque<std::string> dir_queue;
  dir_queue.push_back(root_dir_path);

  while (!dir_queue.empty()) {
    const std::string cur_dir = std::move(dir_queue.front());
    dir_queue.pop_front();
#if PERFETTO_BUILDFLAG(PERFETTO_OS_NACL)
    return base::ErrStatus("ListFilesRecursive not supported yet");
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    std::string glob_path = cur_dir + "*";
    // + 1 because we also have to count the NULL terminator.
    if (glob_path.length() + 1 > MAX_PATH)
      return base::ErrStatus("Directory path %s is too long", dir_path.c_str());
    WIN32_FIND_DATAA ffd;

    base::ScopedResource<HANDLE, CloseFindHandle, nullptr, false,
                         base::PlatformHandleChecker>
        hFind(FindFirstFileA(glob_path.c_str(), &ffd));
    if (!hFind) {
      // For empty directories, there should be at least one entry '.'.
      // If FindFirstFileA returns INVALID_HANDLE_VALUE, this means directory
      // couldn't be accessed.
      return base::ErrStatus("Failed to open directory %s", cur_dir.c_str());
    }
    do {
      if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
        continue;
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        std::string subdir_path = cur_dir + ffd.cFileName + '/';
        dir_queue.push_back(subdir_path);
      } else {
        const std::string full_path = cur_dir + ffd.cFileName;
        PERFETTO_CHECK(full_path.length() > root_dir_path.length());
        output.push_back(full_path.substr(root_dir_path.length()));
      }
    } while (FindNextFileA(*hFind, &ffd));
#else
    ScopedDir dir = ScopedDir(opendir(cur_dir.c_str()));
    if (!dir) {
      return base::ErrStatus("Failed to open directory %s", cur_dir.c_str());
    }
    for (auto* dirent = readdir(dir.get()); dirent != nullptr;
         dirent = readdir(dir.get())) {
      if (strcmp(dirent->d_name, ".") == 0 ||
          strcmp(dirent->d_name, "..") == 0) {
        continue;
      }
      struct stat dirstat;
      std::string full_path = cur_dir + dirent->d_name;
      PERFETTO_CHECK(stat(full_path.c_str(), &dirstat) == 0);
      if (S_ISDIR(dirstat.st_mode)) {
        dir_queue.push_back(full_path + '/');
      } else if (S_ISREG(dirstat.st_mode)) {
        PERFETTO_CHECK(full_path.length() > root_dir_path.length());
        output.push_back(full_path.substr(root_dir_path.length()));
      }
    }
#endif
  }
  return base::OkStatus();
}

std::string GetFileExtension(const std::string& filename) {
  auto ext_idx = filename.rfind('.');
  if (ext_idx == std::string::npos)
    return std::string();
  return filename.substr(ext_idx);
}

base::Status SetFilePermissions(const std::string& file_path,
                                const std::string& group_name_or_id,
                                const std::string& mode_bits) {
#ifdef PERFETTO_SET_FILE_PERMISSIONS
  PERFETTO_CHECK(!file_path.empty());
  PERFETTO_CHECK(!group_name_or_id.empty());

  // Default |group_id| to -1 for not changing the group ownership.
  gid_t group_id = static_cast<gid_t>(-1);
  auto maybe_group_id = base::StringToUInt32(group_name_or_id);
  if (maybe_group_id) {  // A numerical group ID.
    group_id = *maybe_group_id;
  } else {  // A group name.
    struct group* file_group = nullptr;
    // Query the group ID of |group|.
    do {
      file_group = getgrnam(group_name_or_id.c_str());
    } while (file_group == nullptr && errno == EINTR);
    if (file_group == nullptr) {
      return base::ErrStatus("Failed to get group information of %s ",
                             group_name_or_id.c_str());
    }
    group_id = file_group->gr_gid;
  }

  if (PERFETTO_EINTR(chown(file_path.c_str(), geteuid(), group_id))) {
    return base::ErrStatus("Failed to chown %s ", file_path.c_str());
  }

  // |mode| accepts values like "0660" as "rw-rw----" mode bits.
  auto mode_value = base::StringToInt32(mode_bits, 8);
  if (!(mode_bits.size() == 4 && mode_value.has_value())) {
    return base::ErrStatus(
        "The chmod mode bits must be a 4-digit octal number, e.g. 0660");
  }
  if (PERFETTO_EINTR(
          chmod(file_path.c_str(), static_cast<mode_t>(mode_value.value())))) {
    return base::ErrStatus("Failed to chmod %s", file_path.c_str());
  }
  return base::OkStatus();
#else
  base::ignore_result(file_path);
  base::ignore_result(group_name_or_id);
  base::ignore_result(mode_bits);
  return base::ErrStatus(
      "Setting file permissions is not supported on this platform");
#endif
}

std::optional<uint64_t> GetFileSize(const std::string& file_path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // This does not use base::OpenFile to avoid getting an exclusive lock.
  base::ScopedPlatformHandle fd(
      CreateFileA(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
#else
  base::ScopedFile fd(base::OpenFile(file_path, O_RDONLY | O_CLOEXEC));
#endif
  if (!fd) {
    return std::nullopt;
  }
  return GetFileSize(*fd);
}

std::optional<uint64_t> GetFileSize(PlatformHandle fd) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  LARGE_INTEGER file_size;
  file_size.QuadPart = 0;
  if (!GetFileSizeEx(fd, &file_size)) {
    return std::nullopt;
  }
  static_assert(sizeof(decltype(file_size.QuadPart)) <= sizeof(uint64_t));
  return static_cast<uint64_t>(file_size.QuadPart);
#else
  struct stat buf{};
  if (fstat(fd, &buf) == -1) {
    return std::nullopt;
  }
  static_assert(sizeof(decltype(buf.st_size)) <= sizeof(uint64_t));
  return static_cast<uint64_t>(buf.st_size);
#endif
}

}  // namespace base
}  // namespace perfetto
