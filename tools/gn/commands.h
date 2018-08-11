// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_COMMANDS_H_
#define TOOLS_GN_COMMANDS_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/strings/string_piece.h"
#include "base/values.h"
#include "tools/gn/target.h"
#include "tools/gn/unique_vector.h"

class BuildSettings;
class Config;
class LabelPattern;
class Setup;
class SourceFile;
class Target;
class Toolchain;

// Each "Run" command returns the value we should return from main().

namespace commands {

typedef int (*CommandRunner)(const std::vector<std::string>&);

extern const char kAnalyze[];
extern const char kAnalyze_HelpShort[];
extern const char kAnalyze_Help[];
int RunAnalyze(const std::vector<std::string>& args);

extern const char kArgs[];
extern const char kArgs_HelpShort[];
extern const char kArgs_Help[];
int RunArgs(const std::vector<std::string>& args);

extern const char kCheck[];
extern const char kCheck_HelpShort[];
extern const char kCheck_Help[];
int RunCheck(const std::vector<std::string>& args);

extern const char kClean[];
extern const char kClean_HelpShort[];
extern const char kClean_Help[];
int RunClean(const std::vector<std::string>& args);

extern const char kDesc[];
extern const char kDesc_HelpShort[];
extern const char kDesc_Help[];
int RunDesc(const std::vector<std::string>& args);

extern const char kGen[];
extern const char kGen_HelpShort[];
extern const char kGen_Help[];
int RunGen(const std::vector<std::string>& args);

extern const char kFormat[];
extern const char kFormat_HelpShort[];
extern const char kFormat_Help[];
int RunFormat(const std::vector<std::string>& args);

extern const char kHelp[];
extern const char kHelp_HelpShort[];
extern const char kHelp_Help[];
int RunHelp(const std::vector<std::string>& args);

extern const char kLs[];
extern const char kLs_HelpShort[];
extern const char kLs_Help[];
int RunLs(const std::vector<std::string>& args);

extern const char kPath[];
extern const char kPath_HelpShort[];
extern const char kPath_Help[];
int RunPath(const std::vector<std::string>& args);

extern const char kRefs[];
extern const char kRefs_HelpShort[];
extern const char kRefs_Help[];
int RunRefs(const std::vector<std::string>& args);

// -----------------------------------------------------------------------------

struct CommandInfo {
  CommandInfo();
  CommandInfo(const char* in_help_short,
              const char* in_help,
              CommandRunner in_runner);

  const char* help_short;
  const char* help;
  CommandRunner runner;
};

typedef std::map<base::StringPiece, CommandInfo> CommandInfoMap;

const CommandInfoMap& GetCommands();

// Helper functions for some commands ------------------------------------------

// Given a setup that has already been run and some command-line input,
// resolves that input as a target label and returns the corresponding target.
// On failure, returns null and prints the error to the standard output.
const Target* ResolveTargetFromCommandLineString(
    Setup* setup,
    const std::string& label_string);

// Resolves a vector of command line inputs and figures out the full set of
// things they resolve to.
//
// Patterns with wildcards will only match targets. The file_matches aren't
// validated that they are real files or referenced by any targets. They're just
// the set of things that didn't match anything else.
bool ResolveFromCommandLineInput(
    Setup* setup,
    const std::vector<std::string>& input,
    bool all_toolchains,
    UniqueVector<const Target*>* target_matches,
    UniqueVector<const Config*>* config_matches,
    UniqueVector<const Toolchain*>* toolchain_matches,
    UniqueVector<SourceFile>* file_matches);

// Runs the header checker. All targets in the build should be given in
// all_targets, and the specific targets to check should be in to_check.
//
// force_check, if true, will override targets opting out of header checking
// with "check_includes = false" and will check them anyway.
//
// On success, returns true. If the check fails, the error(s) will be printed
// to stdout and false will be returned.
bool CheckPublicHeaders(const BuildSettings* build_settings,
                        const std::vector<const Target*>& all_targets,
                        const std::vector<const Target*>& to_check,
                        bool force_check);

// Filters the given list of targets by the given pattern list.
void FilterTargetsByPatterns(const std::vector<const Target*>& input,
                             const std::vector<LabelPattern>& filter,
                             std::vector<const Target*>* output);
void FilterTargetsByPatterns(const std::vector<const Target*>& input,
                             const std::vector<LabelPattern>& filter,
                             UniqueVector<const Target*>* output);

// Builds a list of pattern from a semicolon-separated list of labels.
bool FilterPatternsFromString(const BuildSettings* build_settings,
                              const std::string& label_list_string,
                              std::vector<LabelPattern>* filters,
                              Err* err);

// These are the documentation strings for the command-line flags used by
// FilterAndPrintTargets. Commands that call that function should incorporate
// these into their help.
#define TARGET_PRINTING_MODE_COMMAND_LINE_HELP \
    "  --as=(buildfile|label|output)\n"\
    "      How to print targets.\n"\
    "\n"\
    "      buildfile\n"\
    "          Prints the build files where the given target was declared as\n"\
    "          file names.\n"\
    "      label  (default)\n"\
    "          Prints the label of the target.\n"\
    "      output\n"\
    "          Prints the first output file for the target relative to the\n"\
    "          root build directory.\n"
#define TARGET_TYPE_FILTER_COMMAND_LINE_HELP \
    "  --type=(action|copy|executable|group|loadable_module|shared_library|\n"\
    "          source_set|static_library)\n"\
    "      Restrict outputs to targets matching the given type. If\n"\
    "      unspecified, no filtering will be performed.\n"
#define TARGET_TESTONLY_FILTER_COMMAND_LINE_HELP \
    "  --testonly=(true|false)\n"\
    "      Restrict outputs to targets with the testonly flag set\n"\
    "      accordingly. When unspecified, the target's testonly flags are\n"\
    "      ignored.\n"

// Applies any testonly and type filters specified on the command line,
// and prints the targets as specified by the --as command line flag.
//
// If indent is true, the results will be indented two spaces.
//
// The vector will be modified so that only the printed targets will remain.
void FilterAndPrintTargets(bool indent, std::vector<const Target*>* targets);
void FilterAndPrintTargets(std::vector<const Target*>* targets,
                           base::ListValue* out);

void FilterAndPrintTargetSet(bool indent,
                             const std::set<const Target*>& targets);
void FilterAndPrintTargetSet(const std::set<const Target*>& targets,
                             base::ListValue* out);

// Extra help from command_check.cc
extern const char kNoGnCheck_Help[];

}  // namespace commands

#endif  // TOOLS_GN_COMMANDS_H_
