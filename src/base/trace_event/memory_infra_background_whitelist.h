// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_MEMORY_INFRA_BACKGROUND_WHITELIST_H_
#define BASE_TRACE_EVENT_MEMORY_INFRA_BACKGROUND_WHITELIST_H_

// This file contains the whitelists for background mode to limit the tracing
// overhead and remove sensitive information from traces.

#include <string>

#include "base/base_export.h"

namespace base {
namespace trace_event {

// Checks if the given |mdp_name| is in the whitelist.
bool BASE_EXPORT IsMemoryDumpProviderWhitelisted(const char* mdp_name);

// Checks if the given |name| matches any of the whitelisted patterns.
bool BASE_EXPORT IsMemoryAllocatorDumpNameWhitelisted(const std::string& name);

// The whitelist is replaced with the given list for tests. The last element of
// the list must be nullptr.
void BASE_EXPORT SetDumpProviderWhitelistForTesting(const char* const* list);
void BASE_EXPORT
SetAllocatorDumpNameWhitelistForTesting(const char* const* list);

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_MEMORY_INFRA_BACKGROUND_WHITELIST_H_
