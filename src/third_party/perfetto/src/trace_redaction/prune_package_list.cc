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

#include "src/trace_redaction/prune_package_list.h"

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"

#include "protos/perfetto/trace/android/packages_list.pbzero.h"

namespace perfetto::trace_redaction {

base::Status PrunePackageList::Transform(const Context& context,
                                         std::string* packet) const {
  if (!context.package_uid.has_value()) {
    return base::ErrStatus("PrunePackageList: missing package uid.");
  }

  protozero::ProtoDecoder decoder(*packet);

  protos::pbzero::TracePacket::Decoder trace_packet_decoder(*packet);

  auto package_list =
      decoder.FindField(protos::pbzero::TracePacket::kPackagesListFieldNumber);

  if (!package_list.valid()) {
    return base::OkStatus();
  }

  protozero::HeapBuffered<protos::pbzero::TracePacket> packet_message;

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == protos::pbzero::TracePacket::kPackagesListFieldNumber) {
      OnPackageList(context, field.as_bytes(),
                    packet_message->set_packages_list());
    } else {
      proto_util::AppendField(field, packet_message.get());
    }
  }

  packet->assign(packet_message.SerializeAsString());

  return base::OkStatus();
}

void PrunePackageList::OnPackageList(
    const Context& context,
    protozero::ConstBytes bytes,
    protos::pbzero::PackagesList* message) const {
  PERFETTO_DCHECK(message);

  protozero::ProtoDecoder decoder(bytes);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == protos::pbzero::PackagesList::kPackagesFieldNumber) {
      // The package uid should already be normalized (see
      // find_package_info.cc).
      //
      // If there are more than one package entry (see
      // trace_redaction_framework.h for more details), we need to match all
      // instances here because retained processes will reference them.
      protos::pbzero::PackagesList::PackageInfo::Decoder info(field.as_bytes());

      if (info.has_uid() && NormalizeUid(info.uid()) == context.package_uid) {
        proto_util::AppendField(field, message);
      }
    } else {
      proto_util::AppendField(field, message);
    }
  }
}

}  // namespace perfetto::trace_redaction
