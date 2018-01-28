// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/setup.h"

#include <stdlib.h>
#include <algorithm>
#include <sstream>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/process/launch.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "tools/gn/command_format.h"
#include "tools/gn/commands.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/input_file.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/parser.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/switches.h"
#include "tools/gn/tokenizer.h"
#include "tools/gn/trace.h"
#include "tools/gn/value.h"
#include "tools/gn/value_extractors.h"

#if defined(OS_WIN)
#include <windows.h>
#endif

extern const char kDotfile_Help[] =
    R"(.gn file

  When gn starts, it will search the current directory and parent directories
  for a file called ".gn". This indicates the source root. You can override
  this detection by using the --root command-line argument

  The .gn file in the source root will be executed. The syntax is the same as a
  buildfile, but with very limited build setup-specific meaning.

  If you specify --root, by default GN will look for the file .gn in that
  directory. If you want to specify a different file, you can additionally pass
  --dotfile:

    gn gen out/Debug --root=/home/build --dotfile=/home/my_gn_file.gn

Variables

  arg_file_template [optional]
      Path to a file containing the text that should be used as the default
      args.gn content when you run `gn args`.

  buildconfig [required]
      Path to the build config file. This file will be used to set up the
      build file execution environment for each toolchain.

  check_targets [optional]
      A list of labels and label patterns that should be checked when running
      "gn check" or "gn gen --check". If unspecified, all targets will be
      checked. If it is the empty list, no targets will be checked.

      The format of this list is identical to that of "visibility" so see "gn
      help visibility" for examples.

  exec_script_whitelist [optional]
      A list of .gn/.gni files (not labels) that have permission to call the
      exec_script function. If this list is defined, calls to exec_script will
      be checked against this list and GN will fail if the current file isn't
      in the list.

      This is to allow the use of exec_script to be restricted since is easy to
      use inappropriately. Wildcards are not supported. Files in the
      secondary_source tree (if defined) should be referenced by ignoring the
      secondary tree and naming them as if they are in the main tree.

      If unspecified, the ability to call exec_script is unrestricted.

      Example:
        exec_script_whitelist = [
          "//base/BUILD.gn",
          "//build/my_config.gni",
        ]

  root [optional]
      Label of the root build target. The GN build will start by loading the
      build file containing this target name. This defaults to "//:" which will
      cause the file //BUILD.gn to be loaded.

  script_executable [optional]
      Path to specific Python executable or potentially a different language
      interpreter that is used to execute scripts in action targets and
      exec_script calls.

  secondary_source [optional]
      Label of an alternate directory tree to find input files. When searching
      for a BUILD.gn file (or the build config file discussed above), the file
      will first be looked for in the source root. If it's not found, the
      secondary source root will be checked (which would contain a parallel
      directory hierarchy).

      This behavior is intended to be used when BUILD.gn files can't be checked
      in to certain source directories for whatever reason.

      The secondary source root must be inside the main source tree.

  default_args [optional]
      Scope containing the default overrides for declared arguments. These
      overrides take precedence over the default values specified in the
      declare_args() block, but can be overriden using --args or the
      args.gn file.

      This is intended to be used when subprojects declare arguments with
      default values that need to be changed for whatever reason.

Example .gn file contents

  buildconfig = "//build/config/BUILDCONFIG.gn"

  check_targets = [
    "//doom_melon/*",  # Check everything in this subtree.
    "//tools:mind_controlling_ant",  # Check this specific target.
  ]

  root = "//:root"

  secondary_source = "//build/config/temporary_buildfiles/"

  default_args = {
    # Default to release builds for this project.
    is_debug = false
    is_component_build = false
  }
)";

namespace {

const base::FilePath::CharType kGnFile[] = FILE_PATH_LITERAL(".gn");

base::FilePath FindDotFile(const base::FilePath& current_dir) {
  base::FilePath try_this_file = current_dir.Append(kGnFile);
  if (base::PathExists(try_this_file))
    return try_this_file;

  base::FilePath with_no_slash = current_dir.StripTrailingSeparators();
  base::FilePath up_one_dir = with_no_slash.DirName();
  if (up_one_dir == current_dir)
    return base::FilePath();  // Got to the top.

  return FindDotFile(up_one_dir);
}

void ForwardItemDefinedToBuilderInMainThread(
    Builder* builder_call_on_main_thread_only,
    std::unique_ptr<Item> item) {
  builder_call_on_main_thread_only->ItemDefined(std::move(item));

  // Pair to the Increment in ItemDefinedCallback.
  g_scheduler->DecrementWorkCount();
}

// Called on any thread. Post the item to the builder on the main thread.
void ItemDefinedCallback(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    Builder* builder_call_on_main_thread_only,
    std::unique_ptr<Item> item) {
  DCHECK(item);

  // Increment the work count for the duration of defining the item with the
  // builder. Otherwise finishing this callback will race finishing loading
  // files. If there is no other pending work at any point in the middle of
  // this call completing on the main thread, the 'Complete' function will
  // be signaled and we'll stop running with an incomplete build.
  g_scheduler->IncrementWorkCount();
  task_runner->PostTask(
      FROM_HERE,
      base::Bind(&ForwardItemDefinedToBuilderInMainThread,
                 base::Unretained(builder_call_on_main_thread_only),
                 base::Passed(&item)));
}

void DecrementWorkCount() {
  g_scheduler->DecrementWorkCount();
}

#if defined(OS_WIN)

// Given the path to a batch file that runs Python, extracts the name of the
// executable actually implementing Python. Generally people write a batch file
// to put something named "python" on the path, which then just redirects to
// a python.exe somewhere else. This step decodes that setup. On failure,
// returns empty path.
base::FilePath PythonBatToExe(const base::FilePath& bat_path) {
  // Note exciting double-quoting to allow spaces. The /c switch seems to check
  // for quotes around the whole thing and then deletes them. If you want to
  // quote the first argument in addition (to allow for spaces in the Python
  // path, you need *another* set of quotes around that, likewise, we need
  // two quotes at the end.
  base::string16 command = L"cmd.exe /c \"\"";
  command.append(bat_path.value());
  command.append(L"\" -c \"import sys; print sys.executable\"\"");

  std::string python_path;
  if (base::GetAppOutput(command, &python_path)) {
    base::TrimWhitespaceASCII(python_path, base::TRIM_ALL, &python_path);

    // Python uses the system multibyte code page for sys.executable.
    base::FilePath exe_path(base::SysNativeMBToWide(python_path));

    // Check for reasonable output, cmd may have output an error message.
    if (base::PathExists(exe_path))
      return exe_path;
  }
  return base::FilePath();
}

const base::char16 kPythonExeName[] = L"python.exe";
const base::char16 kPythonBatName[] = L"python.bat";

base::FilePath FindWindowsPython() {
  base::char16 current_directory[MAX_PATH];
  ::GetCurrentDirectory(MAX_PATH, current_directory);

  // First search for python.exe in the current directory.
  base::FilePath cur_dir_candidate_exe =
      base::FilePath(current_directory).Append(kPythonExeName);
  if (base::PathExists(cur_dir_candidate_exe))
    return cur_dir_candidate_exe;

  // Get the path.
  const base::char16 kPathEnvVarName[] = L"Path";
  DWORD path_length = ::GetEnvironmentVariable(kPathEnvVarName, nullptr, 0);
  if (path_length == 0)
    return base::FilePath();
  std::unique_ptr<base::char16[]> full_path(new base::char16[path_length]);
  DWORD actual_path_length =
      ::GetEnvironmentVariable(kPathEnvVarName, full_path.get(), path_length);
  CHECK_EQ(path_length, actual_path_length + 1);

  // Search for python.exe in the path.
  for (const auto& component : base::SplitStringPiece(
           base::StringPiece16(full_path.get(), path_length), L";",
           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    base::FilePath candidate_exe =
        base::FilePath(component).Append(kPythonExeName);
    if (base::PathExists(candidate_exe))
      return candidate_exe;

    // Also allow python.bat, but convert into the .exe.
    base::FilePath candidate_bat =
        base::FilePath(component).Append(kPythonBatName);
    if (base::PathExists(candidate_bat)) {
      base::FilePath python_exe = PythonBatToExe(candidate_bat);
      if (!python_exe.empty())
        return python_exe;
    }
  }
  return base::FilePath();
}
#endif

}  // namespace

const char Setup::kBuildArgFileName[] = "args.gn";

Setup::Setup()
    : build_settings_(),
      loader_(new LoaderImpl(&build_settings_)),
      builder_(loader_.get()),
      root_build_file_("//BUILD.gn"),
      check_public_headers_(false),
      dotfile_settings_(&build_settings_, std::string()),
      dotfile_scope_(&dotfile_settings_),
      default_args_(nullptr),
      fill_arguments_(true) {
  dotfile_settings_.set_toolchain_label(Label());

  build_settings_.set_item_defined_callback(
      base::Bind(&ItemDefinedCallback, scheduler_.task_runner(), &builder_));

  loader_->set_complete_callback(base::Bind(&DecrementWorkCount));
  // The scheduler's task runner wasn't created when the Loader was created, so
  // we need to set it now.
  loader_->set_task_runner(scheduler_.task_runner());
}

Setup::~Setup() {
}

bool Setup::DoSetup(const std::string& build_dir, bool force_create) {
  base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();

  scheduler_.set_verbose_logging(cmdline->HasSwitch(switches::kVerbose));
  if (cmdline->HasSwitch(switches::kTime) ||
      cmdline->HasSwitch(switches::kTracelog))
    EnableTracing();

  ScopedTrace setup_trace(TraceItem::TRACE_SETUP, "DoSetup");

  if (!FillSourceDir(*cmdline))
    return false;
  if (!RunConfigFile())
    return false;
  if (!FillOtherConfig(*cmdline))
    return false;

  // Must be after FillSourceDir to resolve.
  if (!FillBuildDir(build_dir, !force_create))
    return false;

  // Apply project-specific default (if specified).
  // Must happen before FillArguments().
  if (default_args_) {
    Scope::KeyValueMap overrides;
    default_args_->GetCurrentScopeValues(&overrides);
    build_settings_.build_args().AddArgOverrides(overrides);
  }

  if (fill_arguments_) {
    if (!FillArguments(*cmdline))
      return false;
  }
  if (!FillPythonPath(*cmdline))
    return false;

  // Check for unused variables in the .gn file.
  Err err;
  if (!dotfile_scope_.CheckForUnusedVars(&err)) {
    err.PrintToStdout();
    return false;
  }

  return true;
}

bool Setup::Run() {
  RunPreMessageLoop();
  if (!scheduler_.Run())
    return false;
  return RunPostMessageLoop();
}

SourceFile Setup::GetBuildArgFile() const {
  return SourceFile(build_settings_.build_dir().value() + kBuildArgFileName);
}

void Setup::RunPreMessageLoop() {
  // Will be decremented with the loader is drained.
  g_scheduler->IncrementWorkCount();

  // Load the root build file.
  loader_->Load(root_build_file_, LocationRange(), Label());
}

bool Setup::RunPostMessageLoop() {
  Err err;
  if (!builder_.CheckForBadItems(&err)) {
    err.PrintToStdout();
    return false;
  }

  if (!build_settings_.build_args().VerifyAllOverridesUsed(&err)) {
    // TODO(brettw) implement a system to have a different marker for
    // warnings. Until we have a better system, print the error but don't
    // return failure unless requested on the command line.
    err.PrintToStdout();
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kFailOnUnusedArgs))
      return false;
    return true;
  }

  if (check_public_headers_) {
    std::vector<const Target*> all_targets = builder_.GetAllResolvedTargets();
    std::vector<const Target*> to_check;
    if (check_patterns()) {
      commands::FilterTargetsByPatterns(all_targets, *check_patterns(),
                                        &to_check);
    } else {
      to_check = all_targets;
    }

    if (!commands::CheckPublicHeaders(&build_settings_, all_targets,
                                      to_check, false)) {
      return false;
    }
  }

  // Write out tracing and timing if requested.
  const base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  if (cmdline->HasSwitch(switches::kTime))
    PrintLongHelp(SummarizeTraces());
  if (cmdline->HasSwitch(switches::kTracelog))
    SaveTraces(cmdline->GetSwitchValuePath(switches::kTracelog));

  return true;
}

bool Setup::FillArguments(const base::CommandLine& cmdline) {
  // Use the args on the command line if specified, and save them. Do this even
  // if the list is empty (this means clear any defaults).
  if (cmdline.HasSwitch(switches::kArgs)) {
    if (!FillArgsFromCommandLine(cmdline.GetSwitchValueASCII(switches::kArgs)))
      return false;
    SaveArgsToFile();
    return true;
  }

  // No command line args given, use the arguments from the build dir (if any).
  return FillArgsFromFile();
}

bool Setup::FillArgsFromCommandLine(const std::string& args) {
  args_input_file_.reset(new InputFile(SourceFile()));
  args_input_file_->SetContents(args);
  args_input_file_->set_friendly_name("the command-line \"--args\"");
  return FillArgsFromArgsInputFile();
}

bool Setup::FillArgsFromFile() {
  ScopedTrace setup_trace(TraceItem::TRACE_SETUP, "Load args file");

  SourceFile build_arg_source_file = GetBuildArgFile();
  base::FilePath build_arg_file =
      build_settings_.GetFullPath(build_arg_source_file);

  std::string contents;
  if (!base::ReadFileToString(build_arg_file, &contents))
    return true;  // File doesn't exist, continue with default args.

  // Add a dependency on the build arguments file. If this changes, we want
  // to re-generate the build.
  g_scheduler->AddGenDependency(build_arg_file);

  if (contents.empty())
    return true;  // Empty file, do nothing.

  args_input_file_.reset(new InputFile(build_arg_source_file));
  args_input_file_->SetContents(contents);
  args_input_file_->set_friendly_name(
      "build arg file (use \"gn args <out_dir>\" to edit)");

  setup_trace.Done();  // Only want to count the load as part of the trace.
  return FillArgsFromArgsInputFile();
}

bool Setup::FillArgsFromArgsInputFile() {
  ScopedTrace setup_trace(TraceItem::TRACE_SETUP, "Parse args");

  Err err;
  args_tokens_ = Tokenizer::Tokenize(args_input_file_.get(), &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  args_root_ = Parser::Parse(args_tokens_, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  Scope arg_scope(&dotfile_settings_);
  // Set soure dir so relative imports in args work.
  SourceDir root_source_dir =
      SourceDirForCurrentDirectory(build_settings_.root_path());
  arg_scope.set_source_dir(root_source_dir);
  args_root_->Execute(&arg_scope, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  // Save the result of the command args.
  Scope::KeyValueMap overrides;
  arg_scope.GetCurrentScopeValues(&overrides);
  build_settings_.build_args().AddArgOverrides(overrides);
  return true;
}

bool Setup::SaveArgsToFile() {
  ScopedTrace setup_trace(TraceItem::TRACE_SETUP, "Save args file");

  // For the first run, the build output dir might not be created yet, so do
  // that so we can write a file into it. Ignore errors, we'll catch the error
  // when we try to write a file to it below.
  base::FilePath build_arg_file =
      build_settings_.GetFullPath(GetBuildArgFile());
  base::CreateDirectory(build_arg_file.DirName());

  std::string contents = args_input_file_->contents();
  commands::FormatStringToString(contents, false, &contents);
#if defined(OS_WIN)
  // Use Windows lineendings for this file since it will often open in
  // Notepad which can't handle Unix ones.
  base::ReplaceSubstringsAfterOffset(&contents, 0, "\n", "\r\n");
#endif
  if (base::WriteFile(build_arg_file, contents.c_str(),
      static_cast<int>(contents.size())) == -1) {
    Err(Location(), "Args file could not be written.",
      "The file is \"" + FilePathToUTF8(build_arg_file) +
        "\"").PrintToStdout();
    return false;
  }

  // Add a dependency on the build arguments file. If this changes, we want
  // to re-generate the build.
  g_scheduler->AddGenDependency(build_arg_file);

  return true;
}

bool Setup::FillSourceDir(const base::CommandLine& cmdline) {
  // Find the .gn file.
  base::FilePath root_path;

  // Prefer the command line args to the config file.
  base::FilePath relative_root_path =
      cmdline.GetSwitchValuePath(switches::kRoot);
  if (!relative_root_path.empty()) {
    root_path = base::MakeAbsoluteFilePath(relative_root_path);
    if (root_path.empty()) {
      Err(Location(), "Root source path not found.",
          "The path \"" + FilePathToUTF8(relative_root_path) +
          "\" doesn't exist.").PrintToStdout();
      return false;
    }

    // When --root is specified, an alternate --dotfile can also be set.
    // --dotfile should be a real file path and not a "//foo" source-relative
    // path.
    base::FilePath dot_file_path =
        cmdline.GetSwitchValuePath(switches::kDotfile);
    if (dot_file_path.empty()) {
      dotfile_name_ = root_path.Append(kGnFile);
    } else {
      dotfile_name_ = base::MakeAbsoluteFilePath(dot_file_path);
      if (dotfile_name_.empty()) {
        Err(Location(), "Could not load dotfile.",
            "The file \"" + FilePathToUTF8(dot_file_path) +
            "\" couldn't be loaded.").PrintToStdout();
        return false;
      }
    }
  } else {
    // In the default case, look for a dotfile and that also tells us where the
    // source root is.
    base::FilePath cur_dir;
    base::GetCurrentDirectory(&cur_dir);
    dotfile_name_ = FindDotFile(cur_dir);
    if (dotfile_name_.empty()) {
      Err(Location(), "Can't find source root.",
          "I could not find a \".gn\" file in the current directory or any "
          "parent,\nand the --root command-line argument was not specified.")
          .PrintToStdout();
      return false;
    }
    root_path = dotfile_name_.DirName();
  }

  base::FilePath root_realpath = base::MakeAbsoluteFilePath(root_path);
  if (root_realpath.empty()) {
    Err(Location(), "Can't get the real root path.",
        "I could not get the real path of \"" + FilePathToUTF8(root_path) +
        "\".").PrintToStdout();
    return false;
  }
  if (scheduler_.verbose_logging())
    scheduler_.Log("Using source root", FilePathToUTF8(root_realpath));
  build_settings_.SetRootPath(root_realpath);

  return true;
}

bool Setup::FillBuildDir(const std::string& build_dir, bool require_exists) {
  Err err;
  SourceDir resolved =
      SourceDirForCurrentDirectory(build_settings_.root_path()).
    ResolveRelativeDir(Value(nullptr, build_dir), &err,
        build_settings_.root_path_utf8());
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  base::FilePath build_dir_path = build_settings_.GetFullPath(resolved);
  if (!base::CreateDirectory(build_dir_path)) {
    Err(Location(), "Can't create the build dir.",
        "I could not create the build dir \"" + FilePathToUTF8(build_dir_path) +
        "\".").PrintToStdout();
    return false;
  }
  base::FilePath build_dir_realpath =
      base::MakeAbsoluteFilePath(build_dir_path);
  if (build_dir_realpath.empty()) {
    Err(Location(), "Can't get the real build dir path.",
        "I could not get the real path of \"" + FilePathToUTF8(build_dir_path) +
        "\".").PrintToStdout();
    return false;
  }
  resolved = SourceDirForPath(build_settings_.root_path(),
                              build_dir_realpath);

  if (scheduler_.verbose_logging())
    scheduler_.Log("Using build dir", resolved.value());

  if (require_exists) {
    if (!base::PathExists(build_dir_path.Append(
            FILE_PATH_LITERAL("build.ninja")))) {
      Err(Location(), "Not a build directory.",
          "This command requires an existing build directory. I interpreted "
          "your input\n\"" + build_dir + "\" as:\n  " +
          FilePathToUTF8(build_dir_path) +
          "\nwhich doesn't seem to contain a previously-generated build.")
          .PrintToStdout();
      return false;
    }
  }

  build_settings_.SetBuildDir(resolved);
  return true;
}

bool Setup::FillPythonPath(const base::CommandLine& cmdline) {
  // Trace this since it tends to be a bit slow on Windows.
  ScopedTrace setup_trace(TraceItem::TRACE_SETUP, "Fill Python Path");
  const Value* value = dotfile_scope_.GetValue("script_executable", true);
  if (cmdline.HasSwitch(switches::kScriptExecutable)) {
    build_settings_.set_python_path(
        cmdline.GetSwitchValuePath(switches::kScriptExecutable));
  } else if (value) {
    Err err;
    if (!value->VerifyTypeIs(Value::STRING, &err)) {
      err.PrintToStdout();
      return false;
    }
    build_settings_.set_python_path(
        base::FilePath(UTF8ToFilePath(value->string_value())));
  } else {
#if defined(OS_WIN)
    base::FilePath python_path = FindWindowsPython();
    if (python_path.empty()) {
      scheduler_.Log("WARNING", "Could not find python on path, using "
          "just \"python.exe\"");
      python_path = base::FilePath(kPythonExeName);
    }
    build_settings_.set_python_path(python_path.NormalizePathSeparatorsTo('/'));
#else
    build_settings_.set_python_path(base::FilePath("python"));
#endif
  }
  return true;
}

bool Setup::RunConfigFile() {
  if (scheduler_.verbose_logging())
    scheduler_.Log("Got dotfile", FilePathToUTF8(dotfile_name_));

  dotfile_input_file_.reset(new InputFile(SourceFile("//.gn")));
  if (!dotfile_input_file_->Load(dotfile_name_)) {
    Err(Location(), "Could not load dotfile.",
        "The file \"" + FilePathToUTF8(dotfile_name_) + "\" couldn't be loaded")
        .PrintToStdout();
    return false;
  }

  Err err;
  dotfile_tokens_ = Tokenizer::Tokenize(dotfile_input_file_.get(), &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  dotfile_root_ = Parser::Parse(dotfile_tokens_, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  dotfile_root_->Execute(&dotfile_scope_, &err);
  if (err.has_error()) {
    err.PrintToStdout();
    return false;
  }

  return true;
}

bool Setup::FillOtherConfig(const base::CommandLine& cmdline) {
  Err err;
  SourceDir current_dir("//");
  Label root_target_label(current_dir, "");

  // Secondary source path, read from the config file if present.
  // Read from the config file if present.
  const Value* secondary_value =
      dotfile_scope_.GetValue("secondary_source", true);
  if (secondary_value) {
    if (!secondary_value->VerifyTypeIs(Value::STRING, &err)) {
      err.PrintToStdout();
      return false;
    }
    build_settings_.SetSecondarySourcePath(
        SourceDir(secondary_value->string_value()));
  }

  // Root build file.
  const Value* root_value = dotfile_scope_.GetValue("root", true);
  if (root_value) {
    if (!root_value->VerifyTypeIs(Value::STRING, &err)) {
      err.PrintToStdout();
      return false;
    }

    root_target_label = Label::Resolve(current_dir, Label(), *root_value, &err);
    if (err.has_error()) {
      err.PrintToStdout();
      return false;
    }

    root_build_file_ = Loader::BuildFileForLabel(root_target_label);
  }
  build_settings_.SetRootTargetLabel(root_target_label);

  // Build config file.
  const Value* build_config_value =
      dotfile_scope_.GetValue("buildconfig", true);
  if (!build_config_value) {
    Err(Location(), "No build config file.",
        "Your .gn file (\"" + FilePathToUTF8(dotfile_name_) + "\")\n"
        "didn't specify a \"buildconfig\" value.").PrintToStdout();
    return false;
  } else if (!build_config_value->VerifyTypeIs(Value::STRING, &err)) {
    err.PrintToStdout();
    return false;
  }
  build_settings_.set_build_config_file(
      SourceFile(build_config_value->string_value()));

  // Targets to check.
  const Value* check_targets_value =
      dotfile_scope_.GetValue("check_targets", true);
  if (check_targets_value) {
    check_patterns_.reset(new std::vector<LabelPattern>);
    ExtractListOfLabelPatterns(*check_targets_value, current_dir,
                               check_patterns_.get(), &err);
    if (err.has_error()) {
      err.PrintToStdout();
      return false;
    }
  }

  // Fill exec_script_whitelist.
  const Value* exec_script_whitelist_value =
      dotfile_scope_.GetValue("exec_script_whitelist", true);
  if (exec_script_whitelist_value) {
    // Fill the list of targets to check.
    if (!exec_script_whitelist_value->VerifyTypeIs(Value::LIST, &err)) {
      err.PrintToStdout();
      return false;
    }
    std::unique_ptr<std::set<SourceFile>> whitelist(new std::set<SourceFile>);
    for (const auto& item : exec_script_whitelist_value->list_value()) {
      if (!item.VerifyTypeIs(Value::STRING, &err)) {
        err.PrintToStdout();
        return false;
      }
      whitelist->insert(current_dir.ResolveRelativeFile(item, &err));
      if (err.has_error()) {
        err.PrintToStdout();
        return false;
      }
    }
    build_settings_.set_exec_script_whitelist(std::move(whitelist));
  }

  // Fill optional default_args.
  const Value* default_args_value =
      dotfile_scope_.GetValue("default_args", true);
  if (default_args_value) {
    if (!default_args_value->VerifyTypeIs(Value::SCOPE, &err)) {
      err.PrintToStdout();
      return false;
    }

    default_args_ = default_args_value->scope_value();
  }

  const Value* arg_file_template_value =
      dotfile_scope_.GetValue("arg_file_template", true);
  if (arg_file_template_value) {
    if (!arg_file_template_value->VerifyTypeIs(Value::STRING, &err)) {
      err.PrintToStdout();
      return false;
    }
    SourceFile path(arg_file_template_value->string_value());
    build_settings_.set_arg_file_template_path(path);
  }

  return true;
}
