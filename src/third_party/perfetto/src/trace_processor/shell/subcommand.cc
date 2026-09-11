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

#include "src/trace_processor/shell/subcommand.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor::shell {

Subcommand::~Subcommand() = default;

base::Status RejectExtraPositionals(const SubcommandContext& ctx,
                                    const char* subcommand,
                                    size_t max_positionals,
                                    const char* hint) {
  if (ctx.positional_args.size() <= max_positionals)
    return base::OkStatus();

  std::string extra;
  for (size_t i = max_positionals; i < ctx.positional_args.size(); ++i) {
    if (!extra.empty())
      extra += " ";
    extra += "'" + ctx.positional_args[i] + "'";
  }
  base::Status status = base::ErrStatus(
      "%s: expected at most %zu positional argument(s), got %zu: %s. Extra "
      "arguments are rejected to catch mistakes, e.g. passing each path "
      "intended for a value-taking flag as a separate argument; use a "
      "comma-separated list for those instead.",
      subcommand, max_positionals, ctx.positional_args.size(), extra.c_str());
  if (hint) {
    status = base::ErrStatus("%s %s", status.c_message(), hint);
  }
  return status;
}

FindSubcommandResult FindSubcommandInArgs(
    int argc,
    char** argv,
    const std::vector<Subcommand*>& subcommands,
    const std::unordered_set<std::string>& flags_with_arg) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);

    // Skip flags.
    if (arg[0] == '-') {
      if (flags_with_arg.count(std::string(arg)))
        ++i;  // Skip the flag's argument.
      continue;
    }

    // Positional argument: check if it matches a subcommand.
    for (auto* sc : subcommands) {
      if (arg == sc->name()) {
        return {sc, i};
      }
    }

    // Unknown positional argument (likely a trace file) — stop searching.
    break;
  }
  return {};
}

}  // namespace perfetto::trace_processor::shell
