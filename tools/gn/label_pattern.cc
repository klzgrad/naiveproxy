// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/label_pattern.h"

#include <stddef.h>

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/value.h"

const char kLabelPattern_Help[] =
    R"*(Label patterns

  A label pattern is a way of expressing one or more labels in a portion of the
  source tree. They are not general regular expressions.

  They can take the following forms only:

   - Explicit (no wildcard):
       "//foo/bar:baz"
       ":baz"

   - Wildcard target names:
       "//foo/bar:*" (all targets in the //foo/bar/BUILD.gn file)
       ":*"  (all targets in the current build file)

   - Wildcard directory names ("*" is only supported at the end)
       "*"  (all targets)
       "//foo/bar/*"  (all targets in any subdir of //foo/bar)
       "./*"  (all targets in the current build file or sub dirs)

  Any of the above forms can additionally take an explicit toolchain. In this
  case, the toolchain must be fully qualified (no wildcards are supported in
  the toolchain name).

    "//foo:bar(//build/toochain:mac)"
        An explicit target in an explicit toolchain.

    ":*(//build/toolchain/linux:32bit)"
        All targets in the current build file using the 32-bit Linux toolchain.

    "//foo/*(//build/toolchain:win)"
        All targets in //foo and any subdirectory using the Windows
        toolchain.
)*";

LabelPattern::LabelPattern() : type_(MATCH) {
}

LabelPattern::LabelPattern(Type type,
                           const SourceDir& dir,
                           const base::StringPiece& name,
                           const Label& toolchain_label)
    : toolchain_(toolchain_label),
      type_(type),
      dir_(dir) {
  name.CopyToString(&name_);
}

LabelPattern::LabelPattern(const LabelPattern& other) = default;

LabelPattern::~LabelPattern() {
}

// static
LabelPattern LabelPattern::GetPattern(const SourceDir& current_dir,
                                      const Value& value,
                                      Err* err) {
  if (!value.VerifyTypeIs(Value::STRING, err))
    return LabelPattern();

  base::StringPiece str(value.string_value());
  if (str.empty()) {
    *err = Err(value, "Label pattern must not be empty.");
    return LabelPattern();
  }

  // If there's no wildcard, this is specifying an exact label, use the
  // label resolution code to get all the implicit name stuff.
  size_t star = str.find('*');
  if (star == std::string::npos) {
    Label label = Label::Resolve(current_dir, Label(), value, err);
    if (err->has_error())
      return LabelPattern();

    // Toolchain.
    Label toolchain_label;
    if (!label.toolchain_dir().is_null() || !label.toolchain_name().empty())
      toolchain_label = label.GetToolchainLabel();

    return LabelPattern(MATCH, label.dir(), label.name(), toolchain_label);
  }

  // Wildcard case, need to split apart the label to see what it specifies.
  Label toolchain_label;
  size_t open_paren = str.find('(');
  if (open_paren != std::string::npos) {
    // Has a toolchain definition, extract inside the parens.
    size_t close_paren = str.find(')', open_paren);
    if (close_paren == std::string::npos) {
      *err = Err(value, "No close paren when looking for toolchain name.");
      return LabelPattern();
    }

    std::string toolchain_string =
        str.substr(open_paren + 1, close_paren - open_paren - 1).as_string();
    if (toolchain_string.find('*') != std::string::npos) {
      *err = Err(value, "Can't have a wildcard in the toolchain.");
      return LabelPattern();
    }

    // Parse the inside of the parens as a label for a toolchain.
    Value value_for_toolchain(value.origin(), toolchain_string);
    toolchain_label =
        Label::Resolve(current_dir, Label(), value_for_toolchain, err);
    if (err->has_error())
      return LabelPattern();

    // Trim off the toolchain for the processing below.
    str = str.substr(0, open_paren);
  }

  // Extract path and name.
  base::StringPiece path;
  base::StringPiece name;
  size_t offset = 0;
#if defined(OS_WIN)
  if (IsPathAbsolute(str)) {
    size_t drive_letter_pos = str[0] == '/' ? 1 : 0;
    if (str.size() > drive_letter_pos + 2 && str[drive_letter_pos + 1] == ':' &&
        IsSlash(str[drive_letter_pos + 2]) &&
        base::IsAsciiAlpha(str[drive_letter_pos])) {
      // Skip over the drive letter colon.
      offset = drive_letter_pos + 2;
    }
  }
#endif
  size_t colon = str.find(':', offset);
  if (colon == std::string::npos) {
    path = base::StringPiece(str);
  } else {
    path = str.substr(0, colon);
    name = str.substr(colon + 1);
  }

  // The path can have these forms:
  //   1. <empty>  (use current dir)
  //   2. <non wildcard stuff>  (send through directory resolution)
  //   3. <non wildcard stuff>*  (send stuff through dir resolution, note star)
  //   4. *  (matches anything)
  SourceDir dir;
  bool has_path_star = false;
  if (path.empty()) {
    // Looks like ":foo".
    dir = current_dir;
  } else if (path[path.size() - 1] == '*') {
    // Case 3 or 4 above.
    has_path_star = true;

    // Adjust path to contain everything but the star.
    path = path.substr(0, path.size() - 1);

    if (!path.empty() && path[path.size() - 1] != '/') {
      // The input was "foo*" which is invalid.
      *err = Err(value, "'*' must match full directories in a label pattern.",
          "You did \"foo*\" but this thing doesn't do general pattern\n"
          "matching. Instead, you have to add a slash: \"foo/*\" to match\n"
          "all targets in a directory hierarchy.");
      return LabelPattern();
    }
  }

  // Resolve the part of the path that's not the wildcard.
  if (!path.empty()) {
    // The non-wildcard stuff better not have a wildcard.
    if (path.find('*') != base::StringPiece::npos) {
      *err = Err(value, "Label patterns only support wildcard suffixes.",
          "The pattern contained a '*' that wasn't at the end.");
      return LabelPattern();
    }

    // Resolve the non-wildcard stuff.
    dir = current_dir.ResolveRelativeDir(value, path, err);
    if (err->has_error())
      return LabelPattern();
  }

  // Resolve the name. At this point, we're doing wildcard matches so the
  // name should either be empty ("foo/*") or a wildcard ("foo:*");
  if (colon != std::string::npos && name != "*") {
    *err = Err(value, "Invalid label pattern.",
        "You seem to be using the wildcard more generally that is supported.\n"
        "Did you mean \"foo:*\" to match everything in the file, or\n"
        "\"./*\" to recursively match everything in the currend subtree.");
    return LabelPattern();
  }

  Type type;
  if (has_path_star) {
    // We know there's a wildcard, so if the name is empty it looks like
    // "foo/*".
    type = RECURSIVE_DIRECTORY;
  } else {
    // Everything else should be of the form "foo:*".
    type = DIRECTORY;
  }

  // When we're doing wildcard matching, the name is always empty.
  return LabelPattern(type, dir, base::StringPiece(), toolchain_label);
}

bool LabelPattern::HasWildcard(const std::string& str) {
  // Just look for a star. In the future, we may want to handle escaping or
  // other types of patterns.
  return str.find('*') != std::string::npos;
}

bool LabelPattern::Matches(const Label& label) const {
  if (!toolchain_.is_null()) {
    // Toolchain must match exactly.
    if (toolchain_.dir() != label.toolchain_dir() ||
        toolchain_.name() != label.toolchain_name())
      return false;
  }

  switch (type_) {
    case MATCH:
      return label.name() == name_ && label.dir() == dir_;
    case DIRECTORY:
      // The directories must match exactly.
      return label.dir() == dir_;
    case RECURSIVE_DIRECTORY:
      // Our directory must be a prefix of the input label for recursive.
      return label.dir().value().compare(0, dir_.value().size(), dir_.value())
          == 0;
    default:
      NOTREACHED();
      return false;
  }
}

std::string LabelPattern::Describe() const {
  std::string result;

  switch (type()) {
    case MATCH:
      result = DirectoryWithNoLastSlash(dir()) + ":" + name();
      break;
    case DIRECTORY:
      result = DirectoryWithNoLastSlash(dir()) + ":*";
      break;
    case RECURSIVE_DIRECTORY:
      result = dir().value() + "*";
      break;
  }

  if (!toolchain_.is_null()) {
    result.push_back('(');
    result.append(toolchain_.GetUserVisibleName(false));
    result.push_back(')');
  }
  return result;
}
