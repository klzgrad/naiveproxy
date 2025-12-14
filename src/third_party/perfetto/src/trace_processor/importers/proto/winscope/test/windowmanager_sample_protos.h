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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_TEST_WINDOWMANAGER_SAMPLE_PROTOS_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_TEST_WINDOWMANAGER_SAMPLE_PROTOS_H_

#include <cstdint>
#include <string>

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/trace/android/graphics/rect.pbzero.h"
#include "protos/perfetto/trace/android/server/windowmanagerservice.pbzero.h"
#include "protos/perfetto/trace/android/view/displayinfo.pbzero.h"
#include "protos/perfetto/trace/android/view/windowlayoutparams.pbzero.h"
#include "protos/perfetto/trace/android/windowmanager.pbzero.h"

namespace perfetto::trace_processor::winscope {

class WindowManagerSampleProtos {
 public:
  static std::string EmptyHierarchy() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;
    entry->set_window_manager_service();
    return entry.SerializeAsString();
  }

  static std::string HierarchyWithRootOnly() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;
    AddRoot(&entry);
    return entry.SerializeAsString();
  }

  static std::string HierarchyWithWindowContainer() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* window_container = root->add_children()->set_window_container();
    auto* identifier = window_container->set_identifier();
    identifier->set_hash_code(2);
    identifier->set_title("child - WindowContainer");

    AddGrandchild(window_container);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithDisplayContentAndWindowState() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    // child - DisplayContent
    auto* display_content = root->add_children()->set_display_content();
    display_content->set_id(1);

    auto* display_info = display_content->set_display_info();
    display_info->set_name("child - DisplayContent");
    display_info->set_logical_width(10);
    display_info->set_logical_height(20);

    auto* window_container =
        display_content->set_root_display_area()->set_window_container();
    window_container->set_identifier()->set_hash_code(2);

    // grandchild - WindowState
    auto* window_state = window_container->add_children()->set_window();
    window_state->set_is_visible(true);

    auto* attributes = window_state->set_attributes();
    attributes->set_alpha(0.5);

    auto* frame = window_state->set_window_frames()->set_frame();
    frame->set_left(5);
    frame->set_top(6);
    frame->set_right(15);
    frame->set_bottom(26);

    auto* window_state_window_container = window_state->set_window_container();
    auto* window_state_identifier =
        window_state_window_container->set_identifier();
    window_state_identifier->set_hash_code(3);
    window_state_identifier->set_title("grandchild - WindowState");

    // grandgrandchild - WindowContainer
    auto* window_container_identifier =
        window_state_window_container->add_children()
            ->set_window_container()
            ->set_identifier();
    window_container_identifier->set_hash_code(4);
    window_container_identifier->set_title("grandgrandchild - WindowContainer");

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithWindowStateNameOverrides() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    std::vector<std::string> prefixes = {"Starting", "Waiting For Debugger:"};
    for (size_t i = 0; i < prefixes.size(); i++) {
      auto* window_state = root->add_children()->set_window();
      auto* window_state_window_container =
          window_state->set_window_container();
      auto* window_state_identifier =
          window_state_window_container->set_identifier();
      window_state_identifier->set_hash_code(2 + static_cast<int32_t>(i));
      window_state_identifier->set_title(prefixes[i] + " state - WindowState");
    }

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithDisplayArea() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* display_area = root->add_children()->set_display_area();
    display_area->set_name("child - DisplayArea");

    auto* window_container = display_area->set_window_container();
    window_container->set_identifier()->set_hash_code(2);

    AddGrandchild(window_container);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithTask() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* task_fragment = root->add_children()->set_task()->set_task_fragment();
    auto* window_container = task_fragment->set_window_container();
    auto* identifier = window_container->set_identifier();
    identifier->set_hash_code(2);
    identifier->set_title("child - Task");

    AddGrandchild(window_container);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithTaskIdAndName() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* task = root->add_children()->set_task();
    task->set_id(3);
    task->set_task_name("MockTask");
    auto* task_fragment = task->set_task_fragment();
    auto* window_container = task_fragment->set_window_container();
    auto* identifier = window_container->set_identifier();
    identifier->set_hash_code(2);
    identifier->set_title("child - Task");

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithTaskContainerFallback() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* task = root->add_children()->set_task();
    auto* task_fragment = task->set_task_fragment();
    auto* window_container = task_fragment->set_window_container();
    auto* identifier = window_container->set_identifier();
    identifier->set_hash_code(2);
    identifier->set_title("child - Task");

    auto* task_window_container = task->set_window_container();
    AddGrandchild(task_window_container);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithActivityRecord() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* activity = root->add_children()->set_activity();
    auto* window_token = activity->set_window_token();
    auto* window_container = window_token->set_window_container();

    AddGrandchild(window_container);

    window_token->set_hash_code(2);
    activity->set_name("child - ActivityRecord");

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithWindowToken() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* window_token = root->add_children()->set_window_token();
    auto* window_container = window_token->set_window_container();

    AddGrandchild(window_container);

    // The hash code is also used as title af WindowTokenProto
    window_token->set_hash_code(2);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithTaskFragment() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* window_container =
        root->add_children()->set_task_fragment()->set_window_container();
    auto* identifier = window_container->set_identifier();
    identifier->set_hash_code(2);
    identifier->set_title("child - TaskFragment");

    AddGrandchild(window_container);

    return entry.SerializeAsString();
  }

  static std::string HierarchyWithSiblings() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);

    auto* identifier1 =
        root->add_children()->set_window_container()->set_identifier();
    identifier1->set_hash_code(2);
    identifier1->set_title("child - WindowContainer1");

    auto* identifier2 =
        root->add_children()->set_window_container()->set_identifier();
    identifier2->set_hash_code(3);
    identifier2->set_title("child - WindowContainer2");

    return entry.SerializeAsString();
  }

  static std::string InvalidWindowContainerChildProto() {
    protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry> entry;

    auto* root = AddRoot(&entry);
    auto* child = root->add_children();
    (void)child;

    return entry.SerializeAsString();
  }

 private:
  static protos::pbzero::WindowContainerProto* AddRoot(
      protozero::HeapBuffered<protos::pbzero::WindowManagerTraceEntry>* entry) {
    auto* root = (*entry)
                     ->set_window_manager_service()
                     ->set_root_window_container()
                     ->set_window_container();
    auto* root_identifier = root->set_identifier();
    root_identifier->set_hash_code(1);
    root_identifier->set_title("root");
    return root;
  }

  static protos::pbzero::WindowContainerProto* AddGrandchild(
      protos::pbzero::WindowContainerProto* window_container) {
    auto* grandchild = window_container->add_children()->set_window_container();
    auto* grandchild_identifier = grandchild->set_identifier();
    grandchild_identifier->set_hash_code(3);
    grandchild_identifier->set_title("grandchild - WindowContainer");
    return grandchild;
  }
};

}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_TEST_WINDOWMANAGER_SAMPLE_PROTOS_H_
