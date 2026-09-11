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

#ifndef SRC_TRACE_PROCESSOR_SHELL_CONVERT_HELPERS_H_
#define SRC_TRACE_PROCESSOR_SHELL_CONVERT_HELPERS_H_

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

#include "perfetto/base/status.h"

namespace perfetto::trace_processor::shell {

// Opens the input stream for a conversion. An empty |path| (or "-") means read
// from stdin; otherwise the file at |path| is opened into |owned_file|. On
// success |*out_stream| points at either |owned_file| or std::cin. Returns an
// error if the file cannot be opened or if stdin is a TTY (which would block
// waiting for binary input typed by hand).
base::Status OpenConversionInput(const std::string& path,
                                 std::ifstream* owned_file,
                                 std::istream** out_stream);

// Opens the output stream for a conversion. An empty |path| (or "-") means
// write to stdout; otherwise the file at |path| is opened into |owned_file|. On
// success |*out_stream| points at either |owned_file| or std::cout.
// If |binary_output| is true, stdout is refused when connected to a TTY, as
// dumping binary data would corrupt the terminal.
base::Status OpenConversionOutput(const std::string& path,
                                  bool binary_output,
                                  std::ofstream* owned_file,
                                  std::ostream** out_stream);

// Converts a text-format trace proto read from |input| into a binary trace
// written to |output|.
base::Status TextToTrace(std::istream* input, std::ostream* output);

}  // namespace perfetto::trace_processor::shell

#endif  // SRC_TRACE_PROCESSOR_SHELL_CONVERT_HELPERS_H_
