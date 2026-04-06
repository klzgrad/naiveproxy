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

#include "perfetto/ext/trace_processor/importers/memory_tracker/graph.h"

namespace perfetto {
namespace trace_processor {

namespace {

using Edge = GlobalNodeGraph::Edge;
using PostOrderIterator = GlobalNodeGraph::PostOrderIterator;
using PreOrderIterator = GlobalNodeGraph::PreOrderIterator;
using Process = GlobalNodeGraph::Process;
using Node = GlobalNodeGraph::Node;
using perfetto::base::SplitString;

}  // namespace

GlobalNodeGraph::GlobalNodeGraph()
    : shared_memory_graph_(
          std::unique_ptr<Process>(new Process(kNullProcessId, this))) {}
GlobalNodeGraph::~GlobalNodeGraph() {}

Process* GlobalNodeGraph::CreateGraphForProcess(
    base::PlatformProcessId process_id) {
  auto id_to_node_iterator = process_node_graphs_.emplace(
      process_id, std::unique_ptr<Process>(new Process(process_id, this)));
  return id_to_node_iterator.first->second.get();
}

void GlobalNodeGraph::AddNodeOwnershipEdge(Node* owner,
                                           Node* owned,
                                           int importance) {
  all_edges_.emplace_front(owner, owned, importance);
  Edge* edge = &*all_edges_.begin();
  owner->SetOwnsEdge(edge);
  owned->AddOwnedByEdge(edge);
}

Node* GlobalNodeGraph::CreateNode(Process* process_graph, Node* parent) {
  all_nodes_.emplace_front(process_graph, parent);
  return &*all_nodes_.begin();
}

PreOrderIterator GlobalNodeGraph::VisitInDepthFirstPreOrder() {
  std::vector<Node*> roots;
  for (auto it = process_node_graphs_.rbegin();
       it != process_node_graphs_.rend(); it++) {
    roots.push_back(it->second->root());
  }
  roots.push_back(shared_memory_graph_->root());
  return PreOrderIterator{std::move(roots)};
}

PostOrderIterator GlobalNodeGraph::VisitInDepthFirstPostOrder() {
  std::vector<Node*> roots;
  for (auto it = process_node_graphs_.rbegin();
       it != process_node_graphs_.rend(); it++) {
    roots.push_back(it->second->root());
  }
  roots.push_back(shared_memory_graph_->root());
  return PostOrderIterator(std::move(roots));
}

Process::Process(base::PlatformProcessId pid, GlobalNodeGraph* global_graph)
    : pid_(pid),
      global_graph_(global_graph),
      root_(global_graph->CreateNode(this, nullptr)) {}
Process::~Process() {}

Node* Process::CreateNode(MemoryAllocatorNodeId id,
                          const std::string& path,
                          bool weak) {
  auto tokens = base::SplitString(path, "/");

  // Perform a tree traversal, creating the nodes if they do not
  // already exist on the path to the child.
  Node* current = root_;
  for (const auto& key : tokens) {
    Node* parent = current;
    current = current->GetChild(key);
    if (!current) {
      current = global_graph_->CreateNode(this, parent);
      parent->InsertChild(key, current);
    }
  }

  // The final node should have the weakness specified by the
  // argument and also be considered explicit.
  current->set_weak(weak);
  current->set_explicit(true);

  // The final node should also have the associated |id|.
  current->set_id(id);

  // Add to the global id map as well if it exists.
  if (!id.empty())
    global_graph_->nodes_by_id_.emplace(id, current);

  return current;
}

Node* Process::FindNode(const std::string& path) {
  auto tokens = base::SplitString(path, "/");

  Node* current = root_;
  for (const auto& key : tokens) {
    current = current->GetChild(key);
    if (!current)
      return nullptr;
  }
  return current;
}

Node::Node(Process* node_graph, Node* parent)
    : node_graph_(node_graph), parent_(parent), owns_edge_(nullptr) {}
Node::~Node() {}

Node* Node::GetChild(const std::string& name) const {
  auto child = children_.find(name);
  return child == children_.end() ? nullptr : child->second;
}

void Node::InsertChild(const std::string& name, Node* node) {
  PERFETTO_DCHECK(node);
  children_.emplace(name, node);
}

Node* Node::CreateChild(const std::string& name) {
  Node* new_child = node_graph_->global_graph()->CreateNode(node_graph_, this);
  InsertChild(name, new_child);
  return new_child;
}

bool Node::IsDescendentOf(const Node& possible_parent) const {
  const Node* current = this;
  while (current != nullptr) {
    if (current == &possible_parent)
      return true;
    current = current->parent();
  }
  return false;
}

void Node::AddOwnedByEdge(Edge* edge) {
  owned_by_edges_.push_back(edge);
}

void Node::SetOwnsEdge(Edge* owns_edge) {
  owns_edge_ = owns_edge;
}

void Node::AddEntry(const std::string& name,
                    Node::Entry::ScalarUnits units,
                    uint64_t value) {
  entries_.emplace(name, Node::Entry(units, value));
}

void Node::AddEntry(const std::string& name, const std::string& value) {
  entries_.emplace(name, Node::Entry(value));
}

Node::Entry::Entry(Entry::ScalarUnits units2, uint64_t value)
    : type(Node::Entry::Type::kUInt64), units(units2), value_uint64(value) {}

Node::Entry::Entry(const std::string& value)
    : type(Node::Entry::Type::kString),
      units(Node::Entry::ScalarUnits::kObjects),
      value_string(value),
      value_uint64(0) {}

Edge::Edge(Node* source, Node* target, int priority)
    : source_(source), target_(target), priority_(priority) {}

PreOrderIterator::PreOrderIterator(std::vector<Node*>&& roots)
    : to_visit_(std::move(roots)) {}
PreOrderIterator::PreOrderIterator(PreOrderIterator&& other) = default;
PreOrderIterator::~PreOrderIterator() {}

// Yields the next node in the DFS post-order traversal.
Node* PreOrderIterator::next() {
  while (!to_visit_.empty()) {
    // Retain a pointer to the node at the top and remove it from stack.
    Node* node = to_visit_.back();
    to_visit_.pop_back();

    // If the node has already been visited, don't visit it again.
    if (visited_.count(node) != 0)
      continue;

    // If we haven't visited the node which this node owns then wait for that.
    if (node->owns_edge() && visited_.count(node->owns_edge()->target()) == 0)
      continue;

    // If we haven't visited the node's parent then wait for that.
    if (node->parent() && visited_.count(node->parent()) == 0)
      continue;

    // Visit all children of this node.
    for (auto it = node->children()->rbegin(); it != node->children()->rend();
         it++) {
      to_visit_.push_back(it->second);
    }

    // Visit all owners of this node.
    for (auto it = node->owned_by_edges()->rbegin();
         it != node->owned_by_edges()->rend(); it++) {
      to_visit_.push_back((*it)->source());
    }

    // Add this node to the visited set.
    visited_.insert(node);
    return node;
  }
  return nullptr;
}

PostOrderIterator::PostOrderIterator(std::vector<Node*>&& roots)
    : to_visit_(std::move(roots)) {}
PostOrderIterator::PostOrderIterator(PostOrderIterator&& other) = default;
PostOrderIterator::~PostOrderIterator() = default;

// Yields the next node in the DFS post-order traversal.
Node* PostOrderIterator::next() {
  while (!to_visit_.empty()) {
    // Retain a pointer to the node at the top and remove it from stack.
    Node* node = to_visit_.back();
    to_visit_.pop_back();

    // If the node has already been visited, don't visit it again.
    if (visited_.count(node) != 0)
      continue;

    // If the node is at the top of the path, we have already looked
    // at its children and owners.
    if (!path_.empty() && path_.back() == node) {
      // Mark the current node as visited so we don't visit again.
      visited_.insert(node);

      // The current node is no longer on the path.
      path_.pop_back();

      return node;
    }

    // If the node is not at the front, it should also certainly not be
    // anywhere else in the path. If it is, there is a cycle in the graph.
    path_.push_back(node);

    // Add this node back to the queue of nodes to visit.
    to_visit_.push_back(node);

    // Visit all children of this node.
    for (auto it = node->children()->rbegin(); it != node->children()->rend();
         it++) {
      to_visit_.push_back(it->second);
    }

    // Visit all owners of this node.
    for (auto it = node->owned_by_edges()->rbegin();
         it != node->owned_by_edges()->rend(); it++) {
      to_visit_.push_back((*it)->source());
    }
  }
  return nullptr;
}

}  // namespace trace_processor
}  // namespace perfetto
