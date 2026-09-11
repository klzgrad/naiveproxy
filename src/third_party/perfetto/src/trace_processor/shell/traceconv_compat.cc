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

#include "src/trace_processor/shell/traceconv_compat.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace perfetto::trace_processor::shell {

bool InvokedAsTraceconv(const char* argv0) {
  std::string_view name(argv0);
  size_t slash = name.find_last_of("/\\");
  if (slash != std::string_view::npos)
    name = name.substr(slash + 1);
  constexpr std::string_view kExe = ".exe";
  if (name.size() >= kExe.size() &&
      name.substr(name.size() - kExe.size()) == kExe) {
    name = name.substr(0, name.size() - kExe.size());
  }
  // Match the bare tool name or the prebuilt cache form "traceconv-<sha>", but
  // not unrelated tools that merely share the prefix (e.g. "traceconverter").
  return name == "traceconv" || name.rfind("traceconv-", 0) == 0;
}

std::vector<std::string> RewriteTraceconvArgs(int argc, char** argv) {
  // traceconv options that consume a following argument; used to find the MODE
  // positional regardless of where options appear on the command line.
  static constexpr const char* kFlagsWithArg[] = {
      "-t",           "--truncate",     "--pid",
      "--timestamps", "--symbol-paths", "--proguard-map",
      "--output-dir"};

  int mode_idx = -1;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (!arg.empty() && arg[0] == '-') {
      for (const char* f : kFlagsWithArg) {
        if (arg == f) {
          ++i;  // Skip the flag's argument.
          break;
        }
      }
      continue;
    }
    mode_idx = i;
    break;
  }
  if (mode_idx == -1)
    return {};

  std::string_view mode(argv[mode_idx]);

  // Map the traceconv MODE onto the new subcommand structure. Most modes just
  // get a subcommand word inserted before them, but a few are renamed (and one
  // gains a flag) because the underlying CLI moved:
  //   - util utilities: symbolize, deobfuscate, decompress_packets
  //   - "binary" was renamed to "util text_to_binary"
  //   - "java_heap_profile" became "convert profile --java-heap"
  //   - "bundle" is already a subcommand name, so it is left untouched
  //   - anything else is a convert format ('convert' validates it)
  const char* subcommand = nullptr;
  const char* replacement_mode = nullptr;  // null => keep the original token
  const char* extra_arg = nullptr;         // inserted after the mode, if set
  if (mode == "symbolize" || mode == "deobfuscate" ||
      mode == "decompress_packets") {
    subcommand = "util";
  } else if (mode == "binary") {
    subcommand = "util";
    replacement_mode = "text_to_binary";
  } else if (mode == "java_heap_profile") {
    subcommand = "convert";
    replacement_mode = "profile";
    extra_arg = "--java-heap";
  } else if (mode != "bundle") {
    subcommand = "convert";
  }
  if (!subcommand)
    return {};

  std::vector<std::string> out;
  out.reserve(static_cast<size_t>(argc) + 2);
  for (int i = 0; i < mode_idx; ++i)
    out.emplace_back(argv[i]);
  out.emplace_back(subcommand);
  out.emplace_back(replacement_mode ? replacement_mode : argv[mode_idx]);
  if (extra_arg)
    out.emplace_back(extra_arg);
  for (int i = mode_idx + 1; i < argc; ++i)
    out.emplace_back(argv[i]);
  return out;
}

}  // namespace perfetto::trace_processor::shell
