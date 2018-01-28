// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/ninja_binary_target_writer.h"

#include <stddef.h>
#include <string.h>

#include <cstring>
#include <set>
#include <sstream>

#include "base/containers/hash_tables.h"
#include "base/strings/string_util.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/err.h"
#include "tools/gn/escape.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/ninja_utils.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/settings.h"
#include "tools/gn/source_file_type.h"
#include "tools/gn/string_utils.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"

// Represents a set of tool types. Must be first since it is also shared by
// some helper functions in the anonymous namespace below.
class NinjaBinaryTargetWriter::SourceFileTypeSet {
 public:
  SourceFileTypeSet() {
    memset(flags_, 0, sizeof(bool) * static_cast<int>(SOURCE_NUMTYPES));
  }

  void Set(SourceFileType type) {
    flags_[static_cast<int>(type)] = true;
  }
  bool Get(SourceFileType type) const {
    return flags_[static_cast<int>(type)];
  }

 private:
  bool flags_[static_cast<int>(SOURCE_NUMTYPES)];
};

namespace {

// Returns the proper escape options for writing compiler and linker flags.
EscapeOptions GetFlagOptions() {
  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA_COMMAND;
  return opts;
}

struct DefineWriter {
  DefineWriter() {
    options.mode = ESCAPE_NINJA_COMMAND;
  }

  void operator()(const std::string& s, std::ostream& out) const {
    out << " -D";
    EscapeStringToStream(out, s, options);
  }

  EscapeOptions options;
};

struct IncludeWriter {
  explicit IncludeWriter(PathOutput& path_output) : path_output_(path_output) {
  }
  ~IncludeWriter() {
  }

  void operator()(const SourceDir& d, std::ostream& out) const {
    std::ostringstream path_out;
    path_output_.WriteDir(path_out, d, PathOutput::DIR_NO_LAST_SLASH);
    const std::string& path = path_out.str();
    if (path[0] == '"')
      out << " \"-I" << path.substr(1);
    else
      out << " -I" << path;
  }

  PathOutput& path_output_;
};

// Returns the language-specific suffix for precompiled header files.
const char* GetPCHLangSuffixForToolType(Toolchain::ToolType type) {
  switch (type) {
    case Toolchain::TYPE_CC:
      return "c";
    case Toolchain::TYPE_CXX:
      return "cc";
    case Toolchain::TYPE_OBJC:
      return "m";
    case Toolchain::TYPE_OBJCXX:
      return "mm";
    default:
      NOTREACHED() << "Not a valid PCH tool type: " << type;
      return "";
  }
}

std::string GetWindowsPCHObjectExtension(Toolchain::ToolType tool_type,
                                         const std::string& obj_extension) {
  const char* lang_suffix = GetPCHLangSuffixForToolType(tool_type);
  std::string result = ".";
  // For MSVC, annotate the obj files with the language type. For example:
  //   obj/foo/target_name.precompile.obj ->
  //   obj/foo/target_name.precompile.cc.obj
  result += lang_suffix;
  result += obj_extension;
  return result;
}

std::string GetGCCPCHOutputExtension(Toolchain::ToolType tool_type) {
  const char* lang_suffix = GetPCHLangSuffixForToolType(tool_type);
  std::string result = ".";
  // For GCC, the output name must have a .gch suffix and be annotated with
  // the language type. For example:
  //   obj/foo/target_name.header.h ->
  //   obj/foo/target_name.header.h-cc.gch
  // In order for the compiler to pick it up, the output name (minus the .gch
  // suffix MUST match whatever is passed to the -include flag).
  result += "h-";
  result += lang_suffix;
  result += ".gch";
  return result;
}

// Returns the language-specific lang recognized by gcc’s -x flag for
// precompiled header files.
const char* GetPCHLangForToolType(Toolchain::ToolType type) {
  switch (type) {
    case Toolchain::TYPE_CC:
      return "c-header";
    case Toolchain::TYPE_CXX:
      return "c++-header";
    case Toolchain::TYPE_OBJC:
      return "objective-c-header";
    case Toolchain::TYPE_OBJCXX:
      return "objective-c++-header";
    default:
      NOTREACHED() << "Not a valid PCH tool type: " << type;
      return "";
  }
}

// Fills |outputs| with the object or gch file for the precompiled header of the
// given type (flag type and tool type must match).
void GetPCHOutputFiles(const Target* target,
                       Toolchain::ToolType tool_type,
                       std::vector<OutputFile>* outputs) {
  outputs->clear();

  // Compute the tool. This must use the tool type passed in rather than the
  // detected file type of the precompiled source file since the same
  // precompiled source file will be used for separate C/C++ compiles.
  const Tool* tool = target->toolchain()->GetTool(tool_type);
  if (!tool)
    return;
  SubstitutionWriter::ApplyListToCompilerAsOutputFile(
      target, target->config_values().precompiled_source(),
      tool->outputs(), outputs);

  if (outputs->empty())
    return;
  if (outputs->size() > 1)
    outputs->resize(1);  // Only link the first output from the compiler tool.

  std::string& output_value = (*outputs)[0].value();
  size_t extension_offset = FindExtensionOffset(output_value);
  if (extension_offset == std::string::npos) {
    // No extension found.
    return;
  }
  DCHECK(extension_offset >= 1);
  DCHECK(output_value[extension_offset - 1] == '.');

  std::string output_extension;
  Tool::PrecompiledHeaderType header_type = tool->precompiled_header_type();
  switch (header_type) {
    case Tool::PCH_MSVC:
      output_extension = GetWindowsPCHObjectExtension(
          tool_type, output_value.substr(extension_offset - 1));
      break;
    case Tool::PCH_GCC:
      output_extension = GetGCCPCHOutputExtension(tool_type);
      break;
    case Tool::PCH_NONE:
      NOTREACHED() << "No outputs for no PCH type.";
      break;
  }
  output_value.replace(extension_offset - 1,
                       std::string::npos,
                       output_extension);
}

// Appends the object files generated by the given source set to the given
// output vector.
void AddSourceSetObjectFiles(const Target* source_set,
                             UniqueVector<OutputFile>* obj_files) {
  std::vector<OutputFile> tool_outputs;  // Prevent allocation in loop.
  NinjaBinaryTargetWriter::SourceFileTypeSet used_types;

  // Compute object files for all sources. Only link the first output from
  // the tool if there are more than one.
  for (const auto& source : source_set->sources()) {
    Toolchain::ToolType tool_type = Toolchain::TYPE_NONE;
    if (source_set->GetOutputFilesForSource(source, &tool_type, &tool_outputs))
      obj_files->push_back(tool_outputs[0]);

    used_types.Set(GetSourceFileType(source));
  }

  // Add MSVC precompiled header object files. GCC .gch files are not object
  // files so they are omitted.
  if (source_set->config_values().has_precompiled_headers()) {
    if (used_types.Get(SOURCE_C)) {
      const Tool* tool = source_set->toolchain()->GetTool(Toolchain::TYPE_CC);
      if (tool && tool->precompiled_header_type() == Tool::PCH_MSVC) {
        GetPCHOutputFiles(source_set, Toolchain::TYPE_CC, &tool_outputs);
        obj_files->Append(tool_outputs.begin(), tool_outputs.end());
      }
    }
    if (used_types.Get(SOURCE_CPP)) {
      const Tool* tool = source_set->toolchain()->GetTool(Toolchain::TYPE_CXX);
      if (tool && tool->precompiled_header_type() == Tool::PCH_MSVC) {
        GetPCHOutputFiles(source_set, Toolchain::TYPE_CXX, &tool_outputs);
        obj_files->Append(tool_outputs.begin(), tool_outputs.end());
      }
    }
    if (used_types.Get(SOURCE_M)) {
      const Tool* tool = source_set->toolchain()->GetTool(Toolchain::TYPE_OBJC);
      if (tool && tool->precompiled_header_type() == Tool::PCH_MSVC) {
        GetPCHOutputFiles(source_set, Toolchain::TYPE_OBJC, &tool_outputs);
        obj_files->Append(tool_outputs.begin(), tool_outputs.end());
      }
    }
    if (used_types.Get(SOURCE_MM)) {
      const Tool* tool = source_set->toolchain()->GetTool(
          Toolchain::TYPE_OBJCXX);
      if (tool && tool->precompiled_header_type() == Tool::PCH_MSVC) {
        GetPCHOutputFiles(source_set, Toolchain::TYPE_OBJCXX, &tool_outputs);
        obj_files->Append(tool_outputs.begin(), tool_outputs.end());
      }
    }
  }
}

}  // namespace

NinjaBinaryTargetWriter::NinjaBinaryTargetWriter(const Target* target,
                                                 std::ostream& out)
    : NinjaTargetWriter(target, out),
      tool_(target->toolchain()->GetToolForTargetFinalOutput(target)),
      rule_prefix_(GetNinjaRulePrefixForToolchain(settings_)) {
}

NinjaBinaryTargetWriter::~NinjaBinaryTargetWriter() {
}

void NinjaBinaryTargetWriter::Run() {
  // Figure out what source types are needed.
  SourceFileTypeSet used_types;
  for (const auto& source : target_->sources())
    used_types.Set(GetSourceFileType(source));

  WriteCompilerVars(used_types);

  OutputFile input_dep = WriteInputsStampAndGetDep();

  // The input dependencies will be an order-only dependency. This will cause
  // Ninja to make sure the inputs are up to date before compiling this source,
  // but changes in the inputs deps won't cause the file to be recompiled.
  //
  // This is important to prevent changes in unrelated actions that are
  // upstream of this target from causing everything to be recompiled.
  //
  // Why can we get away with this rather than using implicit deps ("|", which
  // will force rebuilds when the inputs change)? For source code, the
  // computed dependencies of all headers will be computed by the compiler,
  // which will cause source rebuilds if any "real" upstream dependencies
  // change.
  //
  // If a .cc file is generated by an input dependency, Ninja will see the
  // input to the build rule doesn't exist, and that it is an output from a
  // previous step, and build the previous step first. This is a "real"
  // dependency and doesn't need | or || to express.
  //
  // The only case where this rule matters is for the first build where no .d
  // files exist, and Ninja doesn't know what that source file depends on. In
  // this case it's sufficient to ensure that the upstream dependencies are
  // built first. This is exactly what Ninja's order-only dependencies
  // expresses.
  OutputFile order_only_dep =
      WriteInputDepsStampAndGetDep(std::vector<const Target*>());

  // For GCC builds, the .gch files are not object files, but still need to be
  // added as explicit dependencies below. The .gch output files are placed in
  // |pch_other_files|. This is to prevent linking against them.
  std::vector<OutputFile> pch_obj_files;
  std::vector<OutputFile> pch_other_files;
  WritePCHCommands(used_types, input_dep, order_only_dep,
                   &pch_obj_files, &pch_other_files);
  std::vector<OutputFile>* pch_files = !pch_obj_files.empty() ?
      &pch_obj_files : &pch_other_files;

  // Treat all pch output files as explicit dependencies of all
  // compiles that support them. Some notes:
  //
  //  - On Windows, the .pch file is the input to the compile, not the
  //    precompiled header's corresponding object file that we're using here.
  //    But Ninja's depslog doesn't support multiple outputs from the
  //    precompiled header compile step (it outputs both the .pch file and a
  //    corresponding .obj file). So we consistently list the .obj file and the
  //    .pch file we really need comes along with it.
  //
  //  - GCC .gch files are not object files, therefore they are not added to the
  //    object file list.
  std::vector<OutputFile> obj_files;
  std::vector<SourceFile> other_files;
  WriteSources(*pch_files, input_dep, order_only_dep, &obj_files, &other_files);

  // Link all MSVC pch object files. The vector will be empty on GCC toolchains.
  obj_files.insert(obj_files.end(), pch_obj_files.begin(), pch_obj_files.end());
  if (!CheckForDuplicateObjectFiles(obj_files))
    return;

  if (target_->output_type() == Target::SOURCE_SET) {
    WriteSourceSetStamp(obj_files);
#ifndef NDEBUG
    // Verify that the function that separately computes a source set's object
    // files match the object files just computed.
    UniqueVector<OutputFile> computed_obj;
    AddSourceSetObjectFiles(target_, &computed_obj);
    DCHECK_EQ(obj_files.size(), computed_obj.size());
    for (const auto& obj : obj_files)
      DCHECK_NE(static_cast<size_t>(-1), computed_obj.IndexOf(obj));
#endif
  } else {
    WriteLinkerStuff(obj_files, other_files);
  }
}

void NinjaBinaryTargetWriter::WriteCompilerVars(
    const SourceFileTypeSet& used_types) {
  const SubstitutionBits& subst = target_->toolchain()->substitution_bits();

  // Defines.
  if (subst.used[SUBSTITUTION_DEFINES]) {
    out_ << kSubstitutionNinjaNames[SUBSTITUTION_DEFINES] << " =";
    RecursiveTargetConfigToStream<std::string>(
        target_, &ConfigValues::defines, DefineWriter(), out_);
    out_ << std::endl;
  }

  // Include directories.
  if (subst.used[SUBSTITUTION_INCLUDE_DIRS]) {
    out_ << kSubstitutionNinjaNames[SUBSTITUTION_INCLUDE_DIRS] << " =";
    PathOutput include_path_output(
        path_output_.current_dir(),
        settings_->build_settings()->root_path_utf8(),
        ESCAPE_NINJA_COMMAND);
    RecursiveTargetConfigToStream<SourceDir>(
        target_, &ConfigValues::include_dirs,
        IncludeWriter(include_path_output), out_);
    out_ << std::endl;
  }

  bool has_precompiled_headers =
      target_->config_values().has_precompiled_headers();

  EscapeOptions opts = GetFlagOptions();
  if (used_types.Get(SOURCE_S) || used_types.Get(SOURCE_ASM)) {
    WriteOneFlag(SUBSTITUTION_ASMFLAGS, false, Toolchain::TYPE_NONE,
                 &ConfigValues::asmflags, opts);
  }
  if (used_types.Get(SOURCE_C) || used_types.Get(SOURCE_CPP) ||
      used_types.Get(SOURCE_M) || used_types.Get(SOURCE_MM)) {
    WriteOneFlag(SUBSTITUTION_CFLAGS, false, Toolchain::TYPE_NONE,
                 &ConfigValues::cflags, opts);
  }
  if (used_types.Get(SOURCE_C)) {
    WriteOneFlag(SUBSTITUTION_CFLAGS_C, has_precompiled_headers,
                 Toolchain::TYPE_CC, &ConfigValues::cflags_c, opts);
  }
  if (used_types.Get(SOURCE_CPP)) {
    WriteOneFlag(SUBSTITUTION_CFLAGS_CC, has_precompiled_headers,
                 Toolchain::TYPE_CXX, &ConfigValues::cflags_cc, opts);
  }
  if (used_types.Get(SOURCE_M)) {
    WriteOneFlag(SUBSTITUTION_CFLAGS_OBJC, has_precompiled_headers,
                 Toolchain::TYPE_OBJC, &ConfigValues::cflags_objc, opts);
  }
  if (used_types.Get(SOURCE_MM)) {
    WriteOneFlag(SUBSTITUTION_CFLAGS_OBJCC, has_precompiled_headers,
                 Toolchain::TYPE_OBJCXX, &ConfigValues::cflags_objcc, opts);
  }

  WriteSharedVars(subst);
}

OutputFile NinjaBinaryTargetWriter::WriteInputsStampAndGetDep() const {
  CHECK(target_->toolchain())
      << "Toolchain not set on target "
      << target_->label().GetUserVisibleName(true);

  if (target_->inputs().size() == 0)
    return OutputFile();  // No inputs

  // If we only have one input, return it directly instead of writing a stamp
  // file for it.
  if (target_->inputs().size() == 1)
    return OutputFile(settings_->build_settings(), target_->inputs()[0]);

  // Make a stamp file.
  OutputFile input_stamp_file =
      GetBuildDirForTargetAsOutputFile(target_, BuildDirType::OBJ);
  input_stamp_file.value().append(target_->label().name());
  input_stamp_file.value().append(".inputs.stamp");

  out_ << "build ";
  path_output_.WriteFile(out_, input_stamp_file);
  out_ << ": "
       << GetNinjaRulePrefixForToolchain(settings_)
       << Toolchain::ToolTypeToName(Toolchain::TYPE_STAMP);

  // File inputs.
  for (const auto& input : target_->inputs()) {
    out_ << " ";
    path_output_.WriteFile(out_, input);
  }

  out_ << "\n";
  return input_stamp_file;
}

void NinjaBinaryTargetWriter::WriteOneFlag(
    SubstitutionType subst_enum,
    bool has_precompiled_headers,
    Toolchain::ToolType tool_type,
    const std::vector<std::string>& (ConfigValues::* getter)() const,
    EscapeOptions flag_escape_options) {
  if (!target_->toolchain()->substitution_bits().used[subst_enum])
    return;

  out_ << kSubstitutionNinjaNames[subst_enum] << " =";

  if (has_precompiled_headers) {
    const Tool* tool = target_->toolchain()->GetTool(tool_type);
    if (tool && tool->precompiled_header_type() == Tool::PCH_MSVC) {
      // Name the .pch file.
      out_ << " /Fp";
      path_output_.WriteFile(out_, GetWindowsPCHFile(tool_type));

      // Enables precompiled headers and names the .h file. It's a string
      // rather than a file name (so no need to rebase or use path_output_).
      out_ << " /Yu" << target_->config_values().precompiled_header();
      RecursiveTargetConfigStringsToStream(target_, getter,
                                           flag_escape_options, out_);
    } else if (tool && tool->precompiled_header_type() == Tool::PCH_GCC) {
      // The targets to build the .gch files should omit the -include flag
      // below. To accomplish this, each substitution flag is overwritten in the
      // target rule and these values are repeated. The -include flag is omitted
      // in place of the required -x <header lang> flag for .gch targets.
      RecursiveTargetConfigStringsToStream(target_, getter,
                                           flag_escape_options, out_);

      // Compute the gch file (it will be language-specific).
      std::vector<OutputFile> outputs;
      GetPCHOutputFiles(target_, tool_type, &outputs);
      if (!outputs.empty()) {
        // Trim the .gch suffix for the -include flag.
        // e.g. for gch file foo/bar/target.precompiled.h.gch:
        //          -include foo/bar/target.precompiled.h
        std::string pch_file = outputs[0].value();
        pch_file.erase(pch_file.length() - 4);
        out_ << " -include " << pch_file;
      }
    } else {
      RecursiveTargetConfigStringsToStream(target_, getter,
                                           flag_escape_options, out_);
    }
  } else {
    RecursiveTargetConfigStringsToStream(target_, getter,
                                         flag_escape_options, out_);
  }
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WritePCHCommands(
    const SourceFileTypeSet& used_types,
    const OutputFile& input_dep,
    const OutputFile& order_only_dep,
    std::vector<OutputFile>* object_files,
    std::vector<OutputFile>* other_files) {
  if (!target_->config_values().has_precompiled_headers())
    return;

  const Tool* tool_c = target_->toolchain()->GetTool(Toolchain::TYPE_CC);
  if (tool_c &&
      tool_c->precompiled_header_type() != Tool::PCH_NONE &&
      used_types.Get(SOURCE_C)) {
    WritePCHCommand(SUBSTITUTION_CFLAGS_C,
                    Toolchain::TYPE_CC,
                    tool_c->precompiled_header_type(),
                    input_dep, order_only_dep, object_files, other_files);
  }
  const Tool* tool_cxx = target_->toolchain()->GetTool(Toolchain::TYPE_CXX);
  if (tool_cxx &&
      tool_cxx->precompiled_header_type() != Tool::PCH_NONE &&
      used_types.Get(SOURCE_CPP)) {
    WritePCHCommand(SUBSTITUTION_CFLAGS_CC,
                    Toolchain::TYPE_CXX,
                    tool_cxx->precompiled_header_type(),
                    input_dep, order_only_dep, object_files, other_files);
  }

  const Tool* tool_objc = target_->toolchain()->GetTool(Toolchain::TYPE_OBJC);
  if (tool_objc &&
      tool_objc->precompiled_header_type() == Tool::PCH_GCC &&
      used_types.Get(SOURCE_M)) {
    WritePCHCommand(SUBSTITUTION_CFLAGS_OBJC,
                    Toolchain::TYPE_OBJC,
                    tool_objc->precompiled_header_type(),
                    input_dep, order_only_dep, object_files, other_files);
  }

  const Tool* tool_objcxx =
      target_->toolchain()->GetTool(Toolchain::TYPE_OBJCXX);
  if (tool_objcxx &&
      tool_objcxx->precompiled_header_type() == Tool::PCH_GCC &&
      used_types.Get(SOURCE_MM)) {
    WritePCHCommand(SUBSTITUTION_CFLAGS_OBJCC,
                    Toolchain::TYPE_OBJCXX,
                    tool_objcxx->precompiled_header_type(),
                    input_dep, order_only_dep, object_files, other_files);
  }
}

void NinjaBinaryTargetWriter::WritePCHCommand(
    SubstitutionType flag_type,
    Toolchain::ToolType tool_type,
    Tool::PrecompiledHeaderType header_type,
    const OutputFile& input_dep,
    const OutputFile& order_only_dep,
    std::vector<OutputFile>* object_files,
    std::vector<OutputFile>* other_files) {
  switch (header_type) {
    case Tool::PCH_MSVC:
      WriteWindowsPCHCommand(flag_type, tool_type, input_dep, order_only_dep,
                             object_files);
      break;
    case Tool::PCH_GCC:
      WriteGCCPCHCommand(flag_type, tool_type, input_dep, order_only_dep,
                         other_files);
      break;
    case Tool::PCH_NONE:
      NOTREACHED() << "Cannot write a PCH command with no PCH header type";
      break;
  }
}

void NinjaBinaryTargetWriter::WriteGCCPCHCommand(
    SubstitutionType flag_type,
    Toolchain::ToolType tool_type,
    const OutputFile& input_dep,
    const OutputFile& order_only_dep,
    std::vector<OutputFile>* gch_files) {
  // Compute the pch output file (it will be language-specific).
  std::vector<OutputFile> outputs;
  GetPCHOutputFiles(target_, tool_type, &outputs);
  if (outputs.empty())
    return;

  gch_files->insert(gch_files->end(), outputs.begin(), outputs.end());

  std::vector<OutputFile> extra_deps;
  if (!input_dep.value().empty())
    extra_deps.push_back(input_dep);

  // Build line to compile the file.
  WriteCompilerBuildLine(target_->config_values().precompiled_source(),
                         extra_deps, order_only_dep, tool_type, outputs);

  // This build line needs a custom language-specific flags value. Rule-specific
  // variables are just indented underneath the rule line.
  out_ << "  " << kSubstitutionNinjaNames[flag_type] << " =";

  // Each substitution flag is overwritten in the target rule to replace the
  // implicitly generated -include flag with the -x <header lang> flag required
  // for .gch targets.
  EscapeOptions opts = GetFlagOptions();
  if (tool_type == Toolchain::TYPE_CC) {
    RecursiveTargetConfigStringsToStream(target_,
        &ConfigValues::cflags_c, opts, out_);
  } else if (tool_type == Toolchain::TYPE_CXX) {
    RecursiveTargetConfigStringsToStream(target_,
        &ConfigValues::cflags_cc, opts, out_);
  } else if (tool_type == Toolchain::TYPE_OBJC) {
    RecursiveTargetConfigStringsToStream(target_,
        &ConfigValues::cflags_objc, opts, out_);
  } else if (tool_type == Toolchain::TYPE_OBJCXX) {
    RecursiveTargetConfigStringsToStream(target_,
        &ConfigValues::cflags_objcc, opts, out_);
  }

  // Append the command to specify the language of the .gch file.
  out_ << " -x " << GetPCHLangForToolType(tool_type);

  // Write two blank lines to help separate the PCH build lines from the
  // regular source build lines.
  out_ << std::endl << std::endl;
}

void NinjaBinaryTargetWriter::WriteWindowsPCHCommand(
    SubstitutionType flag_type,
    Toolchain::ToolType tool_type,
    const OutputFile& input_dep,
    const OutputFile& order_only_dep,
    std::vector<OutputFile>* object_files) {
  // Compute the pch output file (it will be language-specific).
  std::vector<OutputFile> outputs;
  GetPCHOutputFiles(target_, tool_type, &outputs);
  if (outputs.empty())
    return;

  object_files->insert(object_files->end(), outputs.begin(), outputs.end());

  std::vector<OutputFile> extra_deps;
  if (!input_dep.value().empty())
    extra_deps.push_back(input_dep);

  // Build line to compile the file.
  WriteCompilerBuildLine(target_->config_values().precompiled_source(),
                         extra_deps, order_only_dep, tool_type, outputs);

  // This build line needs a custom language-specific flags value. Rule-specific
  // variables are just indented underneath the rule line.
  out_ << "  " << kSubstitutionNinjaNames[flag_type] << " =";

  // Append the command to generate the .pch file.
  // This adds the value to the existing flag instead of overwriting it.
  out_ << " ${" << kSubstitutionNinjaNames[flag_type] << "}";
  out_ << " /Yc" << target_->config_values().precompiled_header();

  // Write two blank lines to help separate the PCH build lines from the
  // regular source build lines.
  out_ << std::endl << std::endl;
}

void NinjaBinaryTargetWriter::WriteSources(
    const std::vector<OutputFile>& pch_deps,
    const OutputFile& input_dep,
    const OutputFile& order_only_dep,
    std::vector<OutputFile>* object_files,
    std::vector<SourceFile>* other_files) {
  object_files->reserve(object_files->size() + target_->sources().size());

  std::vector<OutputFile> tool_outputs;  // Prevent reallocation in loop.
  std::vector<OutputFile> deps;
  for (const auto& source : target_->sources()) {
    // Clear the vector but maintain the max capacity to prevent reallocations.
    deps.resize(0);
    Toolchain::ToolType tool_type = Toolchain::TYPE_NONE;
    if (!target_->GetOutputFilesForSource(source, &tool_type, &tool_outputs)) {
      if (GetSourceFileType(source) == SOURCE_DEF)
        other_files->push_back(source);
      continue;  // No output for this source.
    }

    if (!input_dep.value().empty())
      deps.push_back(input_dep);

    if (tool_type != Toolchain::TYPE_NONE) {
      // Only include PCH deps that correspond to the tool type, for instance,
      // do not specify target_name.precompile.cc.obj (a CXX PCH file) as a dep
      // for the output of a C tool type.
      //
      // This makes the assumption that pch_deps only contains pch output files
      // with the naming scheme specified in GetWindowsPCHObjectExtension or
      // GetGCCPCHOutputExtension.
      const Tool* tool = target_->toolchain()->GetTool(tool_type);
      if (tool->precompiled_header_type() != Tool::PCH_NONE) {
        for (const auto& dep : pch_deps) {
          const std::string& output_value = dep.value();
          size_t extension_offset = FindExtensionOffset(output_value);
          if (extension_offset == std::string::npos)
            continue;
          std::string output_extension;
          if (tool->precompiled_header_type() == Tool::PCH_MSVC) {
            output_extension = GetWindowsPCHObjectExtension(
                tool_type, output_value.substr(extension_offset - 1));
          } else if (tool->precompiled_header_type() == Tool::PCH_GCC) {
            output_extension = GetGCCPCHOutputExtension(tool_type);
          }
          if (output_value.compare(output_value.size() -
              output_extension.size(), output_extension.size(),
              output_extension) == 0) {
            deps.push_back(dep);
          }
        }
      }
      WriteCompilerBuildLine(source, deps, order_only_dep, tool_type,
                             tool_outputs);
    }

    // It's theoretically possible for a compiler to produce more than one
    // output, but we'll only link to the first output.
    object_files->push_back(tool_outputs[0]);
  }
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteCompilerBuildLine(
    const SourceFile& source,
    const std::vector<OutputFile>& extra_deps,
    const OutputFile& order_only_dep,
    Toolchain::ToolType tool_type,
    const std::vector<OutputFile>& outputs) {
  out_ << "build";
  path_output_.WriteFiles(out_, outputs);

  out_ << ": " << rule_prefix_ << Toolchain::ToolTypeToName(tool_type);
  out_ << " ";
  path_output_.WriteFile(out_, source);

  if (!extra_deps.empty()) {
    out_ << " |";
    for (const OutputFile& dep : extra_deps) {
      out_ << " ";
      path_output_.WriteFile(out_, dep);
    }
  }

  if (!order_only_dep.value().empty()) {
    out_ << " || ";
    path_output_.WriteFile(out_, order_only_dep);
  }
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteLinkerStuff(
    const std::vector<OutputFile>& object_files,
    const std::vector<SourceFile>& other_files) {
  std::vector<OutputFile> output_files;
  SubstitutionWriter::ApplyListToLinkerAsOutputFile(
      target_, tool_, tool_->outputs(), &output_files);

  out_ << "build";
  path_output_.WriteFiles(out_, output_files);

  out_ << ": " << rule_prefix_
       << Toolchain::ToolTypeToName(
              target_->toolchain()->GetToolTypeForTargetFinalOutput(target_));

  UniqueVector<OutputFile> extra_object_files;
  UniqueVector<const Target*> linkable_deps;
  UniqueVector<const Target*> non_linkable_deps;
  GetDeps(&extra_object_files, &linkable_deps, &non_linkable_deps);

  // Object files.
  path_output_.WriteFiles(out_, object_files);
  path_output_.WriteFiles(out_, extra_object_files);

  // Dependencies.
  std::vector<OutputFile> implicit_deps;
  std::vector<OutputFile> solibs;
  for (const Target* cur : linkable_deps) {
    // All linkable deps should have a link output file.
    DCHECK(!cur->link_output_file().value().empty())
        << "No link output file for "
        << target_->label().GetUserVisibleName(false);

    if (cur->dependency_output_file().value() !=
        cur->link_output_file().value()) {
      // This is a shared library with separate link and deps files. Save for
      // later.
      implicit_deps.push_back(cur->dependency_output_file());
      solibs.push_back(cur->link_output_file());
    } else {
      // Normal case, just link to this target.
      out_ << " ";
      path_output_.WriteFile(out_, cur->link_output_file());
    }
  }

  const SourceFile* optional_def_file = nullptr;
  if (!other_files.empty()) {
    for (const SourceFile& src_file : other_files) {
      if (GetSourceFileType(src_file) == SOURCE_DEF) {
        optional_def_file = &src_file;
        implicit_deps.push_back(
            OutputFile(settings_->build_settings(), src_file));
        break;  // Only one def file is allowed.
      }
    }
  }

  // Libraries specified by paths.
  const OrderedSet<LibFile>& libs = target_->all_libs();
  for (size_t i = 0; i < libs.size(); i++) {
    if (libs[i].is_source_file()) {
      implicit_deps.push_back(
          OutputFile(settings_->build_settings(), libs[i].source_file()));
    }
  }

  // Append implicit dependencies collected above.
  if (!implicit_deps.empty()) {
    out_ << " |";
    path_output_.WriteFiles(out_, implicit_deps);
  }

  // Append data dependencies as order-only dependencies.
  //
  // This will include data dependencies and input dependencies (like when
  // this target depends on an action). Having the data dependencies in this
  // list ensures that the data is available at runtime when the user builds
  // this target.
  //
  // The action dependencies are not strictly necessary in this case. They
  // should also have been collected via the input deps stamp that each source
  // file has for an order-only dependency, and since this target depends on
  // the sources, there is already an implicit order-only dependency. However,
  // it's extra work to separate these out and there's no disadvantage to
  // listing them again.
  WriteOrderOnlyDependencies(non_linkable_deps);

  // End of the link "build" line.
  out_ << std::endl;

  // The remaining things go in the inner scope of the link line.
  if (target_->output_type() == Target::EXECUTABLE ||
      target_->output_type() == Target::SHARED_LIBRARY ||
      target_->output_type() == Target::LOADABLE_MODULE) {
    WriteLinkerFlags(optional_def_file);
    WriteLibs();
  } else if (target_->output_type() == Target::STATIC_LIBRARY) {
    out_ << "  arflags =";
    RecursiveTargetConfigStringsToStream(target_, &ConfigValues::arflags,
                                         GetFlagOptions(), out_);
    out_ << std::endl;
  }
  WriteOutputSubstitutions();
  WriteSolibs(solibs);
}

void NinjaBinaryTargetWriter::WriteLinkerFlags(
    const SourceFile* optional_def_file) {
  out_ << "  ldflags =";

  // First the ldflags from the target and its config.
  RecursiveTargetConfigStringsToStream(target_, &ConfigValues::ldflags,
                                       GetFlagOptions(), out_);

  // Followed by library search paths that have been recursively pushed
  // through the dependency tree.
  const OrderedSet<SourceDir> all_lib_dirs = target_->all_lib_dirs();
  if (!all_lib_dirs.empty()) {
    // Since we're passing these on the command line to the linker and not
    // to Ninja, we need to do shell escaping.
    PathOutput lib_path_output(path_output_.current_dir(),
                               settings_->build_settings()->root_path_utf8(),
                               ESCAPE_NINJA_COMMAND);
    for (size_t i = 0; i < all_lib_dirs.size(); i++) {
      out_ << " " << tool_->lib_dir_switch();
      lib_path_output.WriteDir(out_, all_lib_dirs[i],
                               PathOutput::DIR_NO_LAST_SLASH);
    }
  }

  if (optional_def_file) {
    out_ << " /DEF:";
    path_output_.WriteFile(out_, *optional_def_file);
  }

  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteLibs() {
  out_ << "  libs =";

  // Libraries that have been recursively pushed through the dependency tree.
  EscapeOptions lib_escape_opts;
  lib_escape_opts.mode = ESCAPE_NINJA_COMMAND;
  const OrderedSet<LibFile> all_libs = target_->all_libs();
  const std::string framework_ending(".framework");
  for (size_t i = 0; i < all_libs.size(); i++) {
    const LibFile& lib_file = all_libs[i];
    const std::string& lib_value = lib_file.value();
    if (lib_file.is_source_file()) {
      out_ << " ";
      path_output_.WriteFile(out_, lib_file.source_file());
    } else if (base::EndsWith(lib_value, framework_ending,
                              base::CompareCase::INSENSITIVE_ASCII)) {
      // Special-case libraries ending in ".framework" to support Mac: Add the
      // -framework switch and don't add the extension to the output.
      out_ << " -framework ";
      EscapeStringToStream(
          out_, lib_value.substr(0, lib_value.size() - framework_ending.size()),
          lib_escape_opts);
    } else {
      out_ << " " << tool_->lib_switch();
      EscapeStringToStream(out_, lib_value, lib_escape_opts);
    }
  }
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteOutputSubstitutions() {
  out_ << "  output_extension = "
       << SubstitutionWriter::GetLinkerSubstitution(
              target_, tool_, SUBSTITUTION_OUTPUT_EXTENSION);
  out_ << std::endl;
  out_ << "  output_dir = "
       << SubstitutionWriter::GetLinkerSubstitution(
              target_, tool_, SUBSTITUTION_OUTPUT_DIR);
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteSolibs(
    const std::vector<OutputFile>& solibs) {
  if (solibs.empty())
    return;

  out_ << "  solibs =";
  path_output_.WriteFiles(out_, solibs);
  out_ << std::endl;
}

void NinjaBinaryTargetWriter::WriteSourceSetStamp(
    const std::vector<OutputFile>& object_files) {
  // The stamp rule for source sets is generally not used, since targets that
  // depend on this will reference the object files directly. However, writing
  // this rule allows the user to type the name of the target and get a build
  // which can be convenient for development.
  UniqueVector<OutputFile> extra_object_files;
  UniqueVector<const Target*> linkable_deps;
  UniqueVector<const Target*> non_linkable_deps;
  GetDeps(&extra_object_files, &linkable_deps, &non_linkable_deps);

  // The classifier should never put extra object files in a source set:
  // any source sets that we depend on should appear in our non-linkable
  // deps instead.
  DCHECK(extra_object_files.empty());

  std::vector<OutputFile> order_only_deps;
  for (auto* dep : non_linkable_deps)
    order_only_deps.push_back(dep->dependency_output_file());

  WriteStampForTarget(object_files, order_only_deps);
}

void NinjaBinaryTargetWriter::GetDeps(
    UniqueVector<OutputFile>* extra_object_files,
    UniqueVector<const Target*>* linkable_deps,
    UniqueVector<const Target*>* non_linkable_deps) const {
  // Normal public/private deps.
  for (const auto& pair : target_->GetDeps(Target::DEPS_LINKED)) {
    ClassifyDependency(pair.ptr, extra_object_files,
                       linkable_deps, non_linkable_deps);
  }

  // Inherited libraries.
  for (auto* inherited_target : target_->inherited_libraries().GetOrdered()) {
    ClassifyDependency(inherited_target, extra_object_files,
                       linkable_deps, non_linkable_deps);
  }

  // Data deps.
  for (const auto& data_dep_pair : target_->data_deps())
    non_linkable_deps->push_back(data_dep_pair.ptr);
}

void NinjaBinaryTargetWriter::ClassifyDependency(
    const Target* dep,
    UniqueVector<OutputFile>* extra_object_files,
    UniqueVector<const Target*>* linkable_deps,
    UniqueVector<const Target*>* non_linkable_deps) const {
  // Only the following types of outputs have libraries linked into them:
  //  EXECUTABLE
  //  SHARED_LIBRARY
  //  _complete_ STATIC_LIBRARY
  //
  // Child deps of intermediate static libraries get pushed up the
  // dependency tree until one of these is reached, and source sets
  // don't link at all.
  bool can_link_libs = target_->IsFinal();

  if (dep->output_type() == Target::SOURCE_SET ||
      // If a complete static library depends on an incomplete static library,
      // manually link in the object files of the dependent library as if it
      // were a source set. This avoids problems with braindead tools such as
      // ar which don't properly link dependent static libraries.
      (target_->complete_static_lib() &&
       dep->output_type() == Target::STATIC_LIBRARY &&
       !dep->complete_static_lib())) {
    // Source sets have their object files linked into final targets
    // (shared libraries, executables, loadable modules, and complete static
    // libraries). Intermediate static libraries and other source sets
    // just forward the dependency, otherwise the files in the source
    // set can easily get linked more than once which will cause
    // multiple definition errors.
    if (can_link_libs)
      AddSourceSetObjectFiles(dep, extra_object_files);

    // Add the source set itself as a non-linkable dependency on the current
    // target. This will make sure that anything the source set's stamp file
    // depends on (like data deps) are also built before the current target
    // can be complete. Otherwise, these will be skipped since this target
    // will depend only on the source set's object files.
    non_linkable_deps->push_back(dep);
  } else if (target_->complete_static_lib() && dep->IsFinal()) {
    non_linkable_deps->push_back(dep);
  } else if (can_link_libs && dep->IsLinkable()) {
    linkable_deps->push_back(dep);
  } else {
    non_linkable_deps->push_back(dep);
  }
}

void NinjaBinaryTargetWriter::WriteOrderOnlyDependencies(
    const UniqueVector<const Target*>& non_linkable_deps) {
  if (!non_linkable_deps.empty()) {
    out_ << " ||";

    // Non-linkable targets.
    for (auto* non_linkable_dep : non_linkable_deps) {
      out_ << " ";
      path_output_.WriteFile(out_, non_linkable_dep->dependency_output_file());
    }
  }
}

OutputFile NinjaBinaryTargetWriter::GetWindowsPCHFile(
    Toolchain::ToolType tool_type) const {
  // Use "obj/{dir}/{target_name}_{lang}.pch" which ends up
  // looking like "obj/chrome/browser/browser_cc.pch"
  OutputFile ret = GetBuildDirForTargetAsOutputFile(target_, BuildDirType::OBJ);
  ret.value().append(target_->label().name());
  ret.value().push_back('_');
  ret.value().append(GetPCHLangSuffixForToolType(tool_type));
  ret.value().append(".pch");

  return ret;
}

bool NinjaBinaryTargetWriter::CheckForDuplicateObjectFiles(
    const std::vector<OutputFile>& files) const {
  base::hash_set<std::string> set;
  for (const auto& file : files) {
    if (!set.insert(file.value()).second) {
      Err err(
          target_->defined_from(),
          "Duplicate object file",
          "The target " + target_->label().GetUserVisibleName(false) +
          "\ngenerates two object files with the same name:\n  " +
          file.value() + "\n"
          "\n"
          "It could be you accidentally have a file listed twice in the\n"
          "sources. Or, depending on how your toolchain maps sources to\n"
          "object files, two source files with the same name in different\n"
          "directories could map to the same object file.\n"
          "\n"
          "In the latter case, either rename one of the files or move one of\n"
          "the sources to a separate source_set to avoid them both being in\n"
          "the same target.");
      g_scheduler->FailWithError(err);
      return false;
    }
  }
  return true;
}
