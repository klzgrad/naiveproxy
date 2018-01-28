// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/filesystem_utils.h"

#include <algorithm>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "tools/gn/location.h"
#include "tools/gn/settings.h"
#include "tools/gn/source_dir.h"

namespace {

enum DotDisposition {
  // The given dot is just part of a filename and is not special.
  NOT_A_DIRECTORY,

  // The given dot is the current directory.
  DIRECTORY_CUR,

  // The given dot is the first of a double dot that should take us up one.
  DIRECTORY_UP
};

// When we find a dot, this function is called with the character following
// that dot to see what it is. The return value indicates what type this dot is
// (see above). This code handles the case where the dot is at the end of the
// input.
//
// |*consumed_len| will contain the number of characters in the input that
// express what we found.
DotDisposition ClassifyAfterDot(const std::string& path,
                                size_t after_dot,
                                size_t* consumed_len) {
  if (after_dot == path.size()) {
    // Single dot at the end.
    *consumed_len = 1;
    return DIRECTORY_CUR;
  }
  if (IsSlash(path[after_dot])) {
    // Single dot followed by a slash.
    *consumed_len = 2;  // Consume the slash
    return DIRECTORY_CUR;
  }

  if (path[after_dot] == '.') {
    // Two dots.
    if (after_dot + 1 == path.size()) {
      // Double dot at the end.
      *consumed_len = 2;
      return DIRECTORY_UP;
    }
    if (IsSlash(path[after_dot + 1])) {
      // Double dot folowed by a slash.
      *consumed_len = 3;
      return DIRECTORY_UP;
    }
  }

  // The dots are followed by something else, not a directory.
  *consumed_len = 1;
  return NOT_A_DIRECTORY;
}

#if defined(OS_WIN)
inline char NormalizeWindowsPathChar(char c) {
  if (c == '/')
    return '\\';
  return base::ToLowerASCII(c);
}

// Attempts to do a case and slash-insensitive comparison of two 8-bit Windows
// paths.
bool AreAbsoluteWindowsPathsEqual(const base::StringPiece& a,
                                  const base::StringPiece& b) {
  if (a.size() != b.size())
    return false;

  // For now, just do a case-insensitive ASCII comparison. We could convert to
  // UTF-16 and use ICU if necessary.
  for (size_t i = 0; i < a.size(); i++) {
    if (NormalizeWindowsPathChar(a[i]) != NormalizeWindowsPathChar(b[i]))
      return false;
  }
  return true;
}

bool DoesBeginWindowsDriveLetter(const base::StringPiece& path) {
  if (path.size() < 3)
    return false;

  // Check colon first, this will generally fail fastest.
  if (path[1] != ':')
    return false;

  // Check drive letter.
  if (!base::IsAsciiAlpha(path[0]))
    return false;

  if (!IsSlash(path[2]))
    return false;
  return true;
}
#endif

// A wrapper around FilePath.GetComponents that works the way we need. This is
// not super efficient since it does some O(n) transformations on the path. If
// this is called a lot, we might want to optimize.
std::vector<base::FilePath::StringType> GetPathComponents(
    const base::FilePath& path) {
  std::vector<base::FilePath::StringType> result;
  path.GetComponents(&result);

  if (result.empty())
    return result;

  // GetComponents will preserve the "/" at the beginning, which confuses us.
  // We don't expect to have relative paths in this function.
  // Don't use IsSeparator since we always want to allow backslashes.
  if (result[0] == FILE_PATH_LITERAL("/") ||
      result[0] == FILE_PATH_LITERAL("\\"))
    result.erase(result.begin());

#if defined(OS_WIN)
  // On Windows, GetComponents will give us [ "C:", "/", "foo" ], and we
  // don't want the slash in there. This doesn't support input like "C:foo"
  // which means foo relative to the current directory of the C drive but
  // that's basically legacy DOS behavior we don't need to support.
  if (result.size() >= 2 && result[1].size() == 1 &&
      IsSlash(static_cast<char>(result[1][0])))
    result.erase(result.begin() + 1);
#endif

  return result;
}

// Provides the equivalent of == for filesystem strings, trying to do
// approximately the right thing with case.
bool FilesystemStringsEqual(const base::FilePath::StringType& a,
                            const base::FilePath::StringType& b) {
#if defined(OS_WIN)
  // Assume case-insensitive filesystems on Windows. We use the CompareString
  // function to do a case-insensitive comparison based on the current locale
  // (we don't want GN to depend on ICU which is large and requires data
  // files). This isn't perfect, but getting this perfectly right is very
  // difficult and requires I/O, and this comparison should cover 99.9999% of
  // all cases.
  //
  // Note: The documentation for CompareString says it runs fastest on
  // null-terminated strings with -1 passed for the length, so we do that here.
  // There should not be embedded nulls in filesystem strings.
  return ::CompareString(LOCALE_USER_DEFAULT, LINGUISTIC_IGNORECASE,
                         a.c_str(), -1, b.c_str(), -1) == CSTR_EQUAL;
#else
  // Assume case-sensitive filesystems on non-Windows.
  return a == b;
#endif
}

// Helper function for computing subdirectories in the build directory
// corresponding to absolute paths. This will try to resolve the absolute
// path as a source-relative path first, and otherwise it creates a
// special subdirectory for absolute paths to keep them from colliding with
// other generated sources and outputs.
void AppendFixedAbsolutePathSuffix(const BuildSettings* build_settings,
                                   const SourceDir& source_dir,
                                   OutputFile* result) {
  const std::string& build_dir = build_settings->build_dir().value();

  if (base::StartsWith(source_dir.value(), build_dir,
                       base::CompareCase::SENSITIVE)) {
    size_t build_dir_size = build_dir.size();
    result->value().append(&source_dir.value()[build_dir_size],
                           source_dir.value().size() - build_dir_size);
  } else {
    result->value().append("ABS_PATH");
#if defined(OS_WIN)
    // Windows absolute path contains ':' after drive letter. Remove it to
    // avoid inserting ':' in the middle of path (eg. "ABS_PATH/C:/").
    std::string src_dir_value = source_dir.value();
    const auto colon_pos = src_dir_value.find(':');
    if (colon_pos != std::string::npos)
      src_dir_value.erase(src_dir_value.begin() + colon_pos);
#else
    const std::string& src_dir_value = source_dir.value();
#endif
    result->value().append(src_dir_value);
  }
}

}  // namespace

std::string FilePathToUTF8(const base::FilePath::StringType& str) {
#if defined(OS_WIN)
  return base::WideToUTF8(str);
#else
  return str;
#endif
}

base::FilePath UTF8ToFilePath(const base::StringPiece& sp) {
#if defined(OS_WIN)
  return base::FilePath(base::UTF8ToWide(sp));
#else
  return base::FilePath(sp.as_string());
#endif
}

size_t FindExtensionOffset(const std::string& path) {
  for (int i = static_cast<int>(path.size()); i >= 0; i--) {
    if (IsSlash(path[i]))
      break;
    if (path[i] == '.')
      return i + 1;
  }
  return std::string::npos;
}

base::StringPiece FindExtension(const std::string* path) {
  size_t extension_offset = FindExtensionOffset(*path);
  if (extension_offset == std::string::npos)
    return base::StringPiece();
  return base::StringPiece(&path->data()[extension_offset],
                           path->size() - extension_offset);
}

size_t FindFilenameOffset(const std::string& path) {
  for (int i = static_cast<int>(path.size()) - 1; i >= 0; i--) {
    if (IsSlash(path[i]))
      return i + 1;
  }
  return 0;  // No filename found means everything was the filename.
}

base::StringPiece FindFilename(const std::string* path) {
  size_t filename_offset = FindFilenameOffset(*path);
  if (filename_offset == 0)
    return base::StringPiece(*path);  // Everything is the file name.
  return base::StringPiece(&(*path).data()[filename_offset],
                           path->size() - filename_offset);
}

base::StringPiece FindFilenameNoExtension(const std::string* path) {
  if (path->empty())
    return base::StringPiece();
  size_t filename_offset = FindFilenameOffset(*path);
  size_t extension_offset = FindExtensionOffset(*path);

  size_t name_len;
  if (extension_offset == std::string::npos)
    name_len = path->size() - filename_offset;
  else
    name_len = extension_offset - filename_offset - 1;

  return base::StringPiece(&(*path).data()[filename_offset], name_len);
}

void RemoveFilename(std::string* path) {
  path->resize(FindFilenameOffset(*path));
}

bool EndsWithSlash(const std::string& s) {
  return !s.empty() && IsSlash(s[s.size() - 1]);
}

base::StringPiece FindDir(const std::string* path) {
  size_t filename_offset = FindFilenameOffset(*path);
  if (filename_offset == 0u)
    return base::StringPiece();
  return base::StringPiece(path->data(), filename_offset);
}

base::StringPiece FindLastDirComponent(const SourceDir& dir) {
  const std::string& dir_string = dir.value();

  if (dir_string.empty())
    return base::StringPiece();
  int cur = static_cast<int>(dir_string.size()) - 1;
  DCHECK(dir_string[cur] == '/');
  int end = cur;
  cur--;  // Skip before the last slash.

  for (; cur >= 0; cur--) {
    if (dir_string[cur] == '/')
      return base::StringPiece(&dir_string[cur + 1], end - cur - 1);
  }
  return base::StringPiece(&dir_string[0], end);
}

bool IsStringInOutputDir(const SourceDir& output_dir, const std::string& str) {
  // This check will be wrong for all proper prefixes "e.g. "/output" will
  // match "/out" but we don't really care since this is just a sanity check.
  const std::string& dir_str = output_dir.value();
  return str.compare(0, dir_str.length(), dir_str) == 0;
}

bool EnsureStringIsInOutputDir(const SourceDir& output_dir,
                               const std::string& str,
                               const ParseNode* origin,
                               Err* err) {
  if (IsStringInOutputDir(output_dir, str))
    return true;  // Output directory is hardcoded.

  *err = Err(origin, "File is not inside output directory.",
      "The given file should be in the output directory. Normally you would "
      "specify\n\"$target_out_dir/foo\" or "
      "\"$target_gen_dir/foo\". I interpreted this as\n\""
      + str + "\".");
  return false;
}

bool IsPathAbsolute(const base::StringPiece& path) {
  if (path.empty())
    return false;

  if (!IsSlash(path[0])) {
#if defined(OS_WIN)
    // Check for Windows system paths like "C:\foo".
    if (path.size() > 2 && path[1] == ':' && IsSlash(path[2]))
      return true;
#endif
    return false;  // Doesn't begin with a slash, is relative.
  }

  // Double forward slash at the beginning means source-relative (we don't
  // allow backslashes for denoting this).
  if (path.size() > 1 && path[1] == '/')
    return false;

  return true;
}

bool IsPathSourceAbsolute(const base::StringPiece& path) {
  return (path.size() >= 2 && path[0] == '/' && path[1] == '/');
}

bool MakeAbsolutePathRelativeIfPossible(const base::StringPiece& source_root,
                                        const base::StringPiece& path,
                                        std::string* dest) {
  DCHECK(IsPathAbsolute(source_root));
  DCHECK(IsPathAbsolute(path));

  dest->clear();

  if (source_root.size() > path.size())
    return false;  // The source root is longer: the path can never be inside.

#if defined(OS_WIN)
  // Source root should be canonical on Windows. Note that the initial slash
  // must be forward slash, but that the other ones can be either forward or
  // backward.
  DCHECK(source_root.size() > 2 && source_root[0] != '/' &&
         source_root[1] == ':' && IsSlash(source_root[2]));

  size_t after_common_index = std::string::npos;
  if (DoesBeginWindowsDriveLetter(path)) {
    // Handle "C:\foo"
    if (AreAbsoluteWindowsPathsEqual(source_root,
                                     path.substr(0, source_root.size())))
      after_common_index = source_root.size();
    else
      return false;
  } else if (path[0] == '/' && source_root.size() <= path.size() - 1 &&
             DoesBeginWindowsDriveLetter(path.substr(1))) {
    // Handle "/C:/foo"
    if (AreAbsoluteWindowsPathsEqual(source_root,
                                     path.substr(1, source_root.size())))
      after_common_index = source_root.size() + 1;
    else
      return false;
  } else {
    return false;
  }

  // If we get here, there's a match and after_common_index identifies the
  // part after it.

  // The base may or may not have a trailing slash, so skip all slashes from
  // the path after our prefix match.
  size_t first_after_slash = after_common_index;
  while (first_after_slash < path.size() && IsSlash(path[first_after_slash]))
    first_after_slash++;

  dest->assign("//");  // Result is source root relative.
  dest->append(&path.data()[first_after_slash],
               path.size() - first_after_slash);
  return true;

#else

  // On non-Windows this is easy. Since we know both are absolute, just do a
  // prefix check.
  if (path.substr(0, source_root.size()) == source_root) {
    // The base may or may not have a trailing slash, so skip all slashes from
    // the path after our prefix match.
    size_t first_after_slash = source_root.size();
    while (first_after_slash < path.size() && IsSlash(path[first_after_slash]))
      first_after_slash++;

    dest->assign("//");  // Result is source root relative.
    dest->append(&path.data()[first_after_slash],
                 path.size() - first_after_slash);
    return true;
  }
  return false;
#endif
}

void NormalizePath(std::string* path, const base::StringPiece& source_root) {
  char* pathbuf = path->empty() ? nullptr : &(*path)[0];

  // top_index is the first character we can modify in the path. Anything
  // before this indicates where the path is relative to.
  size_t top_index = 0;
  bool is_relative = true;
  if (!path->empty() && pathbuf[0] == '/') {
    is_relative = false;

    if (path->size() > 1 && pathbuf[1] == '/') {
      // Two leading slashes, this is a path into the source dir.
      top_index = 2;
    } else {
      // One leading slash, this is a system-absolute path.
      top_index = 1;
    }
  }

  size_t dest_i = top_index;
  for (size_t src_i = top_index; src_i < path->size(); /* nothing */) {
    if (pathbuf[src_i] == '.') {
      if (src_i == 0 || IsSlash(pathbuf[src_i - 1])) {
        // Slash followed by a dot, see if it's something special.
        size_t consumed_len;
        switch (ClassifyAfterDot(*path, src_i + 1, &consumed_len)) {
          case NOT_A_DIRECTORY:
            // Copy the dot to the output, it means nothing special.
            pathbuf[dest_i++] = pathbuf[src_i++];
            break;
          case DIRECTORY_CUR:
            // Current directory, just skip the input.
            src_i += consumed_len;
            break;
          case DIRECTORY_UP:
            // Back up over previous directory component. If we're already
            // at the top, preserve the "..".
            if (dest_i > top_index) {
              // The previous char was a slash, remove it.
              dest_i--;
            }

            if (dest_i == top_index) {
              if (is_relative) {
                // We're already at the beginning of a relative input, copy the
                // ".." and continue. We need the trailing slash if there was
                // one before (otherwise we're at the end of the input).
                pathbuf[dest_i++] = '.';
                pathbuf[dest_i++] = '.';
                if (consumed_len == 3)
                  pathbuf[dest_i++] = '/';

                // This also makes a new "root" that we can't delete by going
                // up more levels.  Otherwise "../.." would collapse to
                // nothing.
                top_index = dest_i;
              } else if (top_index == 2 && !source_root.empty()) {
                // |path| was passed in as a source-absolute path. Prepend
                // |source_root| to make |path| absolute. |source_root| must not
                // end with a slash unless we are at root.
                DCHECK(source_root.size() == 1u ||
                       !IsSlash(source_root[source_root.size() - 1u]));
                size_t source_root_len = source_root.size();

#if defined(OS_WIN)
                // On Windows, if the source_root does not start with a slash,
                // append one here for consistency.
                if (!IsSlash(source_root[0])) {
                  path->insert(0, "/" + source_root.as_string());
                  source_root_len++;
                } else {
                  path->insert(0, source_root.data(), source_root_len);
                }

                // Normalize slashes in source root portion.
                for (size_t i = 0; i < source_root_len; ++i) {
                  if ((*path)[i] == '\\')
                    (*path)[i] = '/';
                }
#else
                path->insert(0, source_root.data(), source_root_len);
#endif

                // |path| is now absolute, so |top_index| is 1. |dest_i| and
                // |src_i| should be incremented to keep the same relative
                // position. Comsume the leading "//" by decrementing |dest_i|.
                top_index = 1;
                pathbuf = &(*path)[0];
                dest_i += source_root_len - 2;
                src_i += source_root_len;

                // Just find the previous slash or the beginning of input.
                while (dest_i > 0 && !IsSlash(pathbuf[dest_i - 1]))
                  dest_i--;
              }
              // Otherwise we're at the beginning of a system-absolute path, or
              // a source-absolute path for which we don't know the absolute
              // path. Don't allow ".." to go up another level, and just eat it.
            } else {
              // Just find the previous slash or the beginning of input.
              while (dest_i > 0 && !IsSlash(pathbuf[dest_i - 1]))
                dest_i--;
            }
            src_i += consumed_len;
        }
      } else {
        // Dot not preceeded by a slash, copy it literally.
        pathbuf[dest_i++] = pathbuf[src_i++];
      }
    } else if (IsSlash(pathbuf[src_i])) {
      if (src_i > 0 && IsSlash(pathbuf[src_i - 1])) {
        // Two slashes in a row, skip over it.
        src_i++;
      } else {
        // Just one slash, copy it, normalizing to foward slash.
        pathbuf[dest_i] = '/';
        dest_i++;
        src_i++;
      }
    } else {
      // Input nothing special, just copy it.
      pathbuf[dest_i++] = pathbuf[src_i++];
    }
  }
  path->resize(dest_i);
}

void ConvertPathToSystem(std::string* path) {
#if defined(OS_WIN)
  for (size_t i = 0; i < path->size(); i++) {
    if ((*path)[i] == '/')
      (*path)[i] = '\\';
  }
#endif
}

std::string MakeRelativePath(const std::string& input,
                             const std::string& dest) {
#if defined(OS_WIN)
  // Make sure that absolute |input| path starts with a slash if |dest| path
  // does. Otherwise skipping common prefixes won't work properly. Ensure the
  // same for |dest| path too.
  if (IsPathAbsolute(input) && !IsSlash(input[0]) && IsSlash(dest[0])) {
    std::string corrected_input(1, dest[0]);
    corrected_input.append(input);
    return MakeRelativePath(corrected_input, dest);
  }
  if (IsPathAbsolute(dest) && !IsSlash(dest[0]) && IsSlash(input[0])) {
    std::string corrected_dest(1, input[0]);
    corrected_dest.append(dest);
    return MakeRelativePath(input, corrected_dest);
  }

  // Make sure that both absolute paths use the same drive letter case.
  if (IsPathAbsolute(input) && IsPathAbsolute(dest) && input.size() > 1 &&
      dest.size() > 1) {
    int letter_pos = base::IsAsciiAlpha(input[0]) ? 0 : 1;
    if (input[letter_pos] != dest[letter_pos] &&
        base::ToUpperASCII(input[letter_pos]) ==
            base::ToUpperASCII(dest[letter_pos])) {
      std::string corrected_input = input;
      corrected_input[letter_pos] = dest[letter_pos];
      return MakeRelativePath(corrected_input, dest);
    }
  }
#endif

  std::string ret;

  // Skip the common prefixes of the source and dest as long as they end in
  // a [back]slash.
  size_t common_prefix_len = 0;
  size_t max_common_length = std::min(input.size(), dest.size());
  for (size_t i = common_prefix_len; i < max_common_length; i++) {
    if (IsSlash(input[i]) && IsSlash(dest[i]))
      common_prefix_len = i + 1;
    else if (input[i] != dest[i])
      break;
  }

  // Invert the dest dir starting from the end of the common prefix.
  for (size_t i = common_prefix_len; i < dest.size(); i++) {
    if (IsSlash(dest[i]))
      ret.append("../");
  }

  // Append any remaining unique input.
  ret.append(&input[common_prefix_len], input.size() - common_prefix_len);

  // If the result is still empty, the paths are the same.
  if (ret.empty())
    ret.push_back('.');

  return ret;
}

std::string RebasePath(const std::string& input,
                       const SourceDir& dest_dir,
                       const base::StringPiece& source_root) {
  std::string ret;
  DCHECK(source_root.empty() || !source_root.ends_with("/"));

  bool input_is_source_path = (input.size() >= 2 &&
                               input[0] == '/' && input[1] == '/');

  if (!source_root.empty() &&
      (!input_is_source_path || !dest_dir.is_source_absolute())) {
    std::string input_full;
    std::string dest_full;
    if (input_is_source_path) {
      source_root.AppendToString(&input_full);
      input_full.push_back('/');
      input_full.append(input, 2, std::string::npos);
    } else {
      input_full.append(input);
    }
    if (dest_dir.is_source_absolute()) {
      source_root.AppendToString(&dest_full);
      dest_full.push_back('/');
      dest_full.append(dest_dir.value(), 2, std::string::npos);
    } else {
#if defined(OS_WIN)
      // On Windows, SourceDir system-absolute paths start
      // with /, e.g. "/C:/foo/bar".
      const std::string& value = dest_dir.value();
      if (value.size() > 2 && value[2] == ':')
        dest_full.append(dest_dir.value().substr(1));
      else
        dest_full.append(dest_dir.value());
#else
      dest_full.append(dest_dir.value());
#endif
    }
    bool remove_slash = false;
    if (!EndsWithSlash(input_full)) {
      input_full.push_back('/');
      remove_slash = true;
    }
    ret = MakeRelativePath(input_full, dest_full);
    if (remove_slash && ret.size() > 1)
      ret.resize(ret.size() - 1);
    return ret;
  }

  ret = MakeRelativePath(input, dest_dir.value());
  return ret;
}

std::string DirectoryWithNoLastSlash(const SourceDir& dir) {
  std::string ret;

  if (dir.value().empty()) {
    // Just keep input the same.
  } else if (dir.value() == "/") {
    ret.assign("/.");
  } else if (dir.value() == "//") {
    ret.assign("//.");
  } else {
    ret.assign(dir.value());
    ret.resize(ret.size() - 1);
  }
  return ret;
}

SourceDir SourceDirForPath(const base::FilePath& source_root,
                           const base::FilePath& path) {
  std::vector<base::FilePath::StringType> source_comp =
      GetPathComponents(source_root);
  std::vector<base::FilePath::StringType> path_comp =
      GetPathComponents(path);

  // See if path is inside the source root by looking for each of source root's
  // components at the beginning of path.
  bool is_inside_source;
  if (path_comp.size() < source_comp.size() || source_root.empty()) {
    // Too small to fit.
    is_inside_source = false;
  } else {
    is_inside_source = true;
    for (size_t i = 0; i < source_comp.size(); i++) {
      if (!FilesystemStringsEqual(source_comp[i], path_comp[i])) {
        is_inside_source = false;
        break;
      }
    }
  }

  std::string result_str;
  size_t initial_path_comp_to_use;
  if (is_inside_source) {
    // Construct a source-relative path beginning in // and skip all of the
    // shared directories.
    result_str = "//";
    initial_path_comp_to_use = source_comp.size();
  } else {
    // Not inside source code, construct a system-absolute path.
    result_str = "/";
    initial_path_comp_to_use = 0;
  }

  for (size_t i = initial_path_comp_to_use; i < path_comp.size(); i++) {
    result_str.append(FilePathToUTF8(path_comp[i]));
    result_str.push_back('/');
  }
  return SourceDir(result_str);
}

SourceDir SourceDirForCurrentDirectory(const base::FilePath& source_root) {
  base::FilePath cd;
  base::GetCurrentDirectory(&cd);
  return SourceDirForPath(source_root, cd);
}

std::string GetOutputSubdirName(const Label& toolchain_label, bool is_default) {
  // The default toolchain has no subdir.
  if (is_default)
    return std::string();

  // For now just assume the toolchain name is always a valid dir name. We may
  // want to clean up the in the future.
  return toolchain_label.name() + "/";
}

bool ContentsEqual(const base::FilePath& file_path, const std::string& data) {
  // Compare file and stream sizes first. Quick and will save us some time if
  // they are different sizes.
  int64_t file_size;
  if (!base::GetFileSize(file_path, &file_size) ||
      static_cast<size_t>(file_size) != data.size()) {
    return false;
  }

  std::string file_data;
  file_data.resize(file_size);
  if (!base::ReadFileToString(file_path, &file_data))
    return false;

  return file_data == data;
}

bool WriteFileIfChanged(const base::FilePath& file_path,
                        const std::string& data,
                        Err* err) {
  if (ContentsEqual(file_path, data))
    return true;

  return WriteFile(file_path, data, err);
}

bool WriteFile(const base::FilePath& file_path, const std::string& data,
               Err* err) {
  // Create the directory if necessary.
  if (!base::CreateDirectory(file_path.DirName())) {
    if (err) {
      *err =
          Err(Location(), "Unable to create directory.",
              "I was using \"" + FilePathToUTF8(file_path.DirName()) + "\".");
    }
    return false;
  }

  int size = static_cast<int>(data.size());
  bool write_success = false;

#if defined(OS_WIN)
  // On Windows, provide a custom implementation of base::WriteFile. Sometimes
  // the base version fails, especially on the bots. The guess is that Windows
  // Defender or other antivirus programs still have the file open (after
  // checking for the read) when the write happens immediately after. This
  // version opens with FILE_SHARE_READ (normally not what you want when
  // replacing the entire contents of the file) which lets us continue even if
  // another program has the file open for reading. See http://crbug.com/468437
  base::win::ScopedHandle file(::CreateFile(file_path.value().c_str(),
                                            GENERIC_WRITE, FILE_SHARE_READ,
                                            NULL, CREATE_ALWAYS, 0, NULL));
  if (file.IsValid()) {
    DWORD written;
    BOOL result = ::WriteFile(file.Get(), data.c_str(), size, &written, NULL);
    if (result) {
      if (static_cast<int>(written) == size) {
        write_success = true;
      } else {
        // Didn't write all the bytes.
        LOG(ERROR) << "wrote" << written << " bytes to "
                   << base::UTF16ToUTF8(file_path.value()) << " expected "
                   << size;
      }
    } else {
      // WriteFile failed.
      PLOG(ERROR) << "writing file " << base::UTF16ToUTF8(file_path.value())
                  << " failed";
    }
  } else {
    PLOG(ERROR) << "CreateFile failed for path "
                << base::UTF16ToUTF8(file_path.value());
  }
#else
  write_success = base::WriteFile(file_path, data.c_str(), size) == size;
#endif

  if (!write_success && err) {
    *err = Err(Location(), "Unable to write file.",
               "I was writing \"" + FilePathToUTF8(file_path) + "\".");
  }

  return write_success;
}

BuildDirContext::BuildDirContext(const Target* target)
    : BuildDirContext(target->settings()) {
}

BuildDirContext::BuildDirContext(const Settings* settings)
    : BuildDirContext(settings->build_settings(),
                      settings->toolchain_label(),
                      settings->is_default()) {
}

BuildDirContext::BuildDirContext(const Scope* execution_scope)
    : BuildDirContext(execution_scope->settings()) {
}

BuildDirContext::BuildDirContext(const Scope* execution_scope,
                                 const Label& toolchain_label)
    : BuildDirContext(execution_scope->settings()->build_settings(),
                      toolchain_label,
                      execution_scope->settings()->default_toolchain_label() ==
                          toolchain_label) {
}

BuildDirContext::BuildDirContext(const BuildSettings* in_build_settings,
                                 const Label& in_toolchain_label,
                                 bool in_is_default_toolchain)
    : build_settings(in_build_settings),
      toolchain_label(in_toolchain_label),
      is_default_toolchain(in_is_default_toolchain) {
}

SourceDir GetBuildDirAsSourceDir(const BuildDirContext& context,
                                 BuildDirType type) {
  return GetBuildDirAsOutputFile(context, type).AsSourceDir(
      context.build_settings);
}

OutputFile GetBuildDirAsOutputFile(const BuildDirContext& context,
                                   BuildDirType type) {
  OutputFile result(GetOutputSubdirName(context.toolchain_label,
                                        context.is_default_toolchain));
  DCHECK(result.value().empty() || result.value().back() == '/');

  if (type == BuildDirType::GEN)
    result.value().append("gen/");
  else if (type == BuildDirType::OBJ)
    result.value().append("obj/");
  return result;
}

SourceDir GetSubBuildDirAsSourceDir(const BuildDirContext& context,
                                    const SourceDir& source_dir,
                                    BuildDirType type) {
  return GetSubBuildDirAsOutputFile(context, source_dir, type)
      .AsSourceDir(context.build_settings);
}

OutputFile GetSubBuildDirAsOutputFile(const BuildDirContext& context,
                                      const SourceDir& source_dir,
                                      BuildDirType type) {
  DCHECK(type != BuildDirType::TOOLCHAIN_ROOT);
  OutputFile result = GetBuildDirAsOutputFile(context, type);

  if (source_dir.is_source_absolute()) {
    // The source dir is source-absolute, so we trim off the two leading
    // slashes to append to the toolchain object directory.
    result.value().append(&source_dir.value()[2],
                          source_dir.value().size() - 2);
  } else {
    // System-absolute.
    AppendFixedAbsolutePathSuffix(context.build_settings, source_dir, &result);
  }
  return result;
}

SourceDir GetBuildDirForTargetAsSourceDir(const Target* target,
                                          BuildDirType type) {
  return GetSubBuildDirAsSourceDir(
      BuildDirContext(target), target->label().dir(), type);
}

OutputFile GetBuildDirForTargetAsOutputFile(const Target* target,
                                            BuildDirType type) {
  return GetSubBuildDirAsOutputFile(
      BuildDirContext(target), target->label().dir(), type);
}

SourceDir GetScopeCurrentBuildDirAsSourceDir(const Scope* scope,
                                             BuildDirType type) {
  if (type == BuildDirType::TOOLCHAIN_ROOT)
    return GetBuildDirAsSourceDir(BuildDirContext(scope), type);
  return GetSubBuildDirAsSourceDir(
      BuildDirContext(scope), scope->GetSourceDir(), type);
}
