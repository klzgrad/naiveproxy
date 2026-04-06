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
#include "src/trace_processor/importers/proto/winscope/windowmanager_proto_clone.h"

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/trace/android/server/windowmanagerservice.pbzero.h"
#include "protos/perfetto/trace/android/windowmanager.pbzero.h"

namespace perfetto::trace_processor::winscope::windowmanager_proto_clone {

namespace {

void CloneField(const protozero::Field& field, protozero::Message* dst);
void CloneWindowContainerProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowContainerProto* dst_window_container);
void CloneWindowManagerServiceDumpProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowManagerServiceDumpProto* dst_window_manager_service);
void CloneDisplayContentProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::DisplayContentProto* dst_dc);
void CloneDisplayAreaProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::DisplayAreaProto* dst_da);
void CloneTaskProtoPruningChildren(protozero::ConstBytes src_bytes,
                                   protos::pbzero::TaskProto* dst_task);
void CloneActivityRecordProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::ActivityRecordProto* dst_activity);
void CloneWindowTokenProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowTokenProto* dst_wt);
void CloneWindowStateProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowStateProto* dst_ws);
void CloneTaskFragmentProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::TaskFragmentProto* dst_tf);

}  // namespace

std::vector<uint8_t> CloneEntryProtoPruningChildren(
    const protos::pbzero::WindowManagerTraceEntry::Decoder& root) {
  protozero::ConstBytes bytes{root.begin(),
                              static_cast<size_t>(root.end() - root.begin())};
  protozero::ProtoDecoder src_root(bytes);
  protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> dst_root_buf;
  protos::pbzero::WindowManagerTraceEntry* dst_root = dst_root_buf.get();

  for (auto field = src_root.ReadField(); field; field = src_root.ReadField()) {
    if (field.id() == protos::pbzero::WindowManagerTraceEntry::
                          kWindowManagerServiceFieldNumber) {
      auto* dst_window_manager_service = dst_root->BeginNestedMessage<
          protos::pbzero::WindowManagerServiceDumpProto>(field.id());
      CloneWindowManagerServiceDumpProtoPruningChildren(
          field.as_bytes(), dst_window_manager_service);
      continue;
    }
    CloneField(field, dst_root);
  }

  return dst_root_buf.SerializeAsArray();
}

std::vector<uint8_t> CloneRootWindowContainerProtoPruningChildren(
    const protos::pbzero::RootWindowContainerProto::Decoder& root) {
  protozero::ConstBytes bytes{root.begin(),
                              static_cast<size_t>(root.end() - root.begin())};
  protozero::ProtoDecoder src_root(bytes);
  protozero::HeapBuffered<protos::pbzero::RootWindowContainerProto>
      dst_root_buf;
  protos::pbzero::RootWindowContainerProto* dst_root = dst_root_buf.get();

  for (auto field = src_root.ReadField(); field; field = src_root.ReadField()) {
    if (field.id() ==
        protos::pbzero::RootWindowContainerProto::kWindowContainerFieldNumber) {
      auto* dst_window_container =
          dst_root->BeginNestedMessage<protos::pbzero::WindowContainerProto>(
              field.id());
      CloneWindowContainerProtoPruningChildren(field.as_bytes(),
                                               dst_window_container);
      continue;
    }
    CloneField(field, dst_root);
  }

  return dst_root_buf.SerializeAsArray();
}

std::vector<uint8_t> CloneWindowContainerChildProtoPruningChildren(
    const protos::pbzero::WindowContainerChildProto::Decoder& child) {
  protozero::ConstBytes bytes{child.begin(),
                              static_cast<size_t>(child.end() - child.begin())};
  protozero::ProtoDecoder src_child(bytes);
  protozero::HeapBuffered<protos::pbzero::WindowContainerChildProto>
      dst_child_buf;
  protos::pbzero::WindowContainerChildProto* dst_child = dst_child_buf.get();

  for (auto field = src_child.ReadField(); field;
       field = src_child.ReadField()) {
    switch (field.id()) {
      case protos::pbzero::WindowContainerChildProto::
          kWindowContainerFieldNumber:
        CloneWindowContainerProtoPruningChildren(
            field.as_bytes(), dst_child->set_window_container());
        break;
      case protos::pbzero::WindowContainerChildProto::
          kDisplayContentFieldNumber:
        CloneDisplayContentProtoPruningChildren(
            field.as_bytes(), dst_child->set_display_content());
        break;
      case protos::pbzero::WindowContainerChildProto::kDisplayAreaFieldNumber:
        CloneDisplayAreaProtoPruningChildren(field.as_bytes(),
                                             dst_child->set_display_area());
        break;
      case protos::pbzero::WindowContainerChildProto::kTaskFieldNumber:
        CloneTaskProtoPruningChildren(field.as_bytes(), dst_child->set_task());
        break;
      case protos::pbzero::WindowContainerChildProto::kActivityFieldNumber:
        CloneActivityRecordProtoPruningChildren(field.as_bytes(),
                                                dst_child->set_activity());
        break;
      case protos::pbzero::WindowContainerChildProto::kWindowTokenFieldNumber:
        CloneWindowTokenProtoPruningChildren(field.as_bytes(),
                                             dst_child->set_window_token());
        break;
      case protos::pbzero::WindowContainerChildProto::kWindowFieldNumber:
        CloneWindowStateProtoPruningChildren(field.as_bytes(),
                                             dst_child->set_window());
        break;
      case protos::pbzero::WindowContainerChildProto::kTaskFragmentFieldNumber:
        CloneTaskFragmentProtoPruningChildren(field.as_bytes(),
                                              dst_child->set_task_fragment());
        break;
      default:
        // Error: unexpected message format. This error is already detected and
        // handled in the caller (hierarchy walker).
        break;
    }
  }

  return dst_child_buf.SerializeAsArray();
}

namespace {

void CloneWindowManagerServiceDumpProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowManagerServiceDumpProto* dst_window_manager_service) {
  protozero::ProtoDecoder src_wc(src_bytes);
  for (auto field = src_wc.ReadField(); field; field = src_wc.ReadField()) {
    if (field.id() == protos::pbzero::WindowManagerServiceDumpProto::
                          kRootWindowContainerFieldNumber) {
      continue;  // prune children fields
    }
    CloneField(field, dst_window_manager_service);
  }
}

void CloneWindowContainerProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowContainerProto* dst_wc) {
  protozero::ProtoDecoder src_wc(src_bytes);
  for (auto field = src_wc.ReadField(); field; field = src_wc.ReadField()) {
    if (field.id() ==
        protos::pbzero::WindowContainerProto::kChildrenFieldNumber) {
      continue;  // prune children fields
    }
    CloneField(field, dst_wc);
  }
}

void CloneDisplayContentProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::DisplayContentProto* dst_dc) {
  protozero::ProtoDecoder src_dc(src_bytes);
  for (auto field = src_dc.ReadField(); field; field = src_dc.ReadField()) {
    if (field.id() ==
        protos::pbzero::DisplayContentProto::kRootDisplayAreaFieldNumber) {
      CloneDisplayAreaProtoPruningChildren(field.as_bytes(),
                                           dst_dc->set_root_display_area());
      continue;
    }
    CloneField(field, dst_dc);
  }
}

void CloneDisplayAreaProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::DisplayAreaProto* dst_da) {
  protozero::ProtoDecoder src_da(src_bytes);
  for (auto field = src_da.ReadField(); field; field = src_da.ReadField()) {
    if (field.id() ==
        protos::pbzero::DisplayAreaProto::kWindowContainerFieldNumber) {
      CloneWindowContainerProtoPruningChildren(field.as_bytes(),
                                               dst_da->set_window_container());
      continue;
    }
    CloneField(field, dst_da);
  }
}

void CloneTaskProtoPruningChildren(protozero::ConstBytes src_bytes,
                                   protos::pbzero::TaskProto* dst_task) {
  protozero::ProtoDecoder src_task(src_bytes);
  for (auto field = src_task.ReadField(); field; field = src_task.ReadField()) {
    if (field.id() == protos::pbzero::TaskProto::kWindowContainerFieldNumber) {
      CloneWindowContainerProtoPruningChildren(
          field.as_bytes(), dst_task->set_window_container());
      continue;
    }
    if (field.id() == protos::pbzero::TaskProto::kTaskFragmentFieldNumber) {
      CloneTaskFragmentProtoPruningChildren(field.as_bytes(),
                                            dst_task->set_task_fragment());
      continue;
    }
    CloneField(field, dst_task);
  }
}

void CloneActivityRecordProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::ActivityRecordProto* dst_activity) {
  protozero::ProtoDecoder src_activity(src_bytes);
  for (auto field = src_activity.ReadField(); field;
       field = src_activity.ReadField()) {
    if (field.id() ==
        protos::pbzero::ActivityRecordProto::kWindowTokenFieldNumber) {
      CloneWindowTokenProtoPruningChildren(field.as_bytes(),
                                           dst_activity->set_window_token());
      continue;
    }
    CloneField(field, dst_activity);
  }
}

void CloneWindowTokenProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowTokenProto* dst_wt) {
  protozero::ProtoDecoder src_wt(src_bytes);
  for (auto field = src_wt.ReadField(); field; field = src_wt.ReadField()) {
    if (field.id() ==
        protos::pbzero::WindowTokenProto::kWindowContainerFieldNumber) {
      CloneWindowContainerProtoPruningChildren(field.as_bytes(),
                                               dst_wt->set_window_container());
      continue;
    }
    CloneField(field, dst_wt);
  }
}

void CloneWindowStateProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::WindowStateProto* dst_ws) {
  protozero::ProtoDecoder src_ws(src_bytes);
  for (auto field = src_ws.ReadField(); field; field = src_ws.ReadField()) {
    if (field.id() ==
        protos::pbzero::WindowStateProto::kWindowContainerFieldNumber) {
      CloneWindowContainerProtoPruningChildren(field.as_bytes(),
                                               dst_ws->set_window_container());
      continue;
    }
    CloneField(field, dst_ws);
  }
}

void CloneTaskFragmentProtoPruningChildren(
    protozero::ConstBytes src_bytes,
    protos::pbzero::TaskFragmentProto* dst_tf) {
  protozero::ProtoDecoder src_tf(src_bytes);
  for (auto field = src_tf.ReadField(); field; field = src_tf.ReadField()) {
    if (field.id() ==
        protos::pbzero::TaskFragmentProto::kWindowContainerFieldNumber) {
      CloneWindowContainerProtoPruningChildren(field.as_bytes(),
                                               dst_tf->set_window_container());
      continue;
    }
    CloneField(field, dst_tf);
  }
}

void CloneField(const protozero::Field& field, protozero::Message* dst) {
  switch (field.type()) {
    case protozero::proto_utils::ProtoWireType::kVarInt:
      dst->AppendVarInt(field.id(), field.raw_int_value());
      break;
    case protozero::proto_utils::ProtoWireType::kFixed32:
      dst->AppendFixed(field.id(), field.as_uint32());
      break;
    case protozero::proto_utils::ProtoWireType::kFixed64:
      dst->AppendFixed(field.id(), field.as_uint64());
      break;
    case protozero::proto_utils::ProtoWireType::kLengthDelimited:
      dst->AppendBytes(field.id(), field.as_bytes().data,
                       field.as_bytes().size);
      break;
  }
}

}  // namespace

}  // namespace perfetto::trace_processor::winscope::windowmanager_proto_clone
