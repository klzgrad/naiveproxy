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

#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"  // IWYU pragma: keep
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "src/proto_utils/txt_to_pb.h"
#include "src/protozero/filtering/message_filter.h"
#include "src/protozero/filtering/message_filter_config.h"
#include "src/protozero/filtering/string_filter.h"

namespace perfetto {
namespace string_filter_tool {
namespace {

const char kUsage[] =
    R"USAGE(Usage: string_filter_tool -r <rules_textproto> [-t <semantic_type>] <string>
       string_filter_tool -r <rules_textproto> -i <trace_in> -o <trace_out>

Mode 1 (string mode):
  Applies the Perfetto string filtering algorithm to <string> using the rules
  defined in <rules_textproto> and prints the result to stdout.

Mode 2 (trace mode):
  Applies bytecode-based proto field filtering and string filtering to a
  binary-encoded trace file. The config must contain bytecode fields
  (bytecode/bytecode_v2) in addition to string_filter_chain rules.

Arguments:
  -r --rules:          Path to a TraceConfig textproto file. The
                       trace_filter.string_filter_chain field provides string
                       filter rules. For trace mode, bytecode/bytecode_v2 fields
                       are also required.
  -t --semantic_type:  Semantic type to use (integer, default: 0 = UNSPECIFIED).
                       Only used in string mode.
  -i --trace_in:       Path to a binary-encoded proto trace file to filter.
  -o --trace_out:      Path for the filtered trace output.
  <string>             The string to filter (positional argument, string mode).

The rules textproto file should contain a TraceConfig with trace_filter rules,
for example:

  trace_filter {
    string_filter_chain {
      rules {
        policy: SFP_MATCH_REDACT_GROUPS
        regex_pattern: "foo(bar)baz"
      }
    }
  }

For trace mode, include bytecode fields:

  trace_filter {
    bytecode_v2: "\000..."
    string_filter_chain {
      rules { ... }
    }
  }

Output (string mode):
  Prints the (possibly filtered) string to stdout, followed by a newline.
  Exit code 0 if the string was modified, 1 if it was not.

Output (trace mode):
  Writes the filtered trace to --trace_out. Exit code 0 on success.
)USAGE";

using TraceFilter = protos::gen::TraceConfig::TraceFilter;
using StringFilterRule = TraceFilter::StringFilterRule;

std::optional<protozero::StringFilter::Policy> ConvertPolicy(
    TraceFilter::StringFilterPolicy policy) {
  switch (policy) {
    case TraceFilter::SFP_UNSPECIFIED:
      return std::nullopt;
    case TraceFilter::SFP_MATCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kMatchRedactGroups;
    case TraceFilter::SFP_ATRACE_MATCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kAtraceMatchRedactGroups;
    case TraceFilter::SFP_MATCH_BREAK:
      return protozero::StringFilter::Policy::kMatchBreak;
    case TraceFilter::SFP_ATRACE_MATCH_BREAK:
      return protozero::StringFilter::Policy::kAtraceMatchBreak;
    case TraceFilter::SFP_ATRACE_REPEATED_SEARCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kAtraceRepeatedSearchRedactGroups;
  }
  return std::nullopt;
}

protozero::StringFilter::SemanticTypeMask ConvertSemanticTypes(
    const StringFilterRule& rule) {
  protozero::StringFilter::SemanticTypeMask mask;
  if (rule.semantic_type().empty()) {
    mask.Set(0);
    return mask;
  }
  for (const auto& type : rule.semantic_type()) {
    auto semantic_type = static_cast<uint32_t>(type);
    if (semantic_type < protozero::StringFilter::SemanticTypeMask::kLimit) {
      mask.Set(semantic_type);
    }
  }
  return mask;
}

int LoadStringFilterRules(const TraceFilter& trace_filter,
                          protozero::StringFilter& filter) {
  for (const auto& rule : trace_filter.string_filter_chain().rules()) {
    auto opt_policy = ConvertPolicy(rule.policy());
    if (!opt_policy) {
      PERFETTO_ELOG("Unknown string filter policy %d", rule.policy());
      return 1;
    }
    filter.AddRule(*opt_policy, rule.regex_pattern(),
                   rule.atrace_payload_starts_with(), rule.name(),
                   ConvertSemanticTypes(rule));
  }

  // Also load v54 chain if present.
  for (const auto& rule : trace_filter.string_filter_chain_v54().rules()) {
    auto opt_policy = ConvertPolicy(rule.policy());
    if (!opt_policy) {
      PERFETTO_ELOG("Unknown string filter policy %d", rule.policy());
      return 1;
    }
    filter.AddRule(*opt_policy, rule.regex_pattern(),
                   rule.atrace_payload_starts_with(), rule.name(),
                   ConvertSemanticTypes(rule));
  }
  return 0;
}

int Main(int argc, char** argv) {
  static const option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"rules", required_argument, nullptr, 'r'},
      {"semantic_type", required_argument, nullptr, 't'},
      {"trace_in", required_argument, nullptr, 'i'},
      {"trace_out", required_argument, nullptr, 'o'},
      {nullptr, 0, nullptr, 0},
  };

  std::string rules_path;
  std::string trace_in;
  std::string trace_out;
  uint32_t semantic_type = 0;

  for (;;) {
    int option = getopt_long(argc, argv, "hr:t:i:o:", long_options, nullptr);
    if (option == -1)
      break;

    if (option == 'h') {
      fprintf(stdout, "%s", kUsage);
      return 0;
    }

    if (option == 'r') {
      rules_path = optarg;
      continue;
    }

    if (option == 't') {
      auto parsed = base::CStringToUInt32(optarg);
      if (!parsed.has_value()) {
        PERFETTO_ELOG("Invalid semantic type: %s\n", optarg);
        return 1;
      }
      semantic_type = *parsed;
      continue;
    }

    if (option == 'i') {
      trace_in = optarg;
      continue;
    }

    if (option == 'o') {
      trace_out = optarg;
      continue;
    }

    PERFETTO_ELOG("%s", kUsage);
    return 1;
  }

  if (rules_path.empty()) {
    PERFETTO_ELOG("%s", kUsage);
    return 1;
  }

  // Read and parse the rules textproto.
  std::string rules_data;
  if (!base::ReadFile(rules_path, &rules_data)) {
    PERFETTO_ELOG("Could not read rules file: %s", rules_path.c_str());
    return 1;
  }

  auto res = TraceConfigTxtToPb(rules_data, rules_path);
  if (!res.ok()) {
    PERFETTO_ELOG("%s\n", res.status().c_message());
    return 1;
  }

  std::vector<uint8_t>& config_bytes = res.value();
  protos::gen::TraceConfig config;
  config.ParseFromArray(config_bytes.data(), config_bytes.size());

  const auto& trace_filter = config.trace_filter();

  // Trace mode: apply bytecode + string filtering to a trace file.
  if (!trace_in.empty()) {
    if (trace_out.empty()) {
      PERFETTO_ELOG("--trace_out (-o) is required when using --trace_in\n");
      return 1;
    }

    protozero::MessageFilter msg_filter;
    auto status = protozero::LoadMessageFilterConfig(trace_filter, &msg_filter);
    if (!status.ok()) {
      PERFETTO_ELOG("%s", status.c_message());
      return 1;
    }

    // Read the input trace.
    std::string trace_data;
    if (!base::ReadFile(trace_in, &trace_data)) {
      PERFETTO_ELOG("Could not read trace file: %s", trace_in.c_str());
      return 1;
    }

    // Apply the filter.
    auto filtered =
        msg_filter.FilterMessage(trace_data.data(), trace_data.size());
    if (filtered.error) {
      PERFETTO_ELOG("Filtering failed");
      return 1;
    }

    // Write the filtered trace.
    auto fd = base::OpenFile(trace_out, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (!fd) {
      PERFETTO_ELOG("Could not open output file: %s", trace_out.c_str());
      return 1;
    }
    base::WriteAll(*fd, filtered.data.get(), filtered.size);
    PERFETTO_LOG("Written filtered trace (%zu bytes) to %s", filtered.size,
                 trace_out.c_str());
    return 0;
  }

  // String mode: apply string filtering to a single string.
  if (optind >= argc) {
    PERFETTO_ELOG("%s", kUsage);
    return 1;
  }

  // The remaining positional argument is the string to filter.
  // Unescape C-style escape sequences (\n, \t, \\) so users can pass
  // strings containing newlines from the shell.
  std::string input_str;
  for (const char* p = argv[optind]; *p; ++p) {
    if (*p == '\\' && *(p + 1)) {
      switch (*(p + 1)) {
        case 'n':
          input_str += '\n';
          ++p;
          continue;
        case 't':
          input_str += '\t';
          ++p;
          continue;
        case '\\':
          input_str += '\\';
          ++p;
          continue;
        default:
          break;
      }
    }
    input_str += *p;
  }

  protozero::StringFilter filter;
  int err = LoadStringFilterRules(trace_filter, filter);
  if (err)
    return err;

  // Apply the filter. MaybeFilter modifies the string in-place.
  bool was_modified =
      filter.MaybeFilter(input_str.data(), input_str.size(), semantic_type);

  // Print the result.
  fprintf(stdout, "%s\n", input_str.c_str());
  return was_modified ? 0 : 1;
}

}  // namespace
}  // namespace string_filter_tool
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::string_filter_tool::Main(argc, argv);
}
