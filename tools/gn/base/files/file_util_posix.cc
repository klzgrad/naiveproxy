// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "base/command_line.h"
#include "base/containers/stack.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/posix/eintr_wrapper.h"
#include "base/stl_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "util/build_config.h"

#if defined(OS_MACOSX)
#include <AvailabilityMacros.h>
#endif

#if !defined(OS_IOS)
#include <grp.h>
#endif

// We need to do this on AIX due to some inconsistencies in how AIX
// handles XOPEN_SOURCE and ALL_SOURCE.
#if defined(OS_AIX)
extern "C" char* mkdtemp(char* path);
#endif

namespace base {

namespace {

#if defined(OS_BSD) || defined(OS_MACOSX) || defined(OS_NACL) || \
    defined(OS_ANDROID) && __ANDROID_API__ < 21
int CallStat(const char* path, stat_wrapper_t* sb) {
  return stat(path, sb);
}
int CallLstat(const char* path, stat_wrapper_t* sb) {
  return lstat(path, sb);
}
#else
int CallStat(const char* path, stat_wrapper_t* sb) {
  return stat64(path, sb);
}
int CallLstat(const char* path, stat_wrapper_t* sb) {
  return lstat64(path, sb);
}
#endif

#if !defined(OS_NACL_NONSFI)
// Helper for VerifyPathControlledByUser.
bool VerifySpecificPathControlledByUser(const FilePath& path,
                                        uid_t owner_uid,
                                        const std::set<gid_t>& group_gids) {
  stat_wrapper_t stat_info;
  if (CallLstat(path.value().c_str(), &stat_info) != 0) {
    DPLOG(ERROR) << "Failed to get information on path " << path.value();
    return false;
  }

  if (S_ISLNK(stat_info.st_mode)) {
    DLOG(ERROR) << "Path " << path.value() << " is a symbolic link.";
    return false;
  }

  if (stat_info.st_uid != owner_uid) {
    DLOG(ERROR) << "Path " << path.value() << " is owned by the wrong user.";
    return false;
  }

  if ((stat_info.st_mode & S_IWGRP) &&
      !ContainsKey(group_gids, stat_info.st_gid)) {
    DLOG(ERROR) << "Path " << path.value()
                << " is writable by an unprivileged group.";
    return false;
  }

  if (stat_info.st_mode & S_IWOTH) {
    DLOG(ERROR) << "Path " << path.value() << " is writable by any user.";
    return false;
  }

  return true;
}

std::string TempFileName() {
  return std::string(".org.chromium.Chromium.XXXXXX");
}

#if defined(OS_LINUX) || defined(OS_AIX)
// Determine if /dev/shm files can be mapped and then mprotect'd PROT_EXEC.
// This depends on the mount options used for /dev/shm, which vary among
// different Linux distributions and possibly local configuration.  It also
// depends on details of kernel--ChromeOS uses the noexec option for /dev/shm
// but its kernel allows mprotect with PROT_EXEC anyway.
bool DetermineDevShmExecutable() {
  bool result = false;
  FilePath path;

  ScopedFD fd(
      CreateAndOpenFdForTemporaryFileInDir(FilePath("/dev/shm"), &path));
  if (fd.is_valid()) {
    DeleteFile(path, false);
    long sysconf_result = sysconf(_SC_PAGESIZE);
    CHECK_GE(sysconf_result, 0);
    size_t pagesize = static_cast<size_t>(sysconf_result);
    CHECK_GE(sizeof(pagesize), sizeof(sysconf_result));
    void* mapping = mmap(nullptr, pagesize, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (mapping != MAP_FAILED) {
      if (mprotect(mapping, pagesize, PROT_READ | PROT_EXEC) == 0)
        result = true;
      munmap(mapping, pagesize);
    }
  }
  return result;
}
#endif  // defined(OS_LINUX) || defined(OS_AIX)

bool AdvanceEnumeratorWithStat(FileEnumerator* traversal,
                               FilePath* out_next_path,
                               struct stat* out_next_stat) {
  DCHECK(out_next_path);
  DCHECK(out_next_stat);
  *out_next_path = traversal->Next();
  if (out_next_path->empty())
    return false;

  *out_next_stat = traversal->GetInfo().stat();
  return true;
}

bool CopyFileContents(File* infile, File* outfile) {
  static constexpr size_t kBufferSize = 32768;
  std::vector<char> buffer(kBufferSize);

  for (;;) {
    ssize_t bytes_read = infile->ReadAtCurrentPos(buffer.data(), buffer.size());
    if (bytes_read < 0)
      return false;
    if (bytes_read == 0)
      return true;
    // Allow for partial writes
    ssize_t bytes_written_per_read = 0;
    do {
      ssize_t bytes_written_partial = outfile->WriteAtCurrentPos(
          &buffer[bytes_written_per_read], bytes_read - bytes_written_per_read);
      if (bytes_written_partial < 0)
        return false;

      bytes_written_per_read += bytes_written_partial;
    } while (bytes_written_per_read < bytes_read);
  }

  NOTREACHED();
  return false;
}
#endif  // !defined(OS_NACL_NONSFI)

#if !defined(OS_MACOSX)
// Appends |mode_char| to |mode| before the optional character set encoding; see
// https://www.gnu.org/software/libc/manual/html_node/Opening-Streams.html for
// details.
std::string AppendModeCharacter(StringPiece mode, char mode_char) {
  std::string result(mode.as_string());
  size_t comma_pos = result.find(',');
  result.insert(comma_pos == std::string::npos ? result.length() : comma_pos, 1,
                mode_char);
  return result;
}
#endif

}  // namespace

#if !defined(OS_NACL_NONSFI)
FilePath MakeAbsoluteFilePath(const FilePath& input) {
  char full_path[PATH_MAX];
  if (realpath(input.value().c_str(), full_path) == nullptr)
    return FilePath();
  return FilePath(full_path);
}

// TODO(erikkay): The Windows version of this accepts paths like "foo/bar/*"
// which works both with and without the recursive flag.  I'm not sure we need
// that functionality. If not, remove from file_util_win.cc, otherwise add it
// here.
bool DeleteFile(const FilePath& path, bool recursive) {
  const char* path_str = path.value().c_str();
  stat_wrapper_t file_info;
  if (CallLstat(path_str, &file_info) != 0) {
    // The Windows version defines this condition as success.
    return (errno == ENOENT || errno == ENOTDIR);
  }
  if (!S_ISDIR(file_info.st_mode))
    return (unlink(path_str) == 0);
  if (!recursive)
    return (rmdir(path_str) == 0);

  bool success = true;
  stack<std::string> directories;
  directories.push(path.value());
  FileEnumerator traversal(path, true,
                           FileEnumerator::FILES | FileEnumerator::DIRECTORIES |
                               FileEnumerator::SHOW_SYM_LINKS);
  for (FilePath current = traversal.Next(); !current.empty();
       current = traversal.Next()) {
    if (traversal.GetInfo().IsDirectory())
      directories.push(current.value());
    else
      success &= (unlink(current.value().c_str()) == 0);
  }

  while (!directories.empty()) {
    FilePath dir = FilePath(directories.top());
    directories.pop();
    success &= (rmdir(dir.value().c_str()) == 0);
  }
  return success;
}

bool ReplaceFile(const FilePath& from_path,
                 const FilePath& to_path,
                 File::Error* error) {
  if (rename(from_path.value().c_str(), to_path.value().c_str()) == 0)
    return true;
  if (error)
    *error = File::GetLastFileError();
  return false;
}
#endif  // !defined(OS_NACL_NONSFI)

bool CreateLocalNonBlockingPipe(int fds[2]) {
#if defined(OS_LINUX)
  return pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0;
#else
  int raw_fds[2];
  if (pipe(raw_fds) != 0)
    return false;
  ScopedFD fd_out(raw_fds[0]);
  ScopedFD fd_in(raw_fds[1]);
  if (!SetCloseOnExec(fd_out.get()))
    return false;
  if (!SetCloseOnExec(fd_in.get()))
    return false;
  if (!SetNonBlocking(fd_out.get()))
    return false;
  if (!SetNonBlocking(fd_in.get()))
    return false;
  fds[0] = fd_out.release();
  fds[1] = fd_in.release();
  return true;
#endif
}

bool SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags == -1)
    return false;
  if (flags & O_NONBLOCK)
    return true;
  if (HANDLE_EINTR(fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1)
    return false;
  return true;
}

bool SetCloseOnExec(int fd) {
#if defined(OS_NACL_NONSFI)
  const int flags = 0;
#else
  const int flags = fcntl(fd, F_GETFD);
  if (flags == -1)
    return false;
  if (flags & FD_CLOEXEC)
    return true;
#endif  // defined(OS_NACL_NONSFI)
  if (HANDLE_EINTR(fcntl(fd, F_SETFD, flags | FD_CLOEXEC)) == -1)
    return false;
  return true;
}

bool PathExists(const FilePath& path) {
  return access(path.value().c_str(), F_OK) == 0;
}

#if !defined(OS_NACL_NONSFI)
bool PathIsWritable(const FilePath& path) {
  return access(path.value().c_str(), W_OK) == 0;
}
#endif  // !defined(OS_NACL_NONSFI)

bool DirectoryExists(const FilePath& path) {
  stat_wrapper_t file_info;
  if (CallStat(path.value().c_str(), &file_info) != 0)
    return false;
  return S_ISDIR(file_info.st_mode);
}

bool ReadFromFD(int fd, char* buffer, size_t bytes) {
  size_t total_read = 0;
  while (total_read < bytes) {
    ssize_t bytes_read =
        HANDLE_EINTR(read(fd, buffer + total_read, bytes - total_read));
    if (bytes_read <= 0)
      break;
    total_read += bytes_read;
  }
  return total_read == bytes;
}

#if !defined(OS_NACL_NONSFI)

int CreateAndOpenFdForTemporaryFileInDir(const FilePath& directory,
                                         FilePath* path) {
  *path = directory.Append(TempFileName());
  const std::string& tmpdir_string = path->value();
  // this should be OK since mkstemp just replaces characters in place
  char* buffer = const_cast<char*>(tmpdir_string.c_str());

  return HANDLE_EINTR(mkstemp(buffer));
}

#if !defined(OS_FUCHSIA)
bool CreateSymbolicLink(const FilePath& target_path,
                        const FilePath& symlink_path) {
  DCHECK(!symlink_path.empty());
  DCHECK(!target_path.empty());
  return ::symlink(target_path.value().c_str(), symlink_path.value().c_str()) !=
         -1;
}

bool ReadSymbolicLink(const FilePath& symlink_path, FilePath* target_path) {
  DCHECK(!symlink_path.empty());
  DCHECK(target_path);
  char buf[PATH_MAX];
  ssize_t count = ::readlink(symlink_path.value().c_str(), buf, arraysize(buf));

  if (count <= 0) {
    target_path->clear();
    return false;
  }

  *target_path = FilePath(FilePath::StringType(buf, count));
  return true;
}

bool GetPosixFilePermissions(const FilePath& path, int* mode) {
  DCHECK(mode);

  stat_wrapper_t file_info;
  // Uses stat(), because on symbolic link, lstat() does not return valid
  // permission bits in st_mode
  if (CallStat(path.value().c_str(), &file_info) != 0)
    return false;

  *mode = file_info.st_mode & FILE_PERMISSION_MASK;
  return true;
}

bool SetPosixFilePermissions(const FilePath& path, int mode) {
  DCHECK_EQ(mode & ~FILE_PERMISSION_MASK, 0);

  // Calls stat() so that we can preserve the higher bits like S_ISGID.
  stat_wrapper_t stat_buf;
  if (CallStat(path.value().c_str(), &stat_buf) != 0)
    return false;

  // Clears the existing permission bits, and adds the new ones.
  mode_t updated_mode_bits = stat_buf.st_mode & ~FILE_PERMISSION_MASK;
  updated_mode_bits |= mode & FILE_PERMISSION_MASK;

  if (HANDLE_EINTR(chmod(path.value().c_str(), updated_mode_bits)) != 0)
    return false;

  return true;
}

bool ExecutableExistsInPath(Environment* env,
                            const FilePath::StringType& executable) {
  std::string path;
  if (!env->GetVar("PATH", &path)) {
    LOG(ERROR) << "No $PATH variable. Assuming no " << executable << ".";
    return false;
  }

  for (const StringPiece& cur_path :
       SplitStringPiece(path, ":", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY)) {
    FilePath file(cur_path);
    int permissions;
    if (GetPosixFilePermissions(file.Append(executable), &permissions) &&
        (permissions & FILE_PERMISSION_EXECUTE_BY_USER))
      return true;
  }
  return false;
}

#endif  // !OS_FUCHSIA

bool GetTempDir(FilePath* path) {
  const char* tmp = getenv("TMPDIR");
  if (tmp) {
    *path = FilePath(tmp);
    return true;
  }

  *path = FilePath("/tmp");
  return true;
}

#if !defined(OS_MACOSX)  // Mac implementation is in file_util_mac.mm.
FilePath GetHomeDir() {
  const char* home_dir = getenv("HOME");
  if (home_dir && home_dir[0])
    return FilePath(home_dir);

  FilePath rv;
  if (GetTempDir(&rv))
    return rv;

  // Last resort.
  return FilePath("/tmp");
}
#endif  // !defined(OS_MACOSX)

bool CreateTemporaryFile(FilePath* path) {
  FilePath directory;
  if (!GetTempDir(&directory))
    return false;
  int fd = CreateAndOpenFdForTemporaryFileInDir(directory, path);
  if (fd < 0)
    return false;
  close(fd);
  return true;
}

FILE* CreateAndOpenTemporaryFileInDir(const FilePath& dir, FilePath* path) {
  int fd = CreateAndOpenFdForTemporaryFileInDir(dir, path);
  if (fd < 0)
    return nullptr;

  FILE* file = fdopen(fd, "a+");
  if (!file)
    close(fd);
  return file;
}

bool CreateTemporaryFileInDir(const FilePath& dir, FilePath* temp_file) {
  int fd = CreateAndOpenFdForTemporaryFileInDir(dir, temp_file);
  return ((fd >= 0) && !IGNORE_EINTR(close(fd)));
}

static bool CreateTemporaryDirInDirImpl(const FilePath& base_dir,
                                        const FilePath::StringType& name_tmpl,
                                        FilePath* new_dir) {
  DCHECK(name_tmpl.find("XXXXXX") != FilePath::StringType::npos)
      << "Directory name template must contain \"XXXXXX\".";

  FilePath sub_dir = base_dir.Append(name_tmpl);
  std::string sub_dir_string = sub_dir.value();

  // this should be OK since mkdtemp just replaces characters in place
  char* buffer = const_cast<char*>(sub_dir_string.c_str());
  char* dtemp = mkdtemp(buffer);
  if (!dtemp) {
    DPLOG(ERROR) << "mkdtemp";
    return false;
  }
  *new_dir = FilePath(dtemp);
  return true;
}

bool CreateTemporaryDirInDir(const FilePath& base_dir,
                             const FilePath::StringType& prefix,
                             FilePath* new_dir) {
  FilePath::StringType mkdtemp_template = prefix;
  mkdtemp_template.append(FILE_PATH_LITERAL("XXXXXX"));
  return CreateTemporaryDirInDirImpl(base_dir, mkdtemp_template, new_dir);
}

bool CreateNewTempDirectory(const FilePath::StringType& prefix,
                            FilePath* new_temp_path) {
  FilePath tmpdir;
  if (!GetTempDir(&tmpdir))
    return false;

  return CreateTemporaryDirInDirImpl(tmpdir, TempFileName(), new_temp_path);
}

bool CreateDirectoryAndGetError(const FilePath& full_path, File::Error* error) {
  std::vector<FilePath> subpaths;

  // Collect a list of all parent directories.
  FilePath last_path = full_path;
  subpaths.push_back(full_path);
  for (FilePath path = full_path.DirName(); path.value() != last_path.value();
       path = path.DirName()) {
    subpaths.push_back(path);
    last_path = path;
  }

  // Iterate through the parents and create the missing ones.
  for (std::vector<FilePath>::reverse_iterator i = subpaths.rbegin();
       i != subpaths.rend(); ++i) {
    if (DirectoryExists(*i))
      continue;
    if (mkdir(i->value().c_str(), 0700) == 0)
      continue;
    // Mkdir failed, but it might have failed with EEXIST, or some other error
    // due to the the directory appearing out of thin air. This can occur if
    // two processes are trying to create the same file system tree at the same
    // time. Check to see if it exists and make sure it is a directory.
    int saved_errno = errno;
    if (!DirectoryExists(*i)) {
      if (error)
        *error = File::OSErrorToFileError(saved_errno);
      return false;
    }
  }
  return true;
}

bool NormalizeFilePath(const FilePath& path, FilePath* normalized_path) {
  FilePath real_path_result = MakeAbsoluteFilePath(path);
  if (real_path_result.empty())
    return false;

  // To be consistant with windows, fail if |real_path_result| is a
  // directory.
  if (DirectoryExists(real_path_result))
    return false;

  *normalized_path = real_path_result;
  return true;
}

// TODO(rkc): Refactor GetFileInfo and FileEnumerator to handle symlinks
// correctly. http://code.google.com/p/chromium-os/issues/detail?id=15948
bool IsLink(const FilePath& file_path) {
  stat_wrapper_t st;
  // If we can't lstat the file, it's safe to assume that the file won't at
  // least be a 'followable' link.
  if (CallLstat(file_path.value().c_str(), &st) != 0)
    return false;
  return S_ISLNK(st.st_mode);
}

bool GetFileInfo(const FilePath& file_path, File::Info* results) {
  stat_wrapper_t file_info;
  if (CallStat(file_path.value().c_str(), &file_info) != 0)
    return false;

  results->FromStat(file_info);
  return true;
}
#endif  // !defined(OS_NACL_NONSFI)

FILE* OpenFile(const FilePath& filename, const char* mode) {
  // 'e' is unconditionally added below, so be sure there is not one already
  // present before a comma in |mode|.
  DCHECK(
      strchr(mode, 'e') == nullptr ||
      (strchr(mode, ',') != nullptr && strchr(mode, 'e') > strchr(mode, ',')));
  FILE* result = nullptr;
#if defined(OS_MACOSX)
  // macOS does not provide a mode character to set O_CLOEXEC; see
  // https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/fopen.3.html.
  const char* the_mode = mode;
#else
  std::string mode_with_e(AppendModeCharacter(mode, 'e'));
  const char* the_mode = mode_with_e.c_str();
#endif
  do {
    result = fopen(filename.value().c_str(), the_mode);
  } while (!result && errno == EINTR);
#if defined(OS_MACOSX)
  // Mark the descriptor as close-on-exec.
  if (result)
    SetCloseOnExec(fileno(result));
#endif
  return result;
}

// NaCl doesn't implement system calls to open files directly.
#if !defined(OS_NACL)
FILE* FileToFILE(File file, const char* mode) {
  FILE* stream = fdopen(file.GetPlatformFile(), mode);
  if (stream)
    file.TakePlatformFile();
  return stream;
}
#endif  // !defined(OS_NACL)

int ReadFile(const FilePath& filename, char* data, int max_size) {
  int fd = HANDLE_EINTR(open(filename.value().c_str(), O_RDONLY));
  if (fd < 0)
    return -1;

  ssize_t bytes_read = HANDLE_EINTR(read(fd, data, max_size));
  if (IGNORE_EINTR(close(fd)) < 0)
    return -1;
  return bytes_read;
}

int WriteFile(const FilePath& filename, const char* data, int size) {
  int fd = HANDLE_EINTR(creat(filename.value().c_str(), 0666));
  if (fd < 0)
    return -1;

  int bytes_written = WriteFileDescriptor(fd, data, size) ? size : -1;
  if (IGNORE_EINTR(close(fd)) < 0)
    return -1;
  return bytes_written;
}

bool WriteFileDescriptor(const int fd, const char* data, int size) {
  // Allow for partial writes.
  ssize_t bytes_written_total = 0;
  for (ssize_t bytes_written_partial = 0; bytes_written_total < size;
       bytes_written_total += bytes_written_partial) {
    bytes_written_partial = HANDLE_EINTR(
        write(fd, data + bytes_written_total, size - bytes_written_total));
    if (bytes_written_partial < 0)
      return false;
  }

  return true;
}

#if !defined(OS_NACL_NONSFI)

bool AppendToFile(const FilePath& filename, const char* data, int size) {
  bool ret = true;
  int fd = HANDLE_EINTR(open(filename.value().c_str(), O_WRONLY | O_APPEND));
  if (fd < 0) {
    return false;
  }

  // This call will either write all of the data or return false.
  if (!WriteFileDescriptor(fd, data, size)) {
    ret = false;
  }

  if (IGNORE_EINTR(close(fd)) < 0) {
    return false;
  }

  return ret;
}

bool GetCurrentDirectory(FilePath* dir) {
  char system_buffer[PATH_MAX] = "";
  if (!getcwd(system_buffer, sizeof(system_buffer))) {
    NOTREACHED();
    return false;
  }
  *dir = FilePath(system_buffer);
  return true;
}

bool SetCurrentDirectory(const FilePath& path) {
  return chdir(path.value().c_str()) == 0;
}

bool VerifyPathControlledByUser(const FilePath& base,
                                const FilePath& path,
                                uid_t owner_uid,
                                const std::set<gid_t>& group_gids) {
  if (base != path && !base.IsParent(path)) {
    DLOG(ERROR) << "|base| must be a subdirectory of |path|.  base = \""
                << base.value() << "\", path = \"" << path.value() << "\"";
    return false;
  }

  std::vector<FilePath::StringType> base_components;
  std::vector<FilePath::StringType> path_components;

  base.GetComponents(&base_components);
  path.GetComponents(&path_components);

  std::vector<FilePath::StringType>::const_iterator ib, ip;
  for (ib = base_components.begin(), ip = path_components.begin();
       ib != base_components.end(); ++ib, ++ip) {
    // |base| must be a subpath of |path|, so all components should match.
    // If these CHECKs fail, look at the test that base is a parent of
    // path at the top of this function.
    DCHECK(ip != path_components.end());
    DCHECK(*ip == *ib);
  }

  FilePath current_path = base;
  if (!VerifySpecificPathControlledByUser(current_path, owner_uid, group_gids))
    return false;

  for (; ip != path_components.end(); ++ip) {
    current_path = current_path.Append(*ip);
    if (!VerifySpecificPathControlledByUser(current_path, owner_uid,
                                            group_gids))
      return false;
  }
  return true;
}

#if defined(OS_MACOSX) && !defined(OS_IOS)
bool VerifyPathControlledByAdmin(const FilePath& path) {
  const unsigned kRootUid = 0;
  const FilePath kFileSystemRoot("/");

  // The name of the administrator group on mac os.
  const char* const kAdminGroupNames[] = {"admin", "wheel"};

  std::set<gid_t> allowed_group_ids;
  for (int i = 0, ie = arraysize(kAdminGroupNames); i < ie; ++i) {
    struct group* group_record = getgrnam(kAdminGroupNames[i]);
    if (!group_record) {
      DPLOG(ERROR) << "Could not get the group ID of group \""
                   << kAdminGroupNames[i] << "\".";
      continue;
    }

    allowed_group_ids.insert(group_record->gr_gid);
  }

  return VerifyPathControlledByUser(kFileSystemRoot, path, kRootUid,
                                    allowed_group_ids);
}
#endif  // defined(OS_MACOSX) && !defined(OS_IOS)

int GetMaximumPathComponentLength(const FilePath& path) {
  return pathconf(path.value().c_str(), _PC_NAME_MAX);
}

bool GetShmemTempDir(bool executable, FilePath* path) {
#if defined(OS_LINUX) || defined(OS_AIX)
  bool use_dev_shm = true;
  if (executable) {
    static const bool s_dev_shm_executable = DetermineDevShmExecutable();
    use_dev_shm = s_dev_shm_executable;
  }
  if (use_dev_shm) {
    *path = FilePath("/dev/shm");
    return true;
  }
#endif  // defined(OS_LINUX) || defined(OS_AIX)
  return GetTempDir(path);
}

#if !defined(OS_MACOSX)
// Mac has its own implementation, this is for all other Posix systems.
bool CopyFile(const FilePath& from_path, const FilePath& to_path) {
  File infile;
  infile = File(from_path, File::FLAG_OPEN | File::FLAG_READ);
  if (!infile.IsValid())
    return false;

  File outfile(to_path, File::FLAG_WRITE | File::FLAG_CREATE_ALWAYS);
  if (!outfile.IsValid())
    return false;

  return CopyFileContents(&infile, &outfile);
}
#endif  // !defined(OS_MACOSX)

#endif  // !defined(OS_NACL_NONSFI)
}  // namespace base
