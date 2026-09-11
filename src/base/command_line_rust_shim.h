// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_COMMAND_LINE_RUST_SHIM_H_
#define BASE_COMMAND_LINE_RUST_SHIM_H_

#include <memory>

#include "base/command_line.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace base::rust {

// Functions to retrieve/create CommandLine instances.
std::unique_ptr<base::CommandLine> NewForProgram(::rust::Str program);

// Methods on base::CommandLine wrapped as free functions.
bool HasSwitch(const base::CommandLine& cmd, ::rust::Str switch_string);
::rust::String GetSwitchValueASCII(const base::CommandLine& cmd,
                                   ::rust::Str switch_string);
void AppendSwitch(base::CommandLine& cmd, ::rust::Str switch_string);
void AppendSwitchASCII(base::CommandLine& cmd,
                       ::rust::Str switch_string,
                       ::rust::Str value);
void RemoveSwitch(base::CommandLine& cmd, ::rust::Str switch_string);
void AppendArg(base::CommandLine& cmd, ::rust::Str value);
::rust::Vec<::rust::String> GetArgs(const base::CommandLine& cmd);
::rust::String GetProgram(const base::CommandLine& cmd);
bool GetAppOutput(const base::CommandLine& cmd, ::rust::String& output);

}  // namespace base::rust

#endif  // BASE_COMMAND_LINE_RUST_SHIM_H_
