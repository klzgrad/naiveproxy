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

#ifndef SRC_TRACE_PROCESSOR_SHELL_TRACECONV_COMPAT_H_
#define SRC_TRACE_PROCESSOR_SHELL_TRACECONV_COMPAT_H_

#include <string>
#include <vector>

namespace perfetto::trace_processor::shell {

// Returns true if the binary was invoked under the legacy "traceconv" name.
// The traceconv prebuilt wrapper downloads trace_processor_shell but caches and
// execs it as "traceconv-<sha>" (or "traceconv.exe" on Windows), so a basename
// starting with "traceconv" means we should present the traceconv-compatible
// CLI.
bool InvokedAsTraceconv(const char* argv0);

// Maps a traceconv-style command line onto the new subcommand structure,
// inserting the matching subcommand word before the traceconv MODE positional
// and, for a few modes, rewriting the MODE token itself:
//   - "symbolize"/"deobfuscate"/"decompress_packets" -> insert "util"
//   - "binary"                  -> "util text_to_binary" (renamed)
//   - "java_heap_profile"       -> "convert profile --java-heap"
//   - "bundle" (already a subcommand name) -> no change
//   - any other MODE            -> insert "convert" (which validates it)
// Returns the rewritten argv as a vector of strings, or an empty vector if no
// rewrite is needed (no positional MODE found, or MODE is "bundle"). Callers
// should only invoke this when InvokedAsTraceconv() is true.
std::vector<std::string> RewriteTraceconvArgs(int argc, char** argv);

}  // namespace perfetto::trace_processor::shell

#endif  // SRC_TRACE_PROCESSOR_SHELL_TRACECONV_COMPAT_H_
