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

#ifndef SRC_TRACE_PROCESSOR_SHELL_SUBCOMMAND_H_
#define SRC_TRACE_PROCESSOR_SHELL_SUBCOMMAND_H_

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "perfetto/base/status.h"

namespace perfetto::trace_processor {
class TraceProcessorShell_PlatformInterface;
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::shell {

// Forward declaration.
struct GlobalOptions;

// Describes a single flag for a subcommand.
struct FlagSpec {
  const char* long_name;  // e.g. "query-file"
  char short_name;        // e.g. 'f', or '\0' for no short form
  bool has_arg;           // true if the flag takes an argument
  const char* arg_name;   // e.g. "FILE", or nullptr if no argument
  const char* help;       // one-line description
  std::function<void(const char*)> handler;
};

// Helper to create a FlagSpec that writes a string value.
inline FlagSpec StringFlag(const char* name,
                           char ch,
                           const char* arg_name,
                           const char* help,
                           std::string* target) {
  return {name,     ch,   true,
          arg_name, help, [target](const char* v) { *target = v; }};
}

// Helper to create a FlagSpec for a boolean flag (no argument).
inline FlagSpec BoolFlag(const char* name,
                         char ch,
                         const char* help,
                         bool* target) {
  return {name,    ch,   false,
          nullptr, help, [target](const char*) { *target = true; }};
}

// Context passed to subcommands, providing access to shared resources.
struct SubcommandContext {
  TraceProcessorShell_PlatformInterface* platform = nullptr;
  GlobalOptions* global = nullptr;
  std::vector<std::string> positional_args;
};

// Base class for all subcommands (query, export, serve, etc.).
class Subcommand {
 public:
  virtual ~Subcommand();

  Subcommand(const Subcommand&) = delete;
  Subcommand& operator=(const Subcommand&) = delete;
  Subcommand(Subcommand&&) = delete;
  Subcommand& operator=(Subcommand&&) = delete;

  // The name of the subcommand as it appears on the command line
  // (e.g. "query", "export").
  virtual const char* name() const = 0;

  // A short one-line description shown in help output.
  virtual const char* description() const = 0;

  // Positional args shown in usage line, e.g. "<trace_file> [SQL]".
  virtual const char* usage_args() const = 0;

  // Multi-line detailed help shown in per-subcommand help.
  virtual const char* detailed_help() const = 0;

  // Returns the flags this subcommand accepts.
  virtual std::vector<FlagSpec> GetFlags() = 0;

  // Runs the subcommand. |ctx| provides access to shared resources.
  virtual base::Status Run(const SubcommandContext& ctx) = 0;

 protected:
  Subcommand() = default;
};

// Result of FindSubcommandInArgs(). If |subcommand| is non-null, a subcommand
// was found. |argv_index| is the index of the subcommand name in the original
// argv.
struct FindSubcommandResult {
  Subcommand* subcommand = nullptr;
  int argv_index = -1;
};

// Scans argv for the first positional argument (skipping flags) that matches
// a registered subcommand name. Flags starting with '-' are skipped; flags
// listed in |flags_with_arg| (e.g. "--dev-flag", "-q") also skip their
// following argument. The first non-flag positional that doesn't match a
// subcommand stops the search.
FindSubcommandResult FindSubcommandInArgs(
    int argc,
    char** argv,
    const std::vector<Subcommand*>& subcommands,
    const std::unordered_set<std::string>& flags_with_arg);

}  // namespace perfetto::trace_processor::shell

#endif  // SRC_TRACE_PROCESSOR_SHELL_SUBCOMMAND_H_
