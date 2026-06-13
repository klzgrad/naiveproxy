/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/profiling/common/callstack_trie.h"

#include <vector>

#include "perfetto/ext/base/string_splitter.h"
#include "src/profiling/common/interner.h"
#include "src/profiling/common/unwind_support.h"

namespace perfetto {
namespace profiling {

GlobalCallstackTrie::Node* GlobalCallstackTrie::GetOrCreateChild(
    Node* self,
    const Interned<Frame>& loc) {
  Node* child = self->GetChild(loc);
  if (!child)
    child = self->AddChild(loc, ++next_callstack_id_, self);
  return child;
}

std::vector<Interned<Frame>> GlobalCallstackTrie::BuildInverseCallstack(
    const Node* node) const {
  std::vector<Interned<Frame>> res;
  while (node != &root_) {
    res.emplace_back(node->location_);
    node = node->parent_;
  }
  return res;
}

GlobalCallstackTrie::Node* GlobalCallstackTrie::CreateCallsite(
    const std::vector<unwindstack::FrameData>& callstack,
    const std::vector<std::string>& build_ids) {
  PERFETTO_CHECK(callstack.size() == build_ids.size());
  Node* node = &root_;
  // libunwindstack gives the frames top-first, but we want to bookkeep and
  // emit as bottom first.
  auto callstack_it = callstack.crbegin();
  auto build_id_it = build_ids.crbegin();
  for (; callstack_it != callstack.crend() && build_id_it != build_ids.crend();
       ++callstack_it, ++build_id_it) {
    const unwindstack::FrameData& loc = *callstack_it;
    const std::string& build_id = *build_id_it;
    node = GetOrCreateChild(node, InternCodeLocation(loc, build_id));
  }
  return node;
}

GlobalCallstackTrie::Node* GlobalCallstackTrie::CreateCallsite(
    const std::vector<Interned<Frame>>& callstack) {
  Node* node = &root_;
  // libunwindstack gives the frames top-first, but we want to bookkeep and
  // emit as bottom first.
  for (auto it = callstack.crbegin(); it != callstack.crend(); ++it) {
    const Interned<Frame>& loc = *it;
    node = GetOrCreateChild(node, loc);
  }
  return node;
}

void GlobalCallstackTrie::IncrementNode(Node* node) {
  while (node != nullptr) {
    node->ref_count_ += 1;
    node = node->parent_;
  }
}

void GlobalCallstackTrie::DecrementNode(Node* node) {
  PERFETTO_DCHECK(node->ref_count_ >= 1);

  bool delete_prev = false;
  Node* prev = nullptr;
  while (node != nullptr) {
    if (delete_prev)
      node->RemoveChild(prev);
    node->ref_count_ -= 1;
    delete_prev = node->ref_count_ == 0;
    prev = node;
    node = node->parent_;
  }
}

Interned<Frame> GlobalCallstackTrie::InternCodeLocation(
    const unwindstack::FrameData& loc,
    const std::string& build_id) {
  Mapping map(string_interner_.Intern(build_id));
  if (loc.map_info != nullptr) {
    map.exact_offset = loc.map_info->offset();
    map.start_offset = loc.map_info->elf_start_offset();
    map.start = loc.map_info->start();
    map.end = loc.map_info->end();
    map.load_bias = loc.map_info->GetLoadBias();
    base::StringSplitter sp(loc.map_info->GetFullName(), '/');
    while (sp.Next())
      map.path_components.emplace_back(string_interner_.Intern(sp.cur_token()));
  }

  Frame frame(mapping_interner_.Intern(std::move(map)),
              string_interner_.Intern(loc.function_name), loc.rel_pc);

  return frame_interner_.Intern(frame);
}

Interned<Frame> GlobalCallstackTrie::MakeRootFrame() {
  Mapping map(string_interner_.Intern(""));

  Frame frame(mapping_interner_.Intern(std::move(map)),
              string_interner_.Intern(""), 0);

  return frame_interner_.Intern(frame);
}

GlobalCallstackTrie::Node* GlobalCallstackTrie::Node::AddChild(
    const Interned<Frame>& loc,
    uint64_t callstack_id,
    Node* parent) {
  auto it = children_.emplace(loc, callstack_id, parent);
  return const_cast<Node*>(&(*it.first));
}
void GlobalCallstackTrie::Node::RemoveChild(Node* node) {
  children_.erase(*node);
}

GlobalCallstackTrie::Node* GlobalCallstackTrie::Node::GetChild(
    const Interned<Frame>& loc) {
  // This will be nicer with C++14 transparent comparators.
  // Then we will be able to look up by just the key using a sutiable
  // comparator.
  //
  // For now we need to allow to construct Node from the key.
  Node node(loc);
  auto it = children_.find(node);
  if (it == children_.end())
    return nullptr;
  return const_cast<Node*>(&(*it));
}

}  // namespace profiling
}  // namespace perfetto
