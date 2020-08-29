// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <string>

#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/trace_event_stub.h"

namespace base {
namespace trace_event {

ConvertableToTraceFormat::~ConvertableToTraceFormat() = default;

void TracedValue::AppendAsTraceFormat(std::string* out) const {}

MemoryDumpProvider::~MemoryDumpProvider() = default;

MemoryDumpManager g_stub_memory_dump_manager;
MemoryDumpManager* MemoryDumpManager::GetInstance() {
  return &g_stub_memory_dump_manager;
}

template size_t EstimateMemoryUsage(const std::string&);
template size_t EstimateMemoryUsage(const string16&);

}  // namespace trace_event
}  // namespace base
