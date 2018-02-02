// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "tools/gn/commands.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/input_file.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/setup.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/tokenizer.h"
#include "tools/gn/trace.h"

#if defined(OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace commands {

namespace {

const char kSwitchList[] = "list";
const char kSwitchShort[] = "short";
const char kSwitchOverridesOnly[] = "overrides-only";

bool DoesLineBeginWithComment(const base::StringPiece& line) {
  // Skip whitespace.
  size_t i = 0;
  while (i < line.size() && base::IsAsciiWhitespace(line[i]))
    i++;

  return i < line.size() && line[i] == '#';
}

// Returns the offset of the beginning of the line identified by |offset|.
size_t BackUpToLineBegin(const std::string& data, size_t offset) {
  // Degenerate case of an empty line. Below we'll try to return the
  // character after the newline, but that will be incorrect in this case.
  if (offset == 0 || Tokenizer::IsNewline(data, offset))
    return offset;

  size_t cur = offset;
  do {
    cur--;
    if (Tokenizer::IsNewline(data, cur))
      return cur + 1;  // Want the first character *after* the newline.
  } while (cur > 0);
  return 0;
}

// Assumes DoesLineBeginWithComment(), this strips the # character from the
// beginning and normalizes preceeding whitespace.
std::string StripHashFromLine(const base::StringPiece& line) {
  // Replace the # sign and everything before it with 3 spaces, so that a
  // normal comment that has a space after the # will be indented 4 spaces
  // (which makes our formatting come out nicely). If the comment is indented
  // from there, we want to preserve that indenting.
  return "   " + line.substr(line.find('#') + 1).as_string();
}

// Tries to find the comment before the setting of the given value.
void GetContextForValue(const Value& value,
                        std::string* location_str,
                        std::string* comment) {
  Location location = value.origin()->GetRange().begin();
  const InputFile* file = location.file();
  if (!file)
    return;

  *location_str = file->name().value() + ":" +
      base::IntToString(location.line_number());

  const std::string& data = file->contents();
  size_t line_off =
      Tokenizer::ByteOffsetOfNthLine(data, location.line_number());

  while (line_off > 1) {
    line_off -= 2;  // Back up to end of previous line.
    size_t previous_line_offset = BackUpToLineBegin(data, line_off);

    base::StringPiece line(&data[previous_line_offset],
                           line_off - previous_line_offset + 1);
    if (!DoesLineBeginWithComment(line))
      break;

    comment->insert(0, StripHashFromLine(line) + "\n");
    line_off = previous_line_offset;
  }
}

// Prints the value and origin for a default value. Default values always list
// an origin and if there is no origin, print a message about it being
// internally set. Overrides can't be internally set so the location handling
// is a bit different.
//
// The default value also contains the docstring.
void PrintDefaultValueInfo(base::StringPiece name, const Value& value) {
  OutputString(value.ToString(true) + "\n");
  if (value.origin()) {
    std::string location, comment;
    GetContextForValue(value, &location, &comment);
    OutputString("      From " + location + "\n");
    if (!comment.empty())
      OutputString("\n" + comment);
  } else {
    OutputString("      (Internally set; try `gn help " + name.as_string() +
                 "`.)\n");
  }
}

// Override value is null if there is no override.
void PrintArgHelp(const base::StringPiece& name,
                  const Args::ValueWithOverride& val) {
  OutputString(name.as_string(), DECORATION_YELLOW);
  OutputString("\n");

  if (val.has_override) {
    // Override present, print both it and the default.
    OutputString("    Current value = " + val.override_value.ToString(true) +
                 "\n");
    if (val.override_value.origin()) {
      std::string location, comment;
      GetContextForValue(val.override_value, &location, &comment);
      OutputString("      From " + location + "\n");
    }
    OutputString("    Overridden from the default = ");
    PrintDefaultValueInfo(name, val.default_value);
  } else {
    // No override.
    OutputString("    Current value (from the default) = ");
    PrintDefaultValueInfo(name, val.default_value);
  }
}

int ListArgs(const std::string& build_dir) {
  Setup* setup = new Setup;
  if (!setup->DoSetup(build_dir, false) || !setup->Run())
    return 1;

  Args::ValueWithOverrideMap args =
      setup->build_settings().build_args().GetAllArguments();
  std::string list_value =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kSwitchList);
  if (!list_value.empty()) {
    // List just the one specified as the parameter to --list.
    auto found = args.find(list_value);
    if (found == args.end()) {
      Err(Location(), "Unknown build argument.",
          "You asked for \"" + list_value + "\" which I didn't find in any "
          "build file\nassociated with this build.").PrintToStdout();
      return 1;
    }

    // Delete everything from the map except the one requested.
    Args::ValueWithOverrideMap::value_type preserved = *found;
    args.clear();
    args.insert(preserved);
  }

  // Cache this to avoid looking it up for each |arg| in the loops below.
  const bool overrides_only =
      base::CommandLine::ForCurrentProcess()->HasSwitch(kSwitchOverridesOnly);

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSwitchShort)) {
    // Short <key>=<current_value> output.
    for (const auto& arg : args) {
      if (overrides_only && !arg.second.has_override)
        continue;
      OutputString(arg.first.as_string());
      OutputString(" = ");
      if (arg.second.has_override)
        OutputString(arg.second.override_value.ToString(true));
      else
        OutputString(arg.second.default_value.ToString(true));
      OutputString("\n");
    }
    return 0;
  }

  // Long output.
  for (const auto& arg : args) {
    if (overrides_only && !arg.second.has_override)
      continue;
    PrintArgHelp(arg.first, arg.second);
    OutputString("\n");
  }

  return 0;
}

#if defined(OS_WIN)

bool RunEditor(const base::FilePath& file_to_edit) {
  SHELLEXECUTEINFO info;
  memset(&info, 0, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_CLASSNAME;
  info.lpFile = file_to_edit.value().c_str();
  info.nShow = SW_SHOW;
  info.lpClass = L".txt";
  if (!::ShellExecuteEx(&info)) {
    Err(Location(), "Couldn't run editor.",
        "Just edit \"" + FilePathToUTF8(file_to_edit) +
        "\" manually instead.").PrintToStdout();
    return false;
  }

  if (!info.hProcess) {
    // Windows re-used an existing process.
    OutputString("\"" + FilePathToUTF8(file_to_edit) +
                 "\" opened in editor, save it and press <Enter> when done.\n");
    getchar();
  } else {
    OutputString("Waiting for editor on \"" + FilePathToUTF8(file_to_edit) +
                 "\"...\n");
    ::WaitForSingleObject(info.hProcess, INFINITE);
    ::CloseHandle(info.hProcess);
  }
  return true;
}

#else  // POSIX

bool RunEditor(const base::FilePath& file_to_edit) {
  const char* editor_ptr = getenv("GN_EDITOR");
  if (!editor_ptr)
    editor_ptr = getenv("VISUAL");
  if (!editor_ptr)
    editor_ptr = getenv("EDITOR");
  if (!editor_ptr)
    editor_ptr = "vi";

  std::string cmd(editor_ptr);
  cmd.append(" \"");

  // Its impossible to do this properly since we don't know the user's shell,
  // but quoting and escaping internal quotes should handle 99.999% of all
  // cases.
  std::string escaped_name = file_to_edit.value();
  base::ReplaceSubstringsAfterOffset(&escaped_name, 0, "\"", "\\\"");
  cmd.append(escaped_name);
  cmd.push_back('"');

  OutputString("Waiting for editor on \"" + file_to_edit.value() +
               "\"...\n");
  return system(cmd.c_str()) == 0;
}

#endif

int EditArgsFile(const std::string& build_dir) {
  {
    // Scope the setup. We only use it for some basic state. We'll do the
    // "real" build below in the gen command.
    Setup setup;
    // Don't fill build arguments. We're about to edit the file which supplies
    // these in the first place.
    setup.set_fill_arguments(false);
    if (!setup.DoSetup(build_dir, true))
      return 1;

    // Ensure the file exists. Need to normalize path separators since on
    // Windows they can come out as forward slashes here, and that confuses some
    // of the commands.
    BuildSettings build_settings = setup.build_settings();
    base::FilePath arg_file =
        build_settings.GetFullPath(setup.GetBuildArgFile())
            .NormalizePathSeparators();
    if (!base::PathExists(arg_file)) {
      std::string argfile_default_contents =
          "# Build arguments go here.\n"
          "# See \"gn args <out_dir> --list\" for available build "
          "arguments.\n";

      SourceFile template_path = build_settings.arg_file_template_path();
      if (!template_path.is_null()) {
        base::FilePath full_path =
            build_settings.GetFullPath(template_path).NormalizePathSeparators();
        if (!base::PathExists(full_path)) {
          Err err =
              Err(Location(), std::string("Can't load arg_file_template:\n  ") +
                                  template_path.value());
          err.PrintToStdout();
          return 1;
        }

        // Ignore the return code; if the read fails (unlikely), we'll just
        // use the default contents.
        base::ReadFileToString(full_path, &argfile_default_contents);
      }
#if defined(OS_WIN)
      // Use Windows lineendings for this file since it will often open in
      // Notepad which can't handle Unix ones.
      base::ReplaceSubstringsAfterOffset(
          &argfile_default_contents, 0, "\n", "\r\n");
#endif
      base::CreateDirectory(arg_file.DirName());
      base::WriteFile(arg_file, argfile_default_contents.c_str(),
                      static_cast<int>(argfile_default_contents.size()));
    }

    ScopedTrace editor_trace(TraceItem::TRACE_SETUP, "Waiting for editor");
    if (!RunEditor(arg_file))
      return 1;
  }

  // Now do a normal "gen" command.
  OutputString("Generating files...\n");
  std::vector<std::string> gen_commands;
  gen_commands.push_back(build_dir);
  return RunGen(gen_commands);
}

}  // namespace

const char kArgs[] = "args";
const char kArgs_HelpShort[] =
    "args: Display or configure arguments declared by the build.";
const char kArgs_Help[] =
    R"(gn args <out_dir> [--list] [--short] [--args] [--overrides-only]

  See also "gn help buildargs" for a more high-level overview of how
  build arguments work.

Usage

  gn args <out_dir>
      Open the arguments for the given build directory in an editor. If the
      given build directory doesn't exist, it will be created and an empty args
      file will be opened in the editor. You would type something like this
      into that file:
          enable_doom_melon=false
          os="android"

      To find your editor on Posix, GN will search the environment variables in
      order: GN_EDITOR, VISUAL, and EDITOR. On Windows GN will open the command
      associated with .txt files.

      Note: you can edit the build args manually by editing the file "args.gn"
      in the build directory and then running "gn gen <out_dir>".

  gn args <out_dir> --list[=<exact_arg>] [--short] [--overrides-only]
      Lists all build arguments available in the current configuration, or, if
      an exact_arg is specified for the list flag, just that one build
      argument.

      The output will list the declaration location, current value for the
      build, default value (if different than the current value), and comment
      preceeding the declaration.

      If --short is specified, only the names and current values will be
      printed.

      If --overrides-only is specified, only the names and current values of
      arguments that have been overridden (i.e. non-default arguments) will
      be printed. Overrides come from the <out_dir>/args.gn file and //.gn


Examples

  gn args out/Debug
    Opens an editor with the args for out/Debug.

  gn args out/Debug --list --short
    Prints all arguments with their default values for the out/Debug
    build.

  gn args out/Debug --list --short --overrides-only
    Prints overridden arguments for the out/Debug build.

  gn args out/Debug --list=target_cpu
    Prints information about the "target_cpu" argument for the "
   "out/Debug
    build.

  gn args --list --args="os=\"android\" enable_doom_melon=true"
    Prints all arguments with the default values for a build with the
    given arguments set (which may affect the values of other
    arguments).
)";

int RunArgs(const std::vector<std::string>& args) {
  if (args.size() != 1) {
    Err(Location(), "Exactly one build dir needed.",
        "Usage: \"gn args <out_dir>\"\n"
        "Or see \"gn help args\" for more variants.").PrintToStdout();
    return 1;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSwitchList))
    return ListArgs(args[0]);
  return EditArgsFile(args[0]);
}

}  // namespace commands
