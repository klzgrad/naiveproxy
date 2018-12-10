// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_PATH_OUTPUT_H_
#define TOOLS_GN_PATH_OUTPUT_H_

#include <iosfwd>
#include <string>

#include "base/macros.h"
#include "base/strings/string_piece.h"
#include "tools/gn/escape.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/unique_vector.h"

class OutputFile;
class SourceFile;

namespace base {
class FilePath;
}

// Writes file names to streams assuming a certain input directory and
// escaping rules. This gives us a central place for managing this state.
class PathOutput {
 public:
  // Controls whether writing directory names include the trailing slash.
  // Often we don't want the trailing slash when writing out to a command line,
  // especially on Windows where it's a backslash and might be interpreted as
  // escaping the thing following it.
  enum DirSlashEnding {
    DIR_INCLUDE_LAST_SLASH,
    DIR_NO_LAST_SLASH,
  };

  PathOutput(const SourceDir& current_dir,
             const base::StringPiece& source_root,
             EscapingMode escaping);
  ~PathOutput();

  // Read-only since inverse_current_dir_ is computed depending on this.
  EscapingMode escaping_mode() const { return options_.mode; }

  const SourceDir& current_dir() const { return current_dir_; }

  // Getter/setters for flags inside the escape options.
  bool inhibit_quoting() const { return options_.inhibit_quoting; }
  void set_inhibit_quoting(bool iq) { options_.inhibit_quoting = iq; }
  void set_escape_platform(EscapingPlatform p) { options_.platform = p; }

  void WriteFile(std::ostream& out, const SourceFile& file) const;
  void WriteFile(std::ostream& out, const OutputFile& file) const;
  void WriteFile(std::ostream& out, const base::FilePath& file) const;

  // Writes the given OutputFiles with spaces separating them. This will also
  // write an initial space before the first item.
  void WriteFiles(std::ostream& out,
                  const std::vector<OutputFile>& files) const;
  void WriteFiles(std::ostream& out,
                  const UniqueVector<OutputFile>& files) const;

  // This variant assumes the dir ends in a trailing slash or is empty.
  void WriteDir(std::ostream& out,
                const SourceDir& dir,
                DirSlashEnding slash_ending) const;

  void WriteDir(std::ostream& out,
                const OutputFile& file,
                DirSlashEnding slash_ending) const;

  // Backend for WriteFile and WriteDir. This appends the given file or
  // directory string to the file.
  void WritePathStr(std::ostream& out, const base::StringPiece& str) const;

 private:
  // Takes the given string and writes it out, appending to the inverse
  // current dir. This assumes leading slashes have been trimmed.
  void WriteSourceRelativeString(std::ostream& out,
                                 const base::StringPiece& str) const;

  SourceDir current_dir_;

  // Uses system slashes if convert_slashes_to_system_.
  std::string inverse_current_dir_;

  // Since the inverse_current_dir_ depends on some of these, we don't expose
  // this directly to modification.
  EscapeOptions options_;
};

#endif  // TOOLS_GN_PATH_OUTPUT_H_
