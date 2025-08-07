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

#include "perfetto/ext/base/subprocess.h"

#include <tuple>

// This file contains only the common bits (ctors / dtors / move operators).
// The rest lives in subprocess_posix.cc and subprocess_windows.cc.

namespace perfetto {
namespace base {

Subprocess::Args::Args(Args&&) noexcept = default;
Subprocess::Args& Subprocess::Args::operator=(Args&&) = default;

Subprocess::Subprocess(std::initializer_list<std::string> a)
    : args(a), s_(new MovableState()) {}

Subprocess::Subprocess(Subprocess&& other) noexcept {
  static_assert(sizeof(Subprocess) ==
                    sizeof(std::tuple<std::unique_ptr<MovableState>, Args>),
                "base::Subprocess' move ctor needs updating");
  s_ = std::move(other.s_);
  args = std::move(other.args);

  // Reset the state of the moved-from object.
  other.s_.reset(new MovableState());
  other.~Subprocess();
  new (&other) Subprocess();
}

Subprocess& Subprocess::operator=(Subprocess&& other) {
  this->~Subprocess();
  new (this) Subprocess(std::move(other));
  return *this;
}

Subprocess::~Subprocess() {
  if (s_->status == kRunning)
    KillAndWaitForTermination();
}

bool Subprocess::Call(int timeout_ms) {
  PERFETTO_CHECK(s_->status == kNotStarted);
  Start();

  if (!Wait(timeout_ms)) {
    s_->timed_out = true;
    KillAndWaitForTermination(kTimeoutSignal);
  }
  PERFETTO_DCHECK(s_->status != kRunning);
  return s_->status == kTerminated && s_->returncode == 0;
}

std::string Subprocess::Args::GetCmdString() const {
  std::string str;
  for (size_t i = 0; i < exec_cmd.size(); i++) {
    str += i > 0 ? " \"" : "";
    str += exec_cmd[i];
    str += i > 0 ? "\"" : "";
  }
  return str;
}

}  // namespace base
}  // namespace perfetto
