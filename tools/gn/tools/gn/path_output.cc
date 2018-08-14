// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/path_output.h"

#include "base/strings/string_util.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/output_file.h"
#include "tools/gn/string_utils.h"
#include "util/build_config.h"

PathOutput::PathOutput(const SourceDir& current_dir,
                       const base::StringPiece& source_root,
                       EscapingMode escaping)
    : current_dir_(current_dir) {
  inverse_current_dir_ = RebasePath("//", current_dir, source_root);
  if (!EndsWithSlash(inverse_current_dir_))
    inverse_current_dir_.push_back('/');
  options_.mode = escaping;
}

PathOutput::~PathOutput() = default;

void PathOutput::WriteFile(std::ostream& out, const SourceFile& file) const {
  WritePathStr(out, file.value());
}

void PathOutput::WriteDir(std::ostream& out,
                          const SourceDir& dir,
                          DirSlashEnding slash_ending) const {
  if (dir.value() == "/") {
    // Writing system root is always a slash (this will normally only come up
    // on Posix systems).
    if (slash_ending == DIR_NO_LAST_SLASH)
      out << "/.";
    else
      out << "/";
  } else if (dir.value() == "//") {
    // Writing out the source root.
    if (slash_ending == DIR_NO_LAST_SLASH) {
      // The inverse_current_dir_ will contain a [back]slash at the end, so we
      // can't just write it out.
      if (inverse_current_dir_.empty()) {
        out << ".";
      } else {
        out.write(inverse_current_dir_.c_str(),
                  inverse_current_dir_.size() - 1);
      }
    } else {
      if (inverse_current_dir_.empty())
        out << "./";
      else
        out << inverse_current_dir_;
    }
  } else if (dir == current_dir_) {
    // Writing the same directory. This needs special handling here since
    // we need to output something else other than the input.
    if (slash_ending == DIR_INCLUDE_LAST_SLASH)
      out << "./";
    else
      out << ".";
  } else if (slash_ending == DIR_INCLUDE_LAST_SLASH) {
    WritePathStr(out, dir.value());
  } else {
    // DIR_NO_LAST_SLASH mode, just trim the last char.
    WritePathStr(out,
                 base::StringPiece(dir.value().data(), dir.value().size() - 1));
  }
}

void PathOutput::WriteFile(std::ostream& out, const OutputFile& file) const {
  // Here we assume that the path is already preprocessed.
  EscapeStringToStream(out, file.value(), options_);
}

void PathOutput::WriteFiles(std::ostream& out,
                            const std::vector<OutputFile>& files) const {
  for (const auto& file : files) {
    out << " ";
    WriteFile(out, file);
  }
}

void PathOutput::WriteFiles(std::ostream& out,
                            const UniqueVector<OutputFile>& files) const {
  for (const auto& file : files) {
    out << " ";
    WriteFile(out, file);
  }
}

void PathOutput::WriteDir(std::ostream& out,
                          const OutputFile& file,
                          DirSlashEnding slash_ending) const {
  DCHECK(file.value().empty() || file.value()[file.value().size() - 1] == '/');

  switch (slash_ending) {
    case DIR_INCLUDE_LAST_SLASH:
      EscapeStringToStream(out, file.value(), options_);
      break;
    case DIR_NO_LAST_SLASH:
      if (!file.value().empty() &&
          file.value()[file.value().size() - 1] == '/') {
        // Trim trailing slash.
        EscapeStringToStream(
            out,
            base::StringPiece(file.value().data(), file.value().size() - 1),
            options_);
      } else {
        // Doesn't end with a slash, write the whole thing.
        EscapeStringToStream(out, file.value(), options_);
      }
      break;
  }
}

void PathOutput::WriteFile(std::ostream& out,
                           const base::FilePath& file) const {
  // Assume native file paths are always absolute.
  EscapeStringToStream(out, FilePathToUTF8(file), options_);
}

void PathOutput::WriteSourceRelativeString(std::ostream& out,
                                           const base::StringPiece& str) const {
  if (options_.mode == ESCAPE_NINJA_COMMAND) {
    // Shell escaping needs an intermediate string since it may end up
    // quoting the whole thing.
    std::string intermediate;
    intermediate.reserve(inverse_current_dir_.size() + str.size());
    intermediate.assign(inverse_current_dir_.c_str(),
                        inverse_current_dir_.size());
    intermediate.append(str.data(), str.size());

    EscapeStringToStream(
        out, base::StringPiece(intermediate.c_str(), intermediate.size()),
        options_);
  } else {
    // Ninja (and none) escaping can avoid the intermediate string and
    // reprocessing of the inverse_current_dir_.
    out << inverse_current_dir_;
    EscapeStringToStream(out, str, options_);
  }
}

void PathOutput::WritePathStr(std::ostream& out,
                              const base::StringPiece& str) const {
  DCHECK(str.size() > 0 && str[0] == '/');

  if (str.substr(0, current_dir_.value().size()) ==
      base::StringPiece(current_dir_.value())) {
    // The current dir is a prefix of the output file, so we can strip the
    // prefix and write out the result.
    EscapeStringToStream(out, str.substr(current_dir_.value().size()),
                         options_);
  } else if (str.size() >= 2 && str[1] == '/') {
    WriteSourceRelativeString(out, str.substr(2));
  } else {
// Input begins with one slash, don't write the current directory since
// it's system-absolute.
#if defined(OS_WIN)
    // On Windows, trim the leading slash, since the input for absolute
    // paths will look like "/C:/foo/bar.txt".
    EscapeStringToStream(out, str.substr(1), options_);
#else
    EscapeStringToStream(out, str, options_);
#endif
  }
}
