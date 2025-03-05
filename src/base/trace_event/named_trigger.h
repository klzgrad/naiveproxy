// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_NAMED_TRIGGER_H_
#define BASE_TRACE_EVENT_NAMED_TRIGGER_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/base_export.h"
#include "base/trace_event/base_tracing.h"

namespace base::trace_event {

inline constexpr char kStartupTracingTriggerName[] = "startup";

// Notifies that a manual trigger event has occurred. Returns true if the
// trigger caused a scenario to either begin recording or finalize the trace
// depending on the config, or false if the trigger had no effect. If the
// trigger specified isn't active in the config, this will do nothing.
BASE_EXPORT bool EmitNamedTrigger(const std::string& trigger_name,
                                  std::optional<int32_t> value = std::nullopt);

class NamedTriggerManager {
 public:
  virtual bool DoEmitNamedTrigger(const std::string& trigger_name,
                                  std::optional<int32_t> value) = 0;

 protected:
  // Sets the instance returns by GetInstance() globally to |manager|.
  BASE_EXPORT static void SetInstance(NamedTriggerManager* manager);
};

// Returns a flow id that connects to a background tracing trigger.
BASE_EXPORT uint64_t TriggerFlowId(const std::string_view& name,
                                   std::optional<int32_t> value = std::nullopt);

// Returns a perfetto flow that connects to a background tracing trigger.
BASE_EXPORT perfetto::Flow TriggerFlow(
    const std::string_view& name,
    std::optional<int32_t> value = std::nullopt);

}  // namespace base::trace_event

#endif  // BASE_TRACE_EVENT_NAMED_TRIGGER_H_
