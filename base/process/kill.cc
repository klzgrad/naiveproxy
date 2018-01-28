// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/kill.h"

#include "base/process/process_iterator.h"

namespace base {

bool KillProcesses(const FilePath::StringType& executable_name,
                   int exit_code,
                   const ProcessFilter* filter) {
  bool result = true;
  NamedProcessIterator iter(executable_name, filter);
  while (const ProcessEntry* entry = iter.NextProcessEntry()) {
    Process process = Process::Open(entry->pid());
    // Sometimes process open fails. This would cause a DCHECK in
    // process.Terminate(). Maybe the process has killed itself between the
    // time the process list was enumerated and the time we try to open the
    // process?
    if (!process.IsValid()) {
      result = false;
      continue;
    }
    result &= process.Terminate(exit_code, true);
  }
  return result;
}

}  // namespace base
