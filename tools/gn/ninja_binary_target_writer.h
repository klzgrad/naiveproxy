// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_BINARY_TARGET_WRITER_H_
#define TOOLS_GN_NINJA_BINARY_TARGET_WRITER_H_

#include "base/macros.h"
#include "tools/gn/config_values.h"
#include "tools/gn/ninja_target_writer.h"
#include "tools/gn/toolchain.h"
#include "tools/gn/unique_vector.h"

struct EscapeOptions;
class SourceFileTypeSet;

// Writes a .ninja file for a binary target type (an executable, a shared
// library, or a static library).
class NinjaBinaryTargetWriter : public NinjaTargetWriter {
 public:
  class SourceFileTypeSet;

  NinjaBinaryTargetWriter(const Target* target, std::ostream& out);
  ~NinjaBinaryTargetWriter() override;

  void Run() override;

 private:
  typedef std::set<OutputFile> OutputFileSet;

  // Writes all flags for the compiler: includes, defines, cflags, etc.
  void WriteCompilerVars(const SourceFileTypeSet& used_types);

  // Writes to the output stream a stamp rule for inputs, and
  // returns the file to be appended to source rules that encodes the
  // implicit dependencies for the current target. The returned OutputFile
  // will be empty if there are no inputs.
  OutputFile WriteInputsStampAndGetDep() const;

  // has_precompiled_headers is set when this substitution matches a tool type
  // that supports precompiled headers, and this target supports precompiled
  // headers. It doesn't indicate if the tool has precompiled headers (this
  // will be looked up by this function).
  //
  // The tool_type indicates the corresponding tool for flags that are
  // tool-specific (e.g. "cflags_c"). For non-tool-specific flags (e.g.
  // "defines") tool_type should be TYPE_NONE.
  void WriteOneFlag(
      SubstitutionType subst_enum,
      bool has_precompiled_headers,
      Toolchain::ToolType tool_type,
      const std::vector<std::string>& (ConfigValues::* getter)() const,
      EscapeOptions flag_escape_options);

  // Writes build lines required for precompiled headers. Any generated
  // object files will be appended to the |object_files|. Any generated
  // non-object files (for instance, .gch files from a GCC toolchain, are
  // appended to |other_files|).
  //
  // input_dep is the stamp file collecting the dependencies required before
  // compiling this target. It will be empty if there are no input deps.
  void WritePCHCommands(const SourceFileTypeSet& used_types,
                        const OutputFile& input_dep,
                        const OutputFile& order_only_dep,
                        std::vector<OutputFile>* object_files,
                        std::vector<OutputFile>* other_files);

  // Writes a .pch compile build line for a language type.
  void WritePCHCommand(SubstitutionType flag_type,
                       Toolchain::ToolType tool_type,
                       Tool::PrecompiledHeaderType header_type,
                       const OutputFile& input_dep,
                       const OutputFile& order_only_dep,
                       std::vector<OutputFile>* object_files,
                       std::vector<OutputFile>* other_files);

  void WriteGCCPCHCommand(SubstitutionType flag_type,
                          Toolchain::ToolType tool_type,
                          const OutputFile& input_dep,
                          const OutputFile& order_only_dep,
                          std::vector<OutputFile>* gch_files);

  void WriteWindowsPCHCommand(SubstitutionType flag_type,
                              Toolchain::ToolType tool_type,
                              const OutputFile& input_dep,
                              const OutputFile& order_only_dep,
                              std::vector<OutputFile>* object_files);

  // pch_deps are additional dependencies to run before the rule. They are
  // expected to abide by the naming conventions specified by GetPCHOutputFiles.
  //
  // order_only_dep is the name of the stamp file that covers the dependencies
  // that must be run before doing any compiles.
  //
  // The files produced by the compiler will be added to two output vectors.
  void WriteSources(const std::vector<OutputFile>& pch_deps,
                    const OutputFile& input_dep,
                    const OutputFile& order_only_dep,
                    std::vector<OutputFile>* object_files,
                    std::vector<SourceFile>* other_files);

  // Writes a build line.
  void WriteCompilerBuildLine(const SourceFile& source,
                              const std::vector<OutputFile>& extra_deps,
                              const OutputFile& order_only_dep,
                              Toolchain::ToolType tool_type,
                              const std::vector<OutputFile>& outputs);

  void WriteLinkerStuff(const std::vector<OutputFile>& object_files,
                        const std::vector<SourceFile>& other_files);
  void WriteLinkerFlags(const SourceFile* optional_def_file);
  void WriteLibs();
  void WriteOutputSubstitutions();
  void WriteSolibs(const std::vector<OutputFile>& solibs);

  // Writes the stamp line for a source set. These are not linked.
  void WriteSourceSetStamp(const std::vector<OutputFile>& object_files);

  // Gets all target dependencies and classifies them, as well as accumulates
  // object files from source sets we need to link.
  void GetDeps(UniqueVector<OutputFile>* extra_object_files,
               UniqueVector<const Target*>* linkable_deps,
               UniqueVector<const Target*>* non_linkable_deps) const;

  // Classifies the dependency as linkable or nonlinkable with the current
  // target, adding it to the appropriate vector. If the dependency is a source
  // set we should link in, the source set's object files will be appended to
  // |extra_object_files|.
  void ClassifyDependency(const Target* dep,
                          UniqueVector<OutputFile>* extra_object_files,
                          UniqueVector<const Target*>* linkable_deps,
                          UniqueVector<const Target*>* non_linkable_deps) const;

  // Writes the implicit dependencies for the link or stamp line. This is
  // the "||" and everything following it on the ninja line.
  //
  // The order-only dependencies are the non-linkable deps passed in as an
  // argument, plus the data file depdencies in the target.
  void WriteOrderOnlyDependencies(
      const UniqueVector<const Target*>& non_linkable_deps);

  // Returns the computed name of the Windows .pch file for the given
  // tool type. The tool must support precompiled headers.
  OutputFile GetWindowsPCHFile(Toolchain::ToolType tool_type) const;

  // Checks for duplicates in the given list of output files. If any duplicates
  // are found, throws an error and return false.
  bool CheckForDuplicateObjectFiles(const std::vector<OutputFile>& files) const;

  const Tool* tool_;

  // Cached version of the prefix used for rule types for this toolchain.
  std::string rule_prefix_;

  DISALLOW_COPY_AND_ASSIGN(NinjaBinaryTargetWriter);
};

#endif  // TOOLS_GN_NINJA_BINARY_TARGET_WRITER_H_

