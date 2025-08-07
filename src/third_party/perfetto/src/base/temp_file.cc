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

#include "perfetto/ext/base/temp_file.h"

#include "perfetto/base/build_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#include <direct.h>
#include <fileapi.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace base {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
namespace {
std::string GetTempFilePathWin() {
  std::string tmplt = GetSysTempDir() + "\\perfetto-XXXXXX";
  StackString<255> name("%s\\perfetto-XXXXXX", GetSysTempDir().c_str());
  PERFETTO_CHECK(_mktemp_s(name.mutable_data(), name.len() + 1) == 0);
  return name.ToStdString();
}
}  // namespace
#endif

std::string GetSysTempDir() {
  const char* tmpdir = nullptr;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if ((tmpdir = getenv("TMP")))
    return tmpdir;
  if ((tmpdir = getenv("TEMP")))
    return tmpdir;
  return "C:\\TEMP";
#else
  if ((tmpdir = getenv("TMPDIR")))
    return base::StripSuffix(tmpdir, "/");
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return "/data/local/tmp";
#else
  return "/tmp";
#endif  // !OS_ANDROID
#endif  // !OS_WIN
}

// static
TempFile TempFile::Create() {
  TempFile temp_file;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  temp_file.path_ = GetTempFilePathWin();
  // Several tests want to read-back the temp file while still open. On Windows,
  // that requires FILE_SHARE_READ. FILE_SHARE_READ is NOT settable when using
  // the POSIX-compat equivalent function _open(). Hence the CreateFileA +
  // _open_osfhandle dance here.
  HANDLE h =
      ::CreateFileA(temp_file.path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_DELETE | FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY, nullptr);
  PERFETTO_CHECK(PlatformHandleChecker::IsValid(h));
  // According to MSDN, when using _open_osfhandle the caller must not call
  // CloseHandle(). Ownership is moved to the file descriptor, which then needs
  // to be closed with just with _close().
  temp_file.fd_.reset(_open_osfhandle(reinterpret_cast<intptr_t>(h), 0));
#else
  temp_file.path_ = GetSysTempDir() + "/perfetto-XXXXXXXX";
  temp_file.fd_.reset(mkstemp(&temp_file.path_[0]));
#endif
  if (PERFETTO_UNLIKELY(!temp_file.fd_)) {
    PERFETTO_FATAL("Could not create temp file %s", temp_file.path_.c_str());
  }
  return temp_file;
}

// static
TempFile TempFile::CreateUnlinked() {
  TempFile temp_file = TempFile::Create();
  temp_file.Unlink();
  return temp_file;
}

TempFile::TempFile() = default;

TempFile::~TempFile() {
  Unlink();
}

ScopedFile TempFile::ReleaseFD() {
  Unlink();
  return std::move(fd_);
}

void TempFile::Unlink() {
  if (path_.empty())
    return;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // If the FD is still open DeleteFile will mark the file as pending deletion
  // and delete it only when the process exists.
  PERFETTO_CHECK(DeleteFileA(path_.c_str()));
#else
  PERFETTO_CHECK(unlink(path_.c_str()) == 0);
#endif
  path_.clear();
}

TempFile::TempFile(TempFile&&) noexcept = default;
TempFile& TempFile::operator=(TempFile&&) = default;

// static
TempDir TempDir::Create() {
  TempDir temp_dir;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  temp_dir.path_ = GetTempFilePathWin();
  PERFETTO_CHECK(_mkdir(temp_dir.path_.c_str()) == 0);
#else
  temp_dir.path_ = GetSysTempDir() + "/perfetto-XXXXXXXX";
  PERFETTO_CHECK(mkdtemp(&temp_dir.path_[0]));
#endif
  return temp_dir;
}

TempDir::TempDir() = default;
TempDir::TempDir(TempDir&&) noexcept = default;
TempDir& TempDir::operator=(TempDir&&) = default;

TempDir::~TempDir() {
  if (path_.empty())
    return;  // For objects that get std::move()d.
  PERFETTO_CHECK(Rmdir(path_));
}

}  // namespace base
}  // namespace perfetto
