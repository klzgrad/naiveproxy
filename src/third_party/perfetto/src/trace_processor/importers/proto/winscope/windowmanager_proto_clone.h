/*
 * Copyright (C) 2025 The Android Open Source Project
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
#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PROTO_CLONE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PROTO_CLONE_H_

#include <vector>

#include "protos/perfetto/trace/android/server/windowmanagerservice.pbzero.h"
#include "protos/perfetto/trace/android/windowmanager.pbzero.h"

namespace perfetto::trace_processor::winscope::windowmanager_proto_clone {

std::vector<uint8_t> CloneEntryProtoPruningChildren(
    const protos::pbzero::WindowManagerTraceEntry::Decoder&);
std::vector<uint8_t> CloneRootWindowContainerProtoPruningChildren(
    const protos::pbzero::RootWindowContainerProto::Decoder&);
std::vector<uint8_t> CloneWindowContainerChildProtoPruningChildren(
    const protos::pbzero::WindowContainerChildProto::Decoder&);

}  // namespace perfetto::trace_processor::winscope::windowmanager_proto_clone

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINDOWMANAGER_PROTO_CLONE_H_
