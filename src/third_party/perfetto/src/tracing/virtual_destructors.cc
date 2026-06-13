/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/tracing/internal/tracing_tls.h"
#include "perfetto/tracing/tracing.h"
#include "perfetto/tracing/tracing_backend.h"

// This translation unit contains the definitions for the destructor of pure
// virtual interfaces for the src/public:public target. The alternative would be
// introducing a one-liner .cc file for each pure virtual interface, which is
// overkill. This is for compliance with -Wweak-vtables.

namespace perfetto {
namespace internal {

TracingTLS::~TracingTLS() {
  // Avoid entering trace points while the thread is being torn down.
  // This is the problem: when a thread exits, the at-thread-exit destroys the
  // TracingTLS. As part of that the various TraceWriter for the active data
  // sources are destroyd. A TraceWriter dtor will issue a PostTask on the IPC
  // thread to issue a final flush and unregister its ID with the service.
  // The PostTask, in chromium, might have a trace event that will try to
  // re-enter the tracing system.
  // We fix this by resetting the TLS key to the TracingTLS object that is
  // being destroyed in the platform impl (platform_posix.cc,
  // platform_windows.cc, chromium's platform.cc). We carefully rely on the fact
  // that all the tracing path that will be invoked during thread exit will
  // early out if |is_in_trace_point| == true and will not depend on the other
  // TLS state that has been destroyed.
  is_in_trace_point = true;
}

}  // namespace internal

TracingProducerBackend::~TracingProducerBackend() = default;
TracingConsumerBackend::~TracingConsumerBackend() = default;
TracingBackend::~TracingBackend() = default;

}  // namespace perfetto
