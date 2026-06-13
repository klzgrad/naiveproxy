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

#include "src/trace_processor/shell/interactive_subcommand.h"

#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/interactive.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/subcommand.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_SIGNAL_H() 1
#include <signal.h>
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

namespace perfetto::trace_processor::shell {

const char* InteractiveSubcommand::name() const {
  return "interactive";
}

const char* InteractiveSubcommand::description() const {
  return "Interactive SQL shell.";
}

const char* InteractiveSubcommand::usage_args() const {
  return "<trace_file>";
}

const char* InteractiveSubcommand::detailed_help() const {
  return R"(Open a REPL for running SQL queries interactively against a trace file.
This is the default when no subcommand is specified.)";
}

std::vector<FlagSpec> InteractiveSubcommand::GetFlags() {
  return {
      BoolFlag("wide", 'W', "Double column width for output.", &wide_),
  };
}

base::Status InteractiveSubcommand::Run(const SubcommandContext& ctx) {
  if (ctx.positional_args.empty()) {
    return base::ErrStatus("interactive: trace file is required");
  }
  std::string trace_file = ctx.positional_args[0];

  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  RETURN_IF_ERROR(LoadTraceFile(tp.get(), ctx.platform, trace_file).status());

#if PERFETTO_HAS_SIGNAL_H()
  static TraceProcessor* g_tp_for_signal_handler = tp.get();
  signal(SIGINT, [](int) { g_tp_for_signal_handler->InterruptQuery(); });
#endif

  RETURN_IF_ERROR(StartInteractiveShell(
      tp.get(),
      InteractiveOptions{
          wide_ ? 40u : 20u, MetricV1OutputFormat::kNone, {}, {}, nullptr}));

  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
