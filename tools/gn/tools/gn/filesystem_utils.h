// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FILESYSTEM_UTILS_H_
#define TOOLS_GN_FILESYSTEM_UTILS_H_

#include <stddef.h>

#include <string>

#include "base/files/file_path.h"
#include "base/strings/string_piece.h"
#include "tools/gn/settings.h"
#include "tools/gn/target.h"

class Err;

std::string FilePathToUTF8(const base::FilePath::StringType& str);
inline std::string FilePathToUTF8(const base::FilePath& path) {
  return FilePathToUTF8(path.value());
}
base::FilePath UTF8ToFilePath(const base::StringPiece& sp);

// Extensions -----------------------------------------------------------------

// Returns the index of the extension (character after the last dot not after a
// slash). Returns std::string::npos if not found. Returns path.size() if the
// file ends with a dot.
size_t FindExtensionOffset(const std::string& path);

// Returns a string piece pointing into the input string identifying the
// extension. Note that the input pointer must outlive the output.
base::StringPiece FindExtension(const std::string* path);

// Filename parts -------------------------------------------------------------

// Returns the offset of the character following the last slash, or
// 0 if no slash was found. Returns path.size() if the path ends with a slash.
// Note that the input pointer must outlive the output.
size_t FindFilenameOffset(const std::string& path);

// Returns a string piece pointing into the input string identifying the
// file name (following the last slash, including the extension). Note that the
// input pointer must outlive the output.
base::StringPiece FindFilename(const std::string* path);

// Like FindFilename but does not include the extension.
base::StringPiece FindFilenameNoExtension(const std::string* path);

// Removes everything after the last slash. The last slash, if any, will be
// preserved.
void RemoveFilename(std::string* path);

// Returns if the given character is a slash. This allows both slashes and
// backslashes for consistency between Posix and Windows (as opposed to
// FilePath::IsSeparator which is based on the current platform).
inline bool IsSlash(const char ch) {
  return ch == '/' || ch == '\\';
}

// Returns true if the given path ends with a slash.
bool EndsWithSlash(const std::string& s);

// Path parts -----------------------------------------------------------------

// Returns a string piece pointing into the input string identifying the
// directory name of the given path, including the last slash. Note that the
// input pointer must outlive the output.
base::StringPiece FindDir(const std::string* path);

// Returns the substring identifying the last component of the dir, or the
// empty substring if none. For example "//foo/bar/" -> "bar".
base::StringPiece FindLastDirComponent(const SourceDir& dir);

// Returns true if the given string is in the given output dir. This is pretty
// stupid and doesn't handle "." and "..", etc., it is designed for a sanity
// check to keep people from writing output files to the source directory
// accidentally.
bool IsStringInOutputDir(const SourceDir& output_dir, const std::string& str);

// Verifies that the given string references a file inside of the given
// directory. This just uses IsStringInOutputDir above.
//
// The origin will be blamed in the error.
//
// If the file isn't in the dir, returns false and sets the error. Otherwise
// returns true and leaves the error untouched.
bool EnsureStringIsInOutputDir(const SourceDir& output_dir,
                               const std::string& str,
                               const ParseNode* origin,
                               Err* err);

// ----------------------------------------------------------------------------

// Returns true if the input string is absolute. Double-slashes at the
// beginning are treated as source-relative paths. On Windows, this handles
// paths of both the native format: "C:/foo" and ours "/C:/foo"
bool IsPathAbsolute(const base::StringPiece& path);

// Returns true if the input string is source-absolute. Source-absolute
// paths begin with two forward slashes and resolve as if they are
// relative to the source root.
bool IsPathSourceAbsolute(const base::StringPiece& path);

// Given an absolute path, checks to see if is it is inside the source root.
// If it is, fills a source-absolute path into the given output and returns
// true. If it isn't, clears the dest and returns false.
//
// The source_root should be a base::FilePath converted to UTF-8. On Windows,
// it should begin with a "C:/" rather than being our SourceFile's style
// ("/C:/"). The source root can end with a slash or not.
//
// Note that this does not attempt to normalize slashes in the output.
bool MakeAbsolutePathRelativeIfPossible(const base::StringPiece& source_root,
                                        const base::StringPiece& path,
                                        std::string* dest);

// Given two absolute paths |base| and |target|, returns a relative path to
// |target| as if the current directory was |base|.  The relative path returned
// is minimal.  For example, if "../../a/b/" and "../b" are both valid, then the
// latter will be returned.  On Windows, it's impossible to have a relative path
// from C:\foo to D:\bar, so the absolute path |target| is returned instead for
// this case.
base::FilePath MakeAbsoluteFilePathRelativeIfPossible(
    const base::FilePath& base,
    const base::FilePath& target);

// Collapses "." and sequential "/"s and evaluates "..". |path| may be
// system-absolute, source-absolute, or relative. If |path| is source-absolute
// and |source_root| is non-empty, |path| may be system absolute after this
// function returns, if |path| references the filesystem outside of
// |source_root| (ex. path = "//.."). In this case on Windows, |path| will have
// a leading slash. Otherwise, |path| will retain its relativity. |source_root|
// must not end with a slash.
void NormalizePath(std::string* path,
                   const base::StringPiece& source_root = base::StringPiece());

// Converts slashes to backslashes for Windows. Keeps the string unchanged
// for other systems.
void ConvertPathToSystem(std::string* path);

// Takes a path, |input|, and makes it relative to the given directory
// |dest_dir|. Both inputs may be source-relative (e.g. begins with
// with "//") or may be absolute.
//
// If supplied, the |source_root| parameter is the absolute path to
// the source root and not end in a slash. Unless you know that the
// inputs are always source relative, this should be supplied.
std::string RebasePath(
    const std::string& input,
    const SourceDir& dest_dir,
    const base::StringPiece& source_root = base::StringPiece());

// Resolves a file or dir name (parameter input) relative to
// value directory. Will return an empty SourceDir/File on error
// and set the give *err pointer (required). Empty input is always an error.
// Returned value can be used to set value in either SourceFile or SourceDir
// (based on as_file parameter).
//
// Parameter as_file defines whether result path will look like a file path
// or it should be treated as a directory (contains "/" and the end
// of the string).
//
// If source_root is supplied, these functions will additionally handle the
// case where the input is a system-absolute but still inside the source
// tree. This is the case for some external tools.
template <typename StringType>
std::string ResolveRelative(const StringType& input,
                            const std::string& value,
                            bool as_file,
                            const base::StringPiece& source_root);

// Resolves source file or directory relative to some given source root. Returns
// an empty file path on error.
base::FilePath ResolvePath(const std::string& value,
                           bool as_file,
                           const base::FilePath& source_root);

// Returns the given directory with no terminating slash at the end, such that
// appending a slash and more stuff will produce a valid path.
//
// If the directory refers to either the source or system root, we'll append
// a "." so this remains valid.
std::string DirectoryWithNoLastSlash(const SourceDir& dir);

// Returns the "best" SourceDir representing the given path. If it's inside the
// given source_root, a source-relative directory will be returned (e.g.
// "//foo/bar.cc". If it's outside of the source root or the source root is
// empty, a system-absolute directory will be returned.
SourceDir SourceDirForPath(const base::FilePath& source_root,
                           const base::FilePath& path);

// Like SourceDirForPath but returns the SourceDir representing the current
// directory.
SourceDir SourceDirForCurrentDirectory(const base::FilePath& source_root);

// Given the label of a toolchain and whether that toolchain is the default
// toolchain, returns the name of the subdirectory for that toolchain's
// output. This will be the empty string to indicate that the toolchain outputs
// go in the root build directory. Otherwise, the result will end in a slash.
std::string GetOutputSubdirName(const Label& toolchain_label, bool is_default);

// Returns true if the contents of the file and stream given are equal, false
// otherwise.
bool ContentsEqual(const base::FilePath& file_path, const std::string& data);

// Writes given stream contents to the given file if it differs from existing
// file contents. Returns true if new contents was successfully written or
// existing file contents doesn't need updating, false on write error. |err| is
// set on write error if not nullptr.
bool WriteFileIfChanged(const base::FilePath& file_path,
                        const std::string& data,
                        Err* err);

// Writes given stream contents to the given file. Returns true if data was
// successfully written, false otherwise. |err| is set on error if not nullptr.
bool WriteFile(const base::FilePath& file_path,
               const std::string& data,
               Err* err);

// -----------------------------------------------------------------------------

enum class BuildDirType {
  // Returns the root toolchain dir rather than the generated or output
  // subdirectories. This is valid only for the toolchain directory getters.
  // Asking for this for a target or source dir makes no sense.
  TOOLCHAIN_ROOT,

  // Generated file directory.
  GEN,

  // Output file directory.
  OBJ,
};

// In different contexts, different information is known about the toolchain in
// question. If you have a Target or settings object, everything can be
// extracted from there. But when querying label information on something in
// another toolchain, for example, the only thing known (it may not even exist)
// is the toolchain label string and whether it matches the default toolchain.
//
// This object extracts the relevant information from a variety of input
// types for the convenience of the caller.
class BuildDirContext {
 public:
  // Extracts toolchain information associated with the given target.
  explicit BuildDirContext(const Target* target);

  // Extracts toolchain information associated with the given settings object.
  explicit BuildDirContext(const Settings* settings);

  // Extrats toolchain information from the current toolchain of the scope.
  explicit BuildDirContext(const Scope* execution_scope);

  // Extracts the default toolchain information from the given execution
  // scope. The toolchain you want to query must be passed in. This doesn't
  // use the settings object from the Scope so one can query other toolchains.
  // If you want to use the scope's current toolchain, use the version above.
  BuildDirContext(const Scope* execution_scope, const Label& toolchain_label);

  // Specify all information manually.
  BuildDirContext(const BuildSettings* build_settings,
                  const Label& toolchain_label,
                  bool is_default_toolchain);

  const BuildSettings* build_settings;
  const Label& toolchain_label;
  bool is_default_toolchain;
};

// Returns the root, object, or generated file directory for the toolchain.
//
// The toolchain object file root is never exposed in GN (there is no
// root_obj_dir variable) so BuildDirType::OBJ would normally never be passed
// to this function except when it's called by one of the variants below that
// append paths to it.
SourceDir GetBuildDirAsSourceDir(const BuildDirContext& context,
                                 BuildDirType type);
OutputFile GetBuildDirAsOutputFile(const BuildDirContext& context,
                                   BuildDirType type);

// Returns the output or generated file directory corresponding to the given
// source directory.
SourceDir GetSubBuildDirAsSourceDir(const BuildDirContext& context,
                                    const SourceDir& source_dir,
                                    BuildDirType type);
OutputFile GetSubBuildDirAsOutputFile(const BuildDirContext& context,
                                      const SourceDir& source_dir,
                                      BuildDirType type);

// Returns the output or generated file directory corresponding to the given
// target.
SourceDir GetBuildDirForTargetAsSourceDir(const Target* target,
                                          BuildDirType type);
OutputFile GetBuildDirForTargetAsOutputFile(const Target* target,
                                            BuildDirType type);

// Returns the scope's current directory.
SourceDir GetScopeCurrentBuildDirAsSourceDir(const Scope* scope,
                                             BuildDirType type);
// Lack of OutputDir version is due only to it not currently being needed,
// please add one if you need it.

#endif  // TOOLS_GN_FILESYSTEM_UTILS_H_
