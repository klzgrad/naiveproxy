// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/compile_commands_writer.h"

#include <sstream>

#include "base/json/string_escape.h"
#include "base/strings/stringprintf.h"
#include "tools/gn/builder.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/escape.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/ninja_target_command_util.h"
#include "tools/gn/path_output.h"
#include "tools/gn/substitution_writer.h"

// Structure of JSON output file
// [
//   {
//      "directory": "The build directory."
//      "file": "The main source file processed by this compilation step.
//               Must be absolute or relative to the above build directory."
//      "command": "The compile command executed."
//   }
//   ...
// ]

namespace {

#if defined(OS_WIN)
const char kPrettyPrintLineEnding[] = "\r\n";
#else
const char kPrettyPrintLineEnding[] = "\n";
#endif

struct CompileFlags {
  std::string includes;
  std::string defines;
  std::string cflags;
  std::string cflags_c;
  std::string cflags_cc;
  std::string cflags_objc;
  std::string cflags_objcc;
};

void SetupCompileFlags(const Target* target,
                       PathOutput& path_output,
                       EscapeOptions opts,
                       CompileFlags& flags) {
  bool has_precompiled_headers =
      target->config_values().has_precompiled_headers();

  std::ostringstream defines_out;
  RecursiveTargetConfigToStream<std::string>(
      target, &ConfigValues::defines,
      DefineWriter(ESCAPE_NINJA_PREFORMATTED_COMMAND, true), defines_out);
  base::EscapeJSONString(defines_out.str(), false, &flags.defines);

  std::ostringstream includes_out;
  RecursiveTargetConfigToStream<SourceDir>(target, &ConfigValues::include_dirs,
                                           IncludeWriter(path_output),
                                           includes_out);
  base::EscapeJSONString(includes_out.str(), false, &flags.includes);

  std::ostringstream cflags_out;
  WriteOneFlag(target, SUBSTITUTION_CFLAGS, false, Toolchain::TYPE_NONE,
               &ConfigValues::cflags, opts, path_output, cflags_out,
               /*write_substitution=*/false);
  base::EscapeJSONString(cflags_out.str(), false, &flags.cflags);

  std::ostringstream cflags_c_out;
  WriteOneFlag(target, SUBSTITUTION_CFLAGS_C, has_precompiled_headers,
               Toolchain::TYPE_CC, &ConfigValues::cflags_c, opts, path_output,
               cflags_c_out, /*write_substitution=*/false);
  base::EscapeJSONString(cflags_c_out.str(), false, &flags.cflags_c);

  std::ostringstream cflags_cc_out;
  WriteOneFlag(target, SUBSTITUTION_CFLAGS_CC, has_precompiled_headers,
               Toolchain::TYPE_CXX, &ConfigValues::cflags_cc, opts, path_output,
               cflags_cc_out, /*write_substitution=*/false);
  base::EscapeJSONString(cflags_cc_out.str(), false, &flags.cflags_cc);

  std::ostringstream cflags_objc_out;
  WriteOneFlag(target, SUBSTITUTION_CFLAGS_OBJC, has_precompiled_headers,
               Toolchain::TYPE_OBJC, &ConfigValues::cflags_objc, opts,
               path_output, cflags_objc_out,
               /*write_substitution=*/false);
  base::EscapeJSONString(cflags_objc_out.str(), false, &flags.cflags_objc);

  std::ostringstream cflags_objcc_out;
  WriteOneFlag(target, SUBSTITUTION_CFLAGS_OBJCC, has_precompiled_headers,
               Toolchain::TYPE_OBJCXX, &ConfigValues::cflags_objcc, opts,
               path_output, cflags_objcc_out, /*write_substitution=*/false);
  base::EscapeJSONString(cflags_objcc_out.str(), false, &flags.cflags_objcc);
}

void WriteFile(const SourceFile& source,
               PathOutput& path_output,
               std::string* compile_commands) {
  std::ostringstream rel_source_path;
  path_output.WriteFile(rel_source_path, source);
  compile_commands->append("    \"file\": \"");
  compile_commands->append(rel_source_path.str());
}

void WriteDirectory(std::string build_dir, std::string* compile_commands) {
  compile_commands->append("\",");
  compile_commands->append(kPrettyPrintLineEnding);
  compile_commands->append("    \"directory\": \"");
  compile_commands->append(build_dir);
  compile_commands->append("\",");
}

void WriteCommand(const Target* target,
                  const SourceFile& source,
                  const CompileFlags& flags,
                  std::vector<OutputFile>& tool_outputs,
                  PathOutput& path_output,
                  SourceFileType source_type,
                  Toolchain::ToolType tool_type,
                  EscapeOptions opts,
                  std::string* compile_commands) {
  EscapeOptions no_quoting(opts);
  no_quoting.inhibit_quoting = true;
  const Tool* tool = target->toolchain()->GetTool(tool_type);
  std::ostringstream command_out;

  for (const auto& range : tool->command().ranges()) {
    // TODO: this is emitting a bonus space prior to each substitution.
    switch (range.type) {
      case SUBSTITUTION_LITERAL:
        EscapeStringToStream(command_out, range.literal, no_quoting);
        break;
      case SUBSTITUTION_OUTPUT:
        path_output.WriteFiles(command_out, tool_outputs);
        break;
      case SUBSTITUTION_DEFINES:
        command_out << flags.defines;
        break;
      case SUBSTITUTION_INCLUDE_DIRS:
        command_out << flags.includes;
        break;
      case SUBSTITUTION_CFLAGS:
        command_out << flags.cflags;
        break;
      case SUBSTITUTION_CFLAGS_C:
        if (source_type == SOURCE_C)
          command_out << flags.cflags_c;
        break;
      case SUBSTITUTION_CFLAGS_CC:
        if (source_type == SOURCE_CPP)
          command_out << flags.cflags_cc;
        break;
      case SUBSTITUTION_CFLAGS_OBJC:
        if (source_type == SOURCE_M)
          command_out << flags.cflags_objc;
        break;
      case SUBSTITUTION_CFLAGS_OBJCC:
        if (source_type == SOURCE_MM)
          command_out << flags.cflags_objcc;
        break;
      case SUBSTITUTION_LABEL:
      case SUBSTITUTION_LABEL_NAME:
      case SUBSTITUTION_ROOT_GEN_DIR:
      case SUBSTITUTION_ROOT_OUT_DIR:
      case SUBSTITUTION_TARGET_GEN_DIR:
      case SUBSTITUTION_TARGET_OUT_DIR:
      case SUBSTITUTION_TARGET_OUTPUT_NAME:
      case SUBSTITUTION_SOURCE:
      case SUBSTITUTION_SOURCE_NAME_PART:
      case SUBSTITUTION_SOURCE_FILE_PART:
      case SUBSTITUTION_SOURCE_DIR:
      case SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR:
      case SUBSTITUTION_SOURCE_GEN_DIR:
      case SUBSTITUTION_SOURCE_OUT_DIR:
      case SUBSTITUTION_SOURCE_TARGET_RELATIVE:
        EscapeStringToStream(command_out,
                             SubstitutionWriter::GetCompilerSubstitution(
                                 target, source, range.type),
                             opts);
        break;

      // Other flags shouldn't be relevant to compiling C/C++/ObjC/ObjC++
      // source files.
      default:
        NOTREACHED() << "Unsupported substitution for this type of target : "
                     << kSubstitutionNames[range.type];
        continue;
    }
  }
  compile_commands->append(kPrettyPrintLineEnding);
  compile_commands->append("    \"command\": \"");
  compile_commands->append(command_out.str());
}

}  // namespace

void CompileCommandsWriter::RenderJSON(const BuildSettings* build_settings,
                                       std::vector<const Target*>& all_targets,
                                       std::string* compile_commands) {
  // TODO: Determine out an appropriate size to reserve.
  compile_commands->reserve(all_targets.size() * 100);
  compile_commands->append("[");
  compile_commands->append(kPrettyPrintLineEnding);
  bool first = true;
  auto build_dir = build_settings->GetFullPath(build_settings->build_dir())
                       .StripTrailingSeparators();
  std::vector<OutputFile> tool_outputs;  // Prevent reallocation in loop.

  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA_PREFORMATTED_COMMAND;

  for (const auto* target : all_targets) {
    if (!target->IsBinary())
      continue;

    // Precompute values that are the same for all sources in a target to avoid
    // computing for every source.

    PathOutput path_output(
        target->settings()->build_settings()->build_dir(),
        target->settings()->build_settings()->root_path_utf8(),
        ESCAPE_NINJA_COMMAND);

    CompileFlags flags;
    SetupCompileFlags(target, path_output, opts, flags);

    for (const auto& source : target->sources()) {
      // If this source is not a C/C++/ObjC/ObjC++ source (not header) file,
      // continue as it does not belong in the compilation database.
      SourceFileType source_type = GetSourceFileType(source);
      if (source_type != SOURCE_CPP && source_type != SOURCE_C &&
          source_type != SOURCE_M && source_type != SOURCE_MM)
        continue;

      Toolchain::ToolType tool_type = Toolchain::TYPE_NONE;
      if (!target->GetOutputFilesForSource(source, &tool_type, &tool_outputs))
        continue;

      if (!first) {
        compile_commands->append(",");
        compile_commands->append(kPrettyPrintLineEnding);
      }
      first = false;
      compile_commands->append("  {");
      compile_commands->append(kPrettyPrintLineEnding);

      WriteFile(source, path_output, compile_commands);
      WriteDirectory(base::StringPrintf("%" PRIsFP, build_dir.value().c_str()),
                     compile_commands);
      WriteCommand(target, source, flags, tool_outputs, path_output,
                   source_type, tool_type, opts, compile_commands);
      compile_commands->append("\"");
      compile_commands->append(kPrettyPrintLineEnding);
      compile_commands->append("  }");
    }
  }

  compile_commands->append(kPrettyPrintLineEnding);
  compile_commands->append("]");
  compile_commands->append(kPrettyPrintLineEnding);
}

bool CompileCommandsWriter::RunAndWriteFiles(
    const BuildSettings* build_settings,
    const Builder& builder,
    const std::string& file_name,
    bool quiet,
    Err* err) {
  SourceFile output_file = build_settings->build_dir().ResolveRelativeFile(
      Value(nullptr, file_name), err);
  if (output_file.is_null())
    return false;

  base::FilePath output_path = build_settings->GetFullPath(output_file);

  std::vector<const Target*> all_targets = builder.GetAllResolvedTargets();

  std::string json;
  RenderJSON(build_settings, all_targets, &json);
  if (!WriteFileIfChanged(output_path, json, err))
    return false;
  return true;
}
