/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/shell/util_subcommand.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/shell/convert_helpers.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/trace_processor/util/json_value.h"
#include "src/trace_processor/util/tar_writer.h"
#include "src/traceconv/deobfuscate_profile.h"
#include "src/traceconv/symbolize_profile.h"
#include "src/traceconv/trace_unpack.h"

namespace perfetto::trace_processor::shell {
namespace {

// Checks that every files[].path in the manifest names one of the archive
// members. An entry whose path matches nothing is silently ignored by the
// importer (its overrides just never apply), so this is the packer's only
// chance to catch the mistake. Anything else is left to the importer, which
// validates the manifest fully when the archive is opened.
base::Status CheckManifestPaths(const std::string& manifest,
                                const std::set<std::string>& members) {
  ASSIGN_OR_RETURN(json::Dom root, json::Parse(manifest));
  if (!root.IsObject() || !root.HasMember("perfetto_manifest")) {
    return base::ErrStatus(
        "merge: the manifest must be a JSON object with a top-level "
        "perfetto_manifest key.");
  }
  const json::Dom& meta = root["perfetto_manifest"];
  if (!meta.IsObject() || !meta.HasMember("files") ||
      !meta["files"].IsArray()) {
    return base::OkStatus();
  }
  for (const json::Dom& file : meta["files"]) {
    if (!file.IsObject() || !file.HasMember("path") ||
        !file["path"].IsString()) {
      continue;
    }
    std::string path = file["path"].AsString();
    if (!members.count(path)) {
      return base::ErrStatus(
          "merge: the manifest names file '%s' but no input file has that "
          "name. Manifest paths must match the input files' base names.",
          path.c_str());
    }
  }
  return base::OkStatus();
}

// Dry-runs the written archive through a tokenize-only Trace Processor pass
// and reports whether any events would be dropped when it is opened. This is
// the same check the UI's merge dialog runs.
base::Status ValidateMergedArchive(const std::string& path, bool strict) {
  Config config;
  config.parsing_mode = ParsingMode::kTokenizeOnly;
  std::unique_ptr<TraceProcessor> tp = TraceProcessor::CreateInstance(config);
  RETURN_IF_ERROR(ReadTrace(tp.get(), path.c_str()));
  Iterator it = tp->ExecuteQuery(R"(
    SELECT COALESCE(SUM(value), 0)
    FROM stats
    WHERE name IN (
      'clock_sync_unrelatable_clock_domains',
      'clock_sync_failure_no_path',
      'trace_sorter_negative_timestamp_dropped'
    )
  )");
  if (!it.Next()) {
    RETURN_IF_ERROR(it.Status());
    return base::ErrStatus("merge: validation query returned no rows.");
  }
  int64_t dropped = it.Get(0).long_value;
  if (dropped == 0) {
    printf("Validation: all traces line up on the shared timeline.\n");
    return base::OkStatus();
  }
  if (strict) {
    return base::ErrStatus(
        "merge: %" PRId64
        " events would be dropped when this archive is opened: they cannot "
        "be placed on the shared timeline. Adjust the clocks in the "
        "manifest.",
        dropped);
  }
  fprintf(stderr,
          "Warning: %" PRId64
          " events would be dropped when this archive is opened: they cannot "
          "be placed on the shared timeline. Adjust the clocks in the "
          "manifest.\n",
          dropped);
  return base::OkStatus();
}

}  // namespace

const char* UtilSubcommand::name() const {
  return "util";
}

const char* UtilSubcommand::description() const {
  return "Low-level trace utilities (symbolize, deobfuscate, etc.).";
}

const char* UtilSubcommand::usage_args() const {
  return "<merge|symbolize|deobfuscate|decompress_packets|text_to_binary> "
         "[args]";
}

const char* UtilSubcommand::detailed_help() const {
  return R"(Low-level trace utilities.

Utilities:
  merge                Pack several traces into one archive that opens as a
                       single merged trace.
  symbolize            Symbolize addresses in a profile, emitting symbol packets.
  deobfuscate          Emit deobfuscation packets from a trace.
  decompress_packets   Decompress compressed trace packets.
  text_to_binary       Convert a text-format trace proto to binary.

merge usage:
  util merge -o merged.tar [--manifest manifest.json] trace1 trace2 ...

  Packs the input traces (and, optionally, a perfetto_manifest JSON file
  configuring the merge) into a TAR archive. Opening the archive, in the UI
  or in trace_processor, imports all the traces as one merged trace on a
  shared timeline. Archive members are named after the inputs' base names,
  which is what the manifest's files[].path entries must match.

  Unless --no-validate is given, the tool runs sanity checks on the written
  archive and warns if it would not merge cleanly (--strict turns warnings
  into errors).

  Full guide: https://perfetto.dev/docs/analysis/merging-traces
  Manifest format: https://perfetto.dev/docs/reference/perfetto-manifest

Other utilities read from [input] (default stdin) and write to [output]
(default stdout):
  util <utility> [input] [output]

symbolize/deobfuscate are lower-level than 'bundle', which is the recommended
one-shot way to produce a self-contained, symbolized trace.)";
}

std::vector<FlagSpec> UtilSubcommand::GetFlags() {
  return {
      BoolFlag("verbose", '\0', "Print more detailed output.", &verbose_),
      StringFlag("output", 'o', "FILE", "merge: the archive to write.",
                 &merge_output_),
      StringFlag("manifest", '\0', "FILE",
                 "merge: perfetto_manifest JSON file to include.",
                 &merge_manifest_),
      BoolFlag("no-validate", '\0',
               "merge: skip the dry-run check of the written archive.",
               &merge_no_validate_),
      BoolFlag("strict", '\0',
               "merge: fail if the dry-run reports dropped events.",
               &merge_strict_),
  };
}

base::Status UtilSubcommand::Run(const SubcommandContext& ctx) {
  if (ctx.positional_args.empty()) {
    return base::ErrStatus(
        "util: a utility ('symbolize' or 'deobfuscate') must be specified.");
  }
  const std::string& util = ctx.positional_args[0];
  if (util == "merge") {
    // merge accepts any number of input traces, so skip the positional count
    // check (the other utilities take at most [input] [output]).
    return RunMerge(ctx);
  }
  RETURN_IF_ERROR(RejectExtraPositionals(ctx, "util", 3));
  const std::string input_path =
      ctx.positional_args.size() > 1 ? ctx.positional_args[1] : "";
  const std::string output_path =
      ctx.positional_args.size() > 2 ? ctx.positional_args[2] : "";

  if (util != "symbolize" && util != "deobfuscate" &&
      util != "decompress_packets" && util != "text_to_binary") {
    return base::ErrStatus(
        "util: unknown utility '%s' (expected 'merge', 'symbolize', "
        "'deobfuscate', 'decompress_packets' or 'text_to_binary').",
        util.c_str());
  }

  std::ifstream input_file;
  std::istream* input = nullptr;
  RETURN_IF_ERROR(OpenConversionInput(input_path, &input_file, &input));

  std::ofstream output_file;
  std::ostream* output = nullptr;
  // All the utilities emit binary protobuf output.
  RETURN_IF_ERROR(OpenConversionOutput(output_path, /*binary_output=*/true,
                                       &output_file, &output));

  if (util == "symbolize") {
    RETURN_IF_ERROR(trace_to_text::SymbolizeProfile(input, output, verbose_));
  } else if (util == "deobfuscate") {
    RETURN_IF_ERROR(trace_to_text::DeobfuscateProfile(input, output));
  } else if (util == "decompress_packets") {
    RETURN_IF_ERROR(trace_to_text::UnpackCompressedPackets(input, output));
  } else {  // text_to_binary
    RETURN_IF_ERROR(TextToTrace(input, output));
  }
  return base::OkStatus();
}

base::Status UtilSubcommand::RunMerge(const SubcommandContext& ctx) {
  std::vector<std::string> inputs(ctx.positional_args.begin() + 1,
                                  ctx.positional_args.end());
  if (inputs.empty()) {
    return base::ErrStatus("merge: at least one input trace file is required.");
  }
  if (merge_output_.empty()) {
    return base::ErrStatus("merge: an output path is required (-o FILE).");
  }

  // Members are named after the inputs' base names; the manifest keys its
  // entries on these names.
  std::vector<std::string> members;
  std::set<std::string> member_set;
  for (const std::string& path : inputs) {
    std::string member = base::Basename(path);
    if (!member_set.insert(member).second) {
      return base::ErrStatus(
          "merge: two input files share the name '%s'; archive members must "
          "be unique. Rename one of the files.",
          member.c_str());
    }
    members.push_back(std::move(member));
  }

  std::string manifest;
  if (!merge_manifest_.empty()) {
    if (!base::ReadFile(merge_manifest_, &manifest)) {
      return base::ErrStatus("merge: cannot read manifest file '%s'.",
                             merge_manifest_.c_str());
    }
    RETURN_IF_ERROR(CheckManifestPaths(manifest, member_set));
  }

  {
    util::TarWriter tar(merge_output_);
    if (!manifest.empty()) {
      RETURN_IF_ERROR(tar.AddFile("perfetto_manifest.json", manifest));
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
      RETURN_IF_ERROR(tar.AddFileFromPath(members[i], inputs[i]));
    }
  }
  printf("Wrote %s (%zu trace%s%s).\n", merge_output_.c_str(), inputs.size(),
         inputs.size() == 1 ? "" : "s",
         manifest.empty() ? "" : " plus manifest");
  fflush(stdout);

  if (merge_no_validate_) {
    return base::OkStatus();
  }
  return ValidateMergedArchive(merge_output_, merge_strict_);
}

}  // namespace perfetto::trace_processor::shell
