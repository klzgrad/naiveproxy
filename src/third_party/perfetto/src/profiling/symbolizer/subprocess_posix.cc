/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/profiling/symbolizer/subprocess.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace profiling {

Subprocess::Subprocess(const std::string& file, std::vector<std::string> args)
    : input_pipe_(base::Pipe::Create(base::Pipe::kBothBlock)),
      output_pipe_(base::Pipe::Create(base::Pipe::kBothBlock)) {
  std::vector<char*> c_str_args;
  c_str_args.reserve(args.size());
  for (std::string& arg : args)
    c_str_args.push_back(&(arg[0]));
  c_str_args.push_back(nullptr);

  if ((pid_ = fork()) == 0) {
    // Child
    PERFETTO_CHECK(dup2(*input_pipe_.rd, STDIN_FILENO) != -1);
    PERFETTO_CHECK(dup2(*output_pipe_.wr, STDOUT_FILENO) != -1);
    input_pipe_.wr.reset();
    output_pipe_.rd.reset();
    if (execvp(file.c_str(), c_str_args.data()) == -1)
      PERFETTO_FATAL("Failed to exec %s", file.c_str());
  }
  PERFETTO_CHECK(pid_ != -1);
  input_pipe_.rd.reset();
  output_pipe_.wr.reset();
}

Subprocess::~Subprocess() {
  if (pid_ != -1) {
    kill(pid_, SIGKILL);
    int wstatus;
    PERFETTO_EINTR(waitpid(pid_, &wstatus, 0));
  }
}

int64_t Subprocess::Write(const char* buffer, size_t size) {
  if (!input_pipe_.wr) {
    return -1;
  }
  return PERFETTO_EINTR(write(input_pipe_.wr.get(), buffer, size));
}

int64_t Subprocess::Read(char* buffer, size_t size) {
  if (!output_pipe_.rd) {
    return -1;
  }
  return PERFETTO_EINTR(read(output_pipe_.rd.get(), buffer, size));
}

}  // namespace profiling
}  // namespace perfetto

#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
