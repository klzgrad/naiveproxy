/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_REDACTION_DROP_EMPTY_FTRACE_EVENTS_H_
#define SRC_TRACE_REDACTION_DROP_EMPTY_FTRACE_EVENTS_H_

#include <string>

#include "perfetto/base/status.h"
#include "src/trace_redaction/trace_redaction_framework.h"

namespace perfetto::trace_redaction {

// Looks at every ftrace event and if the event is empty (only contains a pid
// and timestamp value), it will be dropped (not copied to the new packet).
// After removing ftrace events, it is possible for a ftrace event bundle to be
// empty. It is easier to drop ftrace event bundles in its own primitive that
// must run after this primitive.
class DropEmptyFtraceEvents final : public TransformPrimitive {
 public:
  base::Status Transform(const Context& context,
                         std::string* packet) const override;

 private:
  void OnPackageList(const Context& context,
                     protozero::ConstBytes bytes,
                     protos::pbzero::PackagesList* message) const;
};

}  // namespace perfetto::trace_redaction

#endif  // SRC_TRACE_REDACTION_DROP_EMPTY_FTRACE_EVENTS_H_
