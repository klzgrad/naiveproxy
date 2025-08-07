/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "perfetto/ext/trace_processor/demangle.h"

#include <string.h>
#include <string>

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_LLVM_DEMANGLE)
#include "llvm/Demangle/Demangle.h"
#elif !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <cxxabi.h>
#endif

namespace perfetto {
namespace trace_processor {
namespace demangle {

// Implementation depends on platform and build config. If llvm demangling
// sources are available, use them. That is the most portable and handles more
// than just Itanium mangling (e.g. Rust's _R scheme). Otherwise use the c++
// standard library demangling if it implements the appropriate ABI. This
// excludes Windows builds, where we therefore never demangle.
// TODO(rsavitski): consider reimplementing llvm::demangle inline as it's
// wrapping in std::strings a set of per-scheme demangling functions that
// operate on C strings. Right now we're introducing yet another layer that
// undoes that conversion.
std::unique_ptr<char, base::FreeDeleter> Demangle(const char* mangled_name) {
#if PERFETTO_BUILDFLAG(PERFETTO_LLVM_DEMANGLE)
  std::string input(mangled_name);
  std::string demangled = llvm::demangle(input);
  if (demangled == input)
    return nullptr;  // demangling unsuccessful

  std::unique_ptr<char, base::FreeDeleter> output(
      static_cast<char*>(malloc(demangled.size() + 1)));
  if (!output)
    return nullptr;
  memcpy(output.get(), demangled.c_str(), demangled.size() + 1);
  return output;

#elif !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  int ignored = 0;
  return std::unique_ptr<char, base::FreeDeleter>(
      abi::__cxa_demangle(mangled_name, nullptr, nullptr, &ignored));

#else
  return nullptr;
#endif
}

}  // namespace demangle
}  // namespace trace_processor
}  // namespace perfetto
