// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SUBSTITUTION_WRITER_H_
#define TOOLS_GN_SUBSTITUTION_WRITER_H_

#include <iosfwd>
#include <vector>

#include "tools/gn/substitution_type.h"

struct EscapeOptions;
class OutputFile;
class Settings;
class SourceDir;
class SourceFile;
class SubstitutionList;
class SubstitutionPattern;
class Target;
class Tool;

// Help text for script source expansion.
extern const char kSourceExpansion_Help[];

// This class handles writing or applying substitution patterns to strings.
//
// There are several different uses:
//
//  - Source substitutions: These are used to compute action_foreach
//    outputs and arguments. Functions are provided to expand these in terms
//    of both OutputFiles (for writing Ninja files) as well as SourceFiles
//    (for computing lists used by code).
//
//  - Target substitutions: These are specific to the target+tool combination
//    and are shared between the compiler and linker ones. It includes things
//    like the target_gen_dir.
//
//  - Compiler substitutions: These are used to compute compiler outputs.
//    It includes all source substitutions (since they depend on the various
//    parts of the source file) as well as the target substitutions.
//
//  - Linker substitutions: These are used to compute linker outputs. It
//    includes the target substitutions.
//
// The compiler and linker specific substitutions do NOT include the various
// cflags, ldflags, libraries, etc. These are written by the ninja target
// writer since they depend on traversing the dependency tree.
//
// The methods which take a target as an argument can accept null target
// pointer if there is no target context, in which case the substitutions
// requiring target context will not work.
class SubstitutionWriter {
 public:
  enum OutputStyle {
    OUTPUT_ABSOLUTE,  // Dirs will be absolute "//foo/bar".
    OUTPUT_RELATIVE,  // Dirs will be relative to a given directory.
  };

  // Writes the pattern to the given stream with no special handling, and with
  // Ninja variables replacing the patterns.
  static void WriteWithNinjaVariables(
      const SubstitutionPattern& pattern,
      const EscapeOptions& escape_options,
      std::ostream& out);

  // NOP substitutions ---------------------------------------------------------

  // Converts the given SubstitutionList to OutputFiles assuming there are
  // no substitutions (it will assert if there are). This is used for cases
  // like actions where the outputs are explicit, but the list is stored as
  // a SubstitutionList.
  static void GetListAsSourceFiles(
      const SubstitutionList& list,
      std::vector<SourceFile>* output);
  static void GetListAsOutputFiles(
      const Settings* settings,
      const SubstitutionList& list,
      std::vector<OutputFile>* output);

  // Source substitutions -----------------------------------------------------

  // Applies the substitution pattern to a source file, returning the result
  // as either a string, a SourceFile or an OutputFile. If the result is
  // expected to be a SourceFile or an OutputFile, this will CHECK if the
  // result isn't in the correct directory. The caller should validate this
  // first (see for example IsFileInOuputDir).
  //
  // The target can be null (see class comment above).
  static SourceFile ApplyPatternToSource(
      const Target* target,
      const Settings* settings,
      const SubstitutionPattern& pattern,
      const SourceFile& source);
  static std::string ApplyPatternToSourceAsString(
      const Target* target,
      const Settings* settings,
      const SubstitutionPattern& pattern,
      const SourceFile& source);
  static OutputFile ApplyPatternToSourceAsOutputFile(
      const Target* target,
      const Settings* settings,
      const SubstitutionPattern& pattern,
      const SourceFile& source);

  // Applies the substitution list to a source, APPENDING the result to the
  // given output vector. It works this way so one can call multiple times to
  // apply to multiple files and create a list. The result can either be
  // SourceFiles or OutputFiles.
  //
  // The target can be null (see class comment above).
  static void ApplyListToSource(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const SourceFile& source,
      std::vector<SourceFile>* output);
  static void ApplyListToSourceAsString(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const SourceFile& source,
      std::vector<std::string>* output);
  static void ApplyListToSourceAsOutputFile(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const SourceFile& source,
      std::vector<OutputFile>* output);

  // Like ApplyListToSource but applies the list to all sources and replaces
  // rather than appends the output (this produces the complete output).
  //
  // The target can be null (see class comment above).
  static void ApplyListToSources(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const std::vector<SourceFile>& sources,
      std::vector<SourceFile>* output);
  static void ApplyListToSourcesAsString(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const std::vector<SourceFile>& sources,
      std::vector<std::string>* output);
  static void ApplyListToSourcesAsOutputFile(
      const Target* target,
      const Settings* settings,
      const SubstitutionList& list,
      const std::vector<SourceFile>& sources,
      std::vector<OutputFile>* output);

  // Given a list of source replacement types used, writes the Ninja variable
  // definitions for the given source file to use for those replacements. The
  // variables will be indented two spaces. Since this is for writing to
  // Ninja files, paths will be relative to the build dir, and no definition
  // for {{source}} will be written since that maps to Ninja's implicit $in
  // variable.
  //
  // The target can be null (see class comment above).
  static void WriteNinjaVariablesForSource(
      const Target* target,
      const Settings* settings,
      const SourceFile& source,
      const std::vector<SubstitutionType>& types,
      const EscapeOptions& escape_options,
      std::ostream& out);

  // Extracts the given type of substitution related to a source file from the
  // given source file. If output_style is OUTPUT_RELATIVE, relative_to
  // indicates the directory that the relative directories should be relative
  // to, otherwise it is ignored.
  //
  // The target can be null (see class comment above).
  static std::string GetSourceSubstitution(
      const Target* target,
      const Settings* settings,
      const SourceFile& source,
      SubstitutionType type,
      OutputStyle output_style,
      const SourceDir& relative_to);

  // Target substitutions ------------------------------------------------------
  //
  // Handles the target substitutions that apply to both compiler and linker
  // tools.
  static OutputFile ApplyPatternToTargetAsOutputFile(
      const Target* target,
      const Tool* tool,
      const SubstitutionPattern& pattern);
  static void ApplyListToTargetAsOutputFile(
      const Target* target,
      const Tool* tool,
      const SubstitutionList& list,
      std::vector<OutputFile>* output);

  // This function is slightly different than the other substitution getters
  // since it can handle failure (since it is designed to be used by the
  // compiler and linker ones which will fall through if it's not a common tool
  // one).
  static bool GetTargetSubstitution(
      const Target* target,
      SubstitutionType type,
      std::string* result);
  static std::string GetTargetSubstitution(
      const Target* target,
      SubstitutionType type);

  // Compiler substitutions ----------------------------------------------------
  //
  // A compiler substitution allows both source and tool substitutions. These
  // are used to compute output names for compiler tools.

  static OutputFile ApplyPatternToCompilerAsOutputFile(
      const Target* target,
      const SourceFile& source,
      const SubstitutionPattern& pattern);
  static void ApplyListToCompilerAsOutputFile(
      const Target* target,
      const SourceFile& source,
      const SubstitutionList& list,
      std::vector<OutputFile>* output);

  // Like GetSourceSubstitution but for strings based on the target or
  // toolchain. This type of result will always be relative to the build
  // directory.
  static std::string GetCompilerSubstitution(
      const Target* target,
      const SourceFile& source,
      SubstitutionType type);

  // Linker substitutions ------------------------------------------------------

  static OutputFile ApplyPatternToLinkerAsOutputFile(
      const Target* target,
      const Tool* tool,
      const SubstitutionPattern& pattern);
  static void ApplyListToLinkerAsOutputFile(
      const Target* target,
      const Tool* tool,
      const SubstitutionList& list,
      std::vector<OutputFile>* output);

  // Like GetSourceSubstitution but for strings based on the target or
  // toolchain. This type of result will always be relative to the build
  // directory.
  static std::string GetLinkerSubstitution(
      const Target* target,
      const Tool* tool,
      SubstitutionType type);
};

#endif  // TOOLS_GN_SUBSTITUTION_WRITER_H_
