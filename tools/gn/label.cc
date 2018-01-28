// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/label.h"

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/value.h"

namespace {

// We print user visible label names with no trailing slash after the
// directory name.
std::string DirWithNoTrailingSlash(const SourceDir& dir) {
  // Be careful not to trim if the input is just "/" or "//".
  if (dir.value().size() > 2)
    return dir.value().substr(0, dir.value().size() - 1);
  return dir.value();
}

// Given the separate-out input (everything before the colon) in the dep rule,
// computes the final build rule. Sets err on failure. On success,
// |*used_implicit| will be set to whether the implicit current directory was
// used. The value is used only for generating error messages.
bool ComputeBuildLocationFromDep(const Value& input_value,
                                 const SourceDir& current_dir,
                                 const base::StringPiece& input,
                                 SourceDir* result,
                                 Err* err) {
  // No rule, use the current location.
  if (input.empty()) {
    *result = current_dir;
    return true;
  }

  *result = current_dir.ResolveRelativeDir(input_value, input, err);
  return true;
}

// Given the separated-out target name (after the colon) computes the final
// name, using the implicit name from the previously-generated
// computed_location if necessary. The input_value is used only for generating
// error messages.
bool ComputeTargetNameFromDep(const Value& input_value,
                              const SourceDir& computed_location,
                              const base::StringPiece& input,
                              std::string* result,
                              Err* err) {
  if (!input.empty()) {
    // Easy case: input is specified, just use it.
    result->assign(input.data(), input.size());
    return true;
  }

  const std::string& loc = computed_location.value();

  // Use implicit name. The path will be "//", "//base/", "//base/i18n/", etc.
  if (loc.size() <= 2) {
    *err = Err(input_value, "This dependency name is empty");
    return false;
  }

  size_t next_to_last_slash = loc.rfind('/', loc.size() - 2);
  DCHECK(next_to_last_slash != std::string::npos);
  result->assign(&loc[next_to_last_slash + 1],
                 loc.size() - next_to_last_slash - 2);
  return true;
}

// The original value is used only for error reporting, use the |input| as the
// input to this function (which may be a substring of the original value when
// we're parsing toolchains.
//
// If the output toolchain vars are NULL, then we'll report an error if we
// find a toolchain specified (this is used when recursively parsing toolchain
// labels which themselves can't have toolchain specs).
//
// We assume that the output variables are initialized to empty so we don't
// write them unless we need them to contain something.
//
// Returns true on success. On failure, the out* variables might be written to
// but shouldn't be used.
bool Resolve(const SourceDir& current_dir,
             const Label& current_toolchain,
             const Value& original_value,
             const base::StringPiece& input,
             SourceDir* out_dir,
             std::string* out_name,
             SourceDir* out_toolchain_dir,
             std::string* out_toolchain_name,
             Err* err) {
  // To workaround the problem that StringPiece operator[] doesn't return a ref.
  const char* input_str = input.data();
  size_t offset = 0;
#if defined(OS_WIN)
  if (IsPathAbsolute(input)) {
    size_t drive_letter_pos = input[0] == '/' ? 1 : 0;
    if (input.size() > drive_letter_pos + 2 &&
        input[drive_letter_pos + 1] == ':' &&
        IsSlash(input[drive_letter_pos + 2]) &&
        base::IsAsciiAlpha(input[drive_letter_pos])) {
      // Skip over the drive letter colon.
      offset = drive_letter_pos + 2;
    }
  }
#endif
  size_t path_separator = input.find_first_of(":(", offset);
  base::StringPiece location_piece;
  base::StringPiece name_piece;
  base::StringPiece toolchain_piece;
  if (path_separator == std::string::npos) {
    location_piece = input;
    // Leave name & toolchain piece null.
  } else {
    location_piece = base::StringPiece(&input_str[0], path_separator);

    size_t toolchain_separator = input.find('(', path_separator);
    if (toolchain_separator == std::string::npos) {
      name_piece = base::StringPiece(&input_str[path_separator + 1],
                                     input.size() - path_separator - 1);
      // Leave location piece null.
    } else if (!out_toolchain_dir) {
      // Toolchain specified but not allows in this context.
      *err = Err(original_value, "Toolchain has a toolchain.",
          "Your toolchain definition (inside the parens) seems to itself "
          "have a\ntoolchain. Don't do this.");
      return false;
    } else {
      // Name piece is everything between the two separators. Note that the
      // separators may be the same (e.g. "//foo(bar)" which means empty name.
      if (toolchain_separator > path_separator) {
        name_piece = base::StringPiece(
            &input_str[path_separator + 1],
            toolchain_separator - path_separator - 1);
      }

      // Toolchain name should end in a ) and this should be the end of the
      // string.
      if (input[input.size() - 1] != ')') {
        *err = Err(original_value, "Bad toolchain name.",
            "Toolchain name must end in a \")\" at the end of the label.");
        return false;
      }

      // Subtract off the two parens to just get the toolchain name.
      toolchain_piece = base::StringPiece(
          &input_str[toolchain_separator + 1],
          input.size() - toolchain_separator - 2);
    }
  }

  // Everything before the separator is the filename.
  // We allow three cases:
  //   Absolute:                "//foo:bar" -> /foo:bar
  //   Target in current file:  ":foo"     -> <currentdir>:foo
  //   Path with implicit name: "/foo"     -> /foo:foo
  if (location_piece.empty() && name_piece.empty()) {
    // Can't use both implicit filename and name (":").
    *err = Err(original_value, "This doesn't specify a dependency.");
    return false;
  }

  if (!ComputeBuildLocationFromDep(original_value, current_dir, location_piece,
                                   out_dir, err))
    return false;

  if (!ComputeTargetNameFromDep(original_value, *out_dir, name_piece,
                                out_name, err))
    return false;

  // Last, do the toolchains.
  if (out_toolchain_dir) {
    // Handle empty toolchain strings. We don't allow normal labels to be
    // empty so we can't allow the recursive call of this function to do this
    // check.
    if (toolchain_piece.empty()) {
      *out_toolchain_dir = current_toolchain.dir();
      *out_toolchain_name = current_toolchain.name();
      return true;
    } else {
      return Resolve(current_dir, current_toolchain, original_value,
                     toolchain_piece, out_toolchain_dir, out_toolchain_name,
                     nullptr, nullptr, err);
    }
  }
  return true;
}

}  // namespace

const char kLabels_Help[] =
    R"*(About labels

  Everything that can participate in the dependency graph (targets, configs,
  and toolchains) are identified by labels. A common label looks like:

    //base/test:test_support

  This consists of a source-root-absolute path, a colon, and a name. This means
  to look for the thing named "test_support" in "base/test/BUILD.gn".

  You can also specify system absolute paths if necessary. Typically such
  paths would be specified via a build arg so the developer can specify where
  the component is on their system.

    /usr/local/foo:bar    (Posix)
    /C:/Program Files/MyLibs:bar   (Windows)

Toolchains

  A canonical label includes the label of the toolchain being used. Normally,
  the toolchain label is implicitly inherited from the current execution
  context, but you can override this to specify cross-toolchain dependencies:

    //base/test:test_support(//build/toolchain/win:msvc)

  Here GN will look for the toolchain definition called "msvc" in the file
  "//build/toolchain/win" to know how to compile this target.

Relative labels

  If you want to refer to something in the same buildfile, you can omit
  the path name and just start with a colon. This format is recommended for
  all same-file references.

    :base

  Labels can be specified as being relative to the current directory.
  Stylistically, we prefer to use absolute paths for all non-file-local
  references unless a build file needs to be run in different contexts (like a
  project needs to be both standalone and pulled into other projects in
  difference places in the directory hierarchy).

    source/plugin:myplugin
    ../net:url_request

Implicit names

  If a name is unspecified, it will inherit the directory name. Stylistically,
  we prefer to omit the colon and name when possible:

    //net  ->  //net:net
    //tools/gn  ->  //tools/gn:gn
)*";

Label::Label() {
}

Label::Label(const SourceDir& dir,
             const base::StringPiece& name,
             const SourceDir& toolchain_dir,
             const base::StringPiece& toolchain_name)
    : dir_(dir),
      toolchain_dir_(toolchain_dir) {
  name_.assign(name.data(), name.size());
  toolchain_name_.assign(toolchain_name.data(), toolchain_name.size());
}

Label::Label(const SourceDir& dir, const base::StringPiece& name)
    : dir_(dir) {
  name_.assign(name.data(), name.size());
}

Label::Label(const Label& other) = default;

Label::~Label() {
}

// static
Label Label::Resolve(const SourceDir& current_dir,
                     const Label& current_toolchain,
                     const Value& input,
                     Err* err) {
  Label ret;
  if (input.type() != Value::STRING) {
    *err = Err(input, "Dependency is not a string.");
    return ret;
  }
  const std::string& input_string = input.string_value();
  if (input_string.empty()) {
    *err = Err(input, "Dependency string is empty.");
    return ret;
  }

  if (!::Resolve(current_dir, current_toolchain, input, input_string,
                 &ret.dir_, &ret.name_,
                 &ret.toolchain_dir_, &ret.toolchain_name_,
                 err))
    return Label();
  return ret;
}

Label Label::GetToolchainLabel() const {
  return Label(toolchain_dir_, toolchain_name_);
}

Label Label::GetWithNoToolchain() const {
  return Label(dir_, name_);
}

std::string Label::GetUserVisibleName(bool include_toolchain) const {
  std::string ret;
  ret.reserve(dir_.value().size() + name_.size() + 1);

  if (dir_.is_null())
    return ret;

  ret = DirWithNoTrailingSlash(dir_);
  ret.push_back(':');
  ret.append(name_);

  if (include_toolchain) {
    ret.push_back('(');
    if (!toolchain_dir_.is_null() && !toolchain_name_.empty()) {
      ret.append(DirWithNoTrailingSlash(toolchain_dir_));
      ret.push_back(':');
      ret.append(toolchain_name_);
    }
    ret.push_back(')');
  }
  return ret;
}

std::string Label::GetUserVisibleName(const Label& default_toolchain) const {
  bool include_toolchain =
      default_toolchain.dir() != toolchain_dir_ ||
      default_toolchain.name() != toolchain_name_;
  return GetUserVisibleName(include_toolchain);
}
