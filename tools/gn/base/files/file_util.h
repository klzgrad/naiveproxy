// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains utility functions for dealing with the local
// filesystem.

#ifndef BASE_FILES_FILE_UTIL_H_
#define BASE_FILES_FILE_UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <set>
#include <string>
#include <vector>

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/strings/string16.h"
#include "util/build_config.h"

#if defined(OS_WIN)
#include "base/win/windows_types.h"
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#endif

namespace base {

class Environment;

//-----------------------------------------------------------------------------
// Functions that involve filesystem access or modification:

// Returns an absolute version of a relative path. Returns an empty path on
// error. On POSIX, this function fails if the path does not exist. This
// function can result in I/O so it can be slow.
FilePath MakeAbsoluteFilePath(const FilePath& input);

// Returns the total number of bytes used by all the files under |root_path|.
// If the path does not exist the function returns 0.
//
// This function is implemented using the FileEnumerator class so it is not
// particularly speedy in any platform.
int64_t ComputeDirectorySize(const FilePath& root_path);

// Deletes the given path, whether it's a file or a directory.
// If it's a directory, it's perfectly happy to delete all of the
// directory's contents.  Passing true to recursive deletes
// subdirectories and their contents as well.
// Returns true if successful, false otherwise. It is considered successful
// to attempt to delete a file that does not exist.
//
// In posix environment and if |path| is a symbolic link, this deletes only
// the symlink. (even if the symlink points to a non-existent file)
//
// WARNING: USING THIS WITH recursive==true IS EQUIVALENT
//          TO "rm -rf", SO USE WITH CAUTION.
bool DeleteFile(const FilePath& path, bool recursive);

#if defined(OS_WIN)
// Schedules to delete the given path, whether it's a file or a directory, until
// the operating system is restarted.
// Note:
// 1) The file/directory to be deleted should exist in a temp folder.
// 2) The directory to be deleted must be empty.
bool DeleteFileAfterReboot(const FilePath& path);
#endif

// Renames file |from_path| to |to_path|. Both paths must be on the same
// volume, or the function will fail. Destination file will be created
// if it doesn't exist. Prefer this function over Move when dealing with
// temporary files. On Windows it preserves attributes of the target file.
// Returns true on success, leaving *error unchanged.
// Returns false on failure and sets *error appropriately, if it is non-NULL.
bool ReplaceFile(const FilePath& from_path,
                 const FilePath& to_path,
                 File::Error* error);

// Returns true if the given path exists on the local filesystem,
// false otherwise.
bool PathExists(const FilePath& path);

// Returns true if the given path is writable by the user, false otherwise.
bool PathIsWritable(const FilePath& path);

// Returns true if the given path exists and is a directory, false otherwise.
bool DirectoryExists(const FilePath& path);

// Returns true if the contents of the two files given are equal, false
// otherwise.  If either file can't be read, returns false.
bool ContentsEqual(const FilePath& filename1, const FilePath& filename2);

// Returns true if the contents of the two text files given are equal, false
// otherwise.  This routine treats "\r\n" and "\n" as equivalent.
bool TextContentsEqual(const FilePath& filename1, const FilePath& filename2);

// Reads the file at |path| into |contents| and returns true on success and
// false on error.  For security reasons, a |path| containing path traversal
// components ('..') is treated as a read error and |contents| is set to empty.
// In case of I/O error, |contents| holds the data that could be read from the
// file before the error occurred.
// |contents| may be NULL, in which case this function is useful for its side
// effect of priming the disk cache (could be used for unit tests).
bool ReadFileToString(const FilePath& path, std::string* contents);

// Reads the file at |path| into |contents| and returns true on success and
// false on error.  For security reasons, a |path| containing path traversal
// components ('..') is treated as a read error and |contents| is set to empty.
// In case of I/O error, |contents| holds the data that could be read from the
// file before the error occurred.  When the file size exceeds |max_size|, the
// function returns false with |contents| holding the file truncated to
// |max_size|.
// |contents| may be NULL, in which case this function is useful for its side
// effect of priming the disk cache (could be used for unit tests).
bool ReadFileToStringWithMaxSize(const FilePath& path,
                                 std::string* contents,
                                 size_t max_size);

#if defined(OS_POSIX) || defined(OS_FUCHSIA)

// Read exactly |bytes| bytes from file descriptor |fd|, storing the result
// in |buffer|. This function is protected against EINTR and partial reads.
// Returns true iff |bytes| bytes have been successfully read from |fd|.
bool ReadFromFD(int fd, char* buffer, size_t bytes);

// Performs the same function as CreateAndOpenTemporaryFileInDir(), but returns
// the file-descriptor directly, rather than wrapping it into a FILE. Returns
// -1 on failure.
int CreateAndOpenFdForTemporaryFileInDir(const FilePath& dir, FilePath* path);

#endif  // OS_POSIX || OS_FUCHSIA

#if defined(OS_POSIX)

// Creates a symbolic link at |symlink| pointing to |target|.  Returns
// false on failure.
bool CreateSymbolicLink(const FilePath& target, const FilePath& symlink);

// Reads the given |symlink| and returns where it points to in |target|.
// Returns false upon failure.
bool ReadSymbolicLink(const FilePath& symlink, FilePath* target);

// Bits and masks of the file permission.
enum FilePermissionBits {
  FILE_PERMISSION_MASK = S_IRWXU | S_IRWXG | S_IRWXO,
  FILE_PERMISSION_USER_MASK = S_IRWXU,
  FILE_PERMISSION_GROUP_MASK = S_IRWXG,
  FILE_PERMISSION_OTHERS_MASK = S_IRWXO,

  FILE_PERMISSION_READ_BY_USER = S_IRUSR,
  FILE_PERMISSION_WRITE_BY_USER = S_IWUSR,
  FILE_PERMISSION_EXECUTE_BY_USER = S_IXUSR,
  FILE_PERMISSION_READ_BY_GROUP = S_IRGRP,
  FILE_PERMISSION_WRITE_BY_GROUP = S_IWGRP,
  FILE_PERMISSION_EXECUTE_BY_GROUP = S_IXGRP,
  FILE_PERMISSION_READ_BY_OTHERS = S_IROTH,
  FILE_PERMISSION_WRITE_BY_OTHERS = S_IWOTH,
  FILE_PERMISSION_EXECUTE_BY_OTHERS = S_IXOTH,
};

// Reads the permission of the given |path|, storing the file permission
// bits in |mode|. If |path| is symbolic link, |mode| is the permission of
// a file which the symlink points to.
bool GetPosixFilePermissions(const FilePath& path, int* mode);
// Sets the permission of the given |path|. If |path| is symbolic link, sets
// the permission of a file which the symlink points to.
bool SetPosixFilePermissions(const FilePath& path, int mode);

// Returns true iff |executable| can be found in any directory specified by the
// environment variable in |env|.
bool ExecutableExistsInPath(Environment* env,
                            const FilePath::StringType& executable);

#endif  // OS_POSIX

// Returns true if the given directory is empty
bool IsDirectoryEmpty(const FilePath& dir_path);

// Get the temporary directory provided by the system.
//
// WARNING: In general, you should use CreateTemporaryFile variants below
// instead of this function. Those variants will ensure that the proper
// permissions are set so that other users on the system can't edit them while
// they're open (which can lead to security issues).
bool GetTempDir(FilePath* path);

// Creates a temporary file. The full path is placed in |path|, and the
// function returns true if was successful in creating the file. The file will
// be empty and all handles closed after this function returns.
bool CreateTemporaryFile(FilePath* path);

// Same as CreateTemporaryFile but the file is created in |dir|.
bool CreateTemporaryFileInDir(const FilePath& dir, FilePath* temp_file);

// Create and open a temporary file.  File is opened for read/write.
// The full path is placed in |path|.
// Returns a handle to the opened file or NULL if an error occurred.
FILE* CreateAndOpenTemporaryFile(FilePath* path);

// Similar to CreateAndOpenTemporaryFile, but the file is created in |dir|.
FILE* CreateAndOpenTemporaryFileInDir(const FilePath& dir, FilePath* path);

// Create a new directory. If prefix is provided, the new directory name is in
// the format of prefixyyyy.
// NOTE: prefix is ignored in the POSIX implementation.
// If success, return true and output the full path of the directory created.
bool CreateNewTempDirectory(const FilePath::StringType& prefix,
                            FilePath* new_temp_path);

// Create a directory within another directory.
// Extra characters will be appended to |prefix| to ensure that the
// new directory does not have the same name as an existing directory.
bool CreateTemporaryDirInDir(const FilePath& base_dir,
                             const FilePath::StringType& prefix,
                             FilePath* new_dir);

// Creates a directory, as well as creating any parent directories, if they
// don't exist. Returns 'true' on successful creation, or if the directory
// already exists.  The directory is only readable by the current user.
// Returns true on success, leaving *error unchanged.
// Returns false on failure and sets *error appropriately, if it is non-NULL.
bool CreateDirectoryAndGetError(const FilePath& full_path, File::Error* error);

// Backward-compatible convenience method for the above.
bool CreateDirectory(const FilePath& full_path);

// Returns the file size. Returns true on success.
bool GetFileSize(const FilePath& file_path, int64_t* file_size);

// Sets |real_path| to |path| with symbolic links and junctions expanded.
// On windows, make sure the path starts with a lettered drive.
// |path| must reference a file.  Function will fail if |path| points to
// a directory or to a nonexistent path.  On windows, this function will
// fail if |path| is a junction or symlink that points to an empty file,
// or if |real_path| would be longer than MAX_PATH characters.
bool NormalizeFilePath(const FilePath& path, FilePath* real_path);

#if defined(OS_WIN)

// Given a path in NT native form ("\Device\HarddiskVolumeXX\..."),
// return in |drive_letter_path| the equivalent path that starts with
// a drive letter ("C:\...").  Return false if no such path exists.
bool DevicePathToDriveLetterPath(const FilePath& device_path,
                                 FilePath* drive_letter_path);

// Given an existing file in |path|, set |real_path| to the path
// in native NT format, of the form "\Device\HarddiskVolumeXX\..".
// Returns false if the path can not be found. Empty files cannot
// be resolved with this function.
bool NormalizeToNativeFilePath(const FilePath& path, FilePath* nt_path);
#endif

// This function will return if the given file is a symlink or not.
bool IsLink(const FilePath& file_path);

// Returns information about the given file path.
bool GetFileInfo(const FilePath& file_path, File::Info* info);

// Wrapper for fopen-like calls. Returns non-NULL FILE* on success. The
// underlying file descriptor (POSIX) or handle (Windows) is unconditionally
// configured to not be propagated to child processes.
FILE* OpenFile(const FilePath& filename, const char* mode);

// Closes file opened by OpenFile. Returns true on success.
bool CloseFile(FILE* file);

// Associates a standard FILE stream with an existing File. Note that this
// functions take ownership of the existing File.
FILE* FileToFILE(File file, const char* mode);

// Truncates an open file to end at the location of the current file pointer.
// This is a cross-platform analog to Windows' SetEndOfFile() function.
bool TruncateFile(FILE* file);

// Reads at most the given number of bytes from the file into the buffer.
// Returns the number of read bytes, or -1 on error.
int ReadFile(const FilePath& filename, char* data, int max_size);

// Writes the given buffer into the file, overwriting any data that was
// previously there.  Returns the number of bytes written, or -1 on error.
int WriteFile(const FilePath& filename, const char* data, int size);

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
// Appends |data| to |fd|. Does not close |fd| when done.  Returns true iff
// |size| bytes of |data| were written to |fd|.
bool WriteFileDescriptor(const int fd, const char* data, int size);
#endif

// Appends |data| to |filename|.  Returns true iff |size| bytes of |data| were
// written to |filename|.
bool AppendToFile(const FilePath& filename, const char* data, int size);

// Gets the current working directory for the process.
bool GetCurrentDirectory(FilePath* path);

// Sets the current working directory for the process.
bool SetCurrentDirectory(const FilePath& path);

// Attempts to find a number that can be appended to the |path| to make it
// unique. If |path| does not exist, 0 is returned.  If it fails to find such
// a number, -1 is returned. If |suffix| is not empty, also checks the
// existence of it with the given suffix.
int GetUniquePathNumber(const FilePath& path,
                        const FilePath::StringType& suffix);

// Sets the given |fd| to non-blocking mode.
// Returns true if it was able to set it in the non-blocking mode, otherwise
// false.
bool SetNonBlocking(int fd);

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
// Creates a non-blocking, close-on-exec pipe.
// This creates a non-blocking pipe that is not intended to be shared with any
// child process. This will be done atomically if the operating system supports
// it. Returns true if it was able to create the pipe, otherwise false.
bool CreateLocalNonBlockingPipe(int fds[2]);

// Sets the given |fd| to close-on-exec mode.
// Returns true if it was able to set it in the close-on-exec mode, otherwise
// false.
bool SetCloseOnExec(int fd);

// Test that |path| can only be changed by a given user and members of
// a given set of groups.
// Specifically, test that all parts of |path| under (and including) |base|:
// * Exist.
// * Are owned by a specific user.
// * Are not writable by all users.
// * Are owned by a member of a given set of groups, or are not writable by
//   their group.
// * Are not symbolic links.
// This is useful for checking that a config file is administrator-controlled.
// |base| must contain |path|.
bool VerifyPathControlledByUser(const base::FilePath& base,
                                const base::FilePath& path,
                                uid_t owner_uid,
                                const std::set<gid_t>& group_gids);
#endif  // defined(OS_POSIX) || defined(OS_FUCHSIA)

#if defined(OS_MACOSX) && !defined(OS_IOS)
// Is |path| writable only by a user with administrator privileges?
// This function uses Mac OS conventions.  The super user is assumed to have
// uid 0, and the administrator group is assumed to be named "admin".
// Testing that |path|, and every parent directory including the root of
// the filesystem, are owned by the superuser, controlled by the group
// "admin", are not writable by all users, and contain no symbolic links.
// Will return false if |path| does not exist.
bool VerifyPathControlledByAdmin(const base::FilePath& path);
#endif  // defined(OS_MACOSX) && !defined(OS_IOS)

// Returns the maximum length of path component on the volume containing
// the directory |path|, in the number of FilePath::CharType, or -1 on failure.
int GetMaximumPathComponentLength(const base::FilePath& path);

#if defined(OS_LINUX) || defined(OS_AIX)
// Broad categories of file systems as returned by statfs() on Linux.
enum FileSystemType {
  FILE_SYSTEM_UNKNOWN,   // statfs failed.
  FILE_SYSTEM_0,         // statfs.f_type == 0 means unknown, may indicate AFS.
  FILE_SYSTEM_ORDINARY,  // on-disk filesystem like ext2
  FILE_SYSTEM_NFS,
  FILE_SYSTEM_SMB,
  FILE_SYSTEM_CODA,
  FILE_SYSTEM_MEMORY,  // in-memory file system
  FILE_SYSTEM_CGROUP,  // cgroup control.
  FILE_SYSTEM_OTHER,   // any other value.
  FILE_SYSTEM_TYPE_COUNT
};

// Attempts determine the FileSystemType for |path|.
// Returns false if |path| doesn't exist.
bool GetFileSystemType(const FilePath& path, FileSystemType* type);
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
// Get a temporary directory for shared memory files. The directory may depend
// on whether the destination is intended for executable files, which in turn
// depends on how /dev/shmem was mounted. As a result, you must supply whether
// you intend to create executable shmem segments so this function can find
// an appropriate location.
bool GetShmemTempDir(bool executable, FilePath* path);
#endif

}  // namespace base

#endif  // BASE_FILES_FILE_UTIL_H_
