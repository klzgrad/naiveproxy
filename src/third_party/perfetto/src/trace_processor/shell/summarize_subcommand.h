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

#ifndef SRC_TRACE_PROCESSOR_SHELL_SUMMARIZE_SUBCOMMAND_H_
#define SRC_TRACE_PROCESSOR_SHELL_SUMMARIZE_SUBCOMMAND_H_

#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "src/trace_processor/shell/subcommand.h"

namespace perfetto::trace_processor::shell {

class SummarizeSubcommand : public Subcommand {
 public:
  const char* name() const override;
  const char* description() const override;
  const char* usage_args() const override;
  const char* detailed_help() const override;
  std::vector<FlagSpec> GetFlags() override;
  base::Status Run(const SubcommandContext& ctx) override;

 private:
  std::string metrics_v2_;
  std::string metadata_query_;
  std::string output_format_;
  std::string post_query_path_;
  std::string perf_file_;
  bool interactive_ = false;
};

}  // namespace perfetto::trace_processor::shell

#endif  // SRC_TRACE_PROCESSOR_SHELL_SUMMARIZE_SUBCOMMAND_H_
