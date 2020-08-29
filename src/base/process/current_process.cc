// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/current_process.h"

namespace base {

namespace {

struct ProcessName {
  CurrentProcessType type;
  const char* name;
};

constexpr ProcessName kProcessNames[] = {
    {CurrentProcessType{}, ""},
};

}  // namespace

// static
CurrentProcess& CurrentProcess::GetInstance() {
  static base::NoDestructor<CurrentProcess> instance;
  return *instance;
}

void CurrentProcess::SetProcessType(CurrentProcessType process_type) {
  std::string process_name;
  for (size_t i = 0; i < std::size(kProcessNames); ++i) {
    if (process_type == kProcessNames[i].type) {
      process_name = kProcessNames[i].name;
    }
  }
  CurrentProcess::GetInstance().SetProcessNameAndType(process_name,
                                                      process_type);
}

void CurrentProcess::SetProcessNameAndType(const std::string& process_name,
                                           CurrentProcessType process_type) {
  {
    AutoLock lock(lock_);
    process_name_ = process_name;
    process_type_.store(static_cast<CurrentProcessType>(process_type),
                        std::memory_order_relaxed);
  }
#if BUILDFLAG(ENABLE_BASE_TRACING)
  trace_event::TraceLog::GetInstance()->set_process_name(process_name);
#endif
}

}  // namespace base
