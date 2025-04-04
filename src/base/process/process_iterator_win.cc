// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_iterator.h"

#include "base/strings/string_util.h"

namespace base {

ProcessIterator::ProcessIterator(const ProcessFilter* filter)
    : snapshot_(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)),
      filter_(filter) {}

ProcessIterator::~ProcessIterator() {
  CloseHandle(snapshot_);
}

bool ProcessIterator::CheckForNextProcess() {
  InitProcessEntry(&entry_);

  if (!started_iteration_) {
    started_iteration_ = true;
    return !!Process32First(snapshot_, &entry_);
  }

  return !!Process32Next(snapshot_, &entry_);
}

void ProcessIterator::InitProcessEntry(ProcessEntry* entry) {
  memset(entry, 0, sizeof(*entry));
  entry->dwSize = sizeof(*entry);
}

bool NamedProcessIterator::IncludeEntry() {
  // Case insensitive.
  return !_wcsicmp(executable_name_.c_str(), entry().exe_file()) &&
         ProcessIterator::IncludeEntry();
}

}  // namespace base
