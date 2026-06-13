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

#include "perfetto/ext/trace_processor/importers/memory_tracker/graph_processor.h"

#include <list>

namespace perfetto {
namespace trace_processor {

using Edge = GlobalNodeGraph::Edge;
using Node = GlobalNodeGraph::Node;
using Process = GlobalNodeGraph::Process;

namespace {

const char kSharedMemoryRootNode[] = "shared_memory";
const char kSizeEntryName[] = "size";
const char kEffectiveSizeEntryName[] = "effective_size";

Node::Entry::ScalarUnits EntryUnitsFromString(const std::string& units) {
  if (units == RawMemoryGraphNode::kUnitsBytes) {
    return Node::Entry::ScalarUnits::kBytes;
  } else if (units == RawMemoryGraphNode::kUnitsObjects) {
    return Node::Entry::ScalarUnits::kObjects;
  } else {
    // Invalid units so we just return a value of the correct type.
    return Node::Entry::ScalarUnits::kObjects;
  }
}

std::optional<uint64_t> GetSizeEntryOfNode(Node* node) {
  auto size_it = node->entries()->find(kSizeEntryName);
  if (size_it == node->entries()->end())
    return std::nullopt;

  PERFETTO_DCHECK(size_it->second.type == Node::Entry::Type::kUInt64);
  PERFETTO_DCHECK(size_it->second.units == Node::Entry::ScalarUnits::kBytes);
  return std::optional<uint64_t>(size_it->second.value_uint64);
}

}  // namespace

// static
std::unique_ptr<GlobalNodeGraph> GraphProcessor::CreateMemoryGraph(
    const GraphProcessor::RawMemoryNodeMap& process_nodes) {
  auto global_graph = std::unique_ptr<GlobalNodeGraph>(new GlobalNodeGraph());

  // First pass: collects allocator nodes into a graph and populate
  // with entries.
  for (const auto& pid_to_node : process_nodes) {
    // There can be null entries in the map; simply filter these out.
    if (!pid_to_node.second)
      continue;

    auto* graph = global_graph->CreateGraphForProcess(pid_to_node.first);
    CollectAllocatorNodes(*pid_to_node.second, global_graph.get(), graph);
  }

  // Second pass: generate the graph of edges between the nodes.
  for (const auto& pid_to_node : process_nodes) {
    // There can be null entries in the map; simply filter these out.
    if (!pid_to_node.second)
      continue;

    AddEdges(*pid_to_node.second, global_graph.get());
  }

  return global_graph;
}

// static
void GraphProcessor::RemoveWeakNodesFromGraph(GlobalNodeGraph* global_graph) {
  auto* global_root = global_graph->shared_memory_graph()->root();

  // Third pass: mark recursively nodes as weak if they don't have an associated
  // node and all their children are weak.
  MarkImplicitWeakParentsRecursively(global_root);
  for (const auto& pid_to_process : global_graph->process_node_graphs()) {
    MarkImplicitWeakParentsRecursively(pid_to_process.second->root());
  }

  // Fourth pass: recursively mark nodes as weak if they own a node which is
  // weak or if they have a parent who is weak.
  {
    std::set<const Node*> visited;
    MarkWeakOwnersAndChildrenRecursively(global_root, &visited);
    for (const auto& pid_to_process : global_graph->process_node_graphs()) {
      MarkWeakOwnersAndChildrenRecursively(pid_to_process.second->root(),
                                           &visited);
    }
  }

  // Fifth pass: remove all nodes which are weak (including their descendants)
  // and clean owned by edges to match.
  RemoveWeakNodesRecursively(global_root);
  for (const auto& pid_to_process : global_graph->process_node_graphs()) {
    RemoveWeakNodesRecursively(pid_to_process.second->root());
  }
}

// static
void GraphProcessor::AddOverheadsAndPropagateEntries(
    GlobalNodeGraph* global_graph) {
  // Sixth pass: account for tracing overhead in system memory allocators.
  for (auto& pid_to_process : global_graph->process_node_graphs()) {
    Process* process = pid_to_process.second.get();
    if (process->FindNode("winheap")) {
      AssignTracingOverhead("winheap", global_graph,
                            pid_to_process.second.get());
    } else if (process->FindNode("malloc")) {
      AssignTracingOverhead("malloc", global_graph,
                            pid_to_process.second.get());
    }
  }

  // Seventh pass: aggregate non-size integer entries into parents and propagate
  // string and int entries for shared graph.
  auto* global_root = global_graph->shared_memory_graph()->root();
  AggregateNumericsRecursively(global_root);
  PropagateNumericsAndDiagnosticsRecursively(global_root);
  for (auto& pid_to_process : global_graph->process_node_graphs()) {
    AggregateNumericsRecursively(pid_to_process.second->root());
  }
}

// static
void GraphProcessor::CalculateSizesForGraph(GlobalNodeGraph* global_graph) {
  // Eighth pass: calculate the size field for nodes by considering the sizes
  // of their children and owners.
  {
    auto it = global_graph->VisitInDepthFirstPostOrder();
    while (Node* node = it.next()) {
      CalculateSizeForNode(node);
    }
  }

  // Ninth pass: Calculate not-owned and not-owning sub-sizes of all nodes.
  {
    auto it = global_graph->VisitInDepthFirstPostOrder();
    while (Node* node = it.next()) {
      CalculateNodeSubSizes(node);
    }
  }

  // Tenth pass: Calculate owned and owning coefficients of owned and owner
  // nodes.
  {
    auto it = global_graph->VisitInDepthFirstPostOrder();
    while (Node* node = it.next()) {
      CalculateNodeOwnershipCoefficient(node);
    }
  }

  // Eleventh pass: Calculate cumulative owned and owning coefficients of all
  // nodes.
  {
    auto it = global_graph->VisitInDepthFirstPreOrder();
    while (Node* node = it.next()) {
      CalculateNodeCumulativeOwnershipCoefficient(node);
    }
  }

  // Twelfth pass: Calculate the effective sizes of all nodes.
  {
    auto it = global_graph->VisitInDepthFirstPostOrder();
    while (Node* node = it.next()) {
      CalculateNodeEffectiveSize(node);
    }
  }
}

// static
std::map<base::PlatformProcessId, uint64_t>
GraphProcessor::ComputeSharedFootprintFromGraph(
    const GlobalNodeGraph& global_graph) {
  // Go through all nodes associated with global nodes and find if they are
  // owned by shared memory nodes.
  Node* global_root =
      global_graph.shared_memory_graph()->root()->GetChild("global");

  // If there are no global nodes then just return an empty map with no data.
  if (!global_root)
    return std::map<base::PlatformProcessId, uint64_t>();

  struct GlobalNodeOwners {
    std::list<Edge*> edges;
    int max_priority = 0;
  };

  std::map<Node*, GlobalNodeOwners> global_node_to_shared_owners;
  for (const auto& path_to_child : *global_root->children()) {
    // The path of this node is something like "global/foo".
    Node* global_node = path_to_child.second;

    // If there's no size to attribute, there's no point in propagating
    // anything.
    if (global_node->entries()->count(kSizeEntryName) == 0)
      continue;

    for (auto* edge : *global_node->owned_by_edges()) {
      // Find if the source node's path starts with "shared_memory/" which
      // indcates shared memory.
      Node* source_root = edge->source()->node_graph()->root();
      const Node* current = edge->source();
      PERFETTO_DCHECK(current != source_root);

      // Traverse up until we hit the point where |current| holds a node which
      // is the child of |source_root|.
      while (current->parent() != source_root)
        current = current->parent();

      // If the source is indeed a shared memory node, add the edge to the map.
      if (source_root->GetChild(kSharedMemoryRootNode) == current) {
        GlobalNodeOwners* owners = &global_node_to_shared_owners[global_node];
        owners->edges.push_back(edge);
        owners->max_priority = std::max(owners->max_priority, edge->priority());
      }
    }
  }

  // Go through the map and leave only the edges which have the maximum
  // priority.
  for (auto& global_to_shared_edges : global_node_to_shared_owners) {
    int max_priority = global_to_shared_edges.second.max_priority;
    global_to_shared_edges.second.edges.remove_if(
        [max_priority](Edge* edge) { return edge->priority() < max_priority; });
  }

  // Compute the footprints by distributing the memory of the nodes
  // among the processes which have edges left.
  std::map<base::PlatformProcessId, uint64_t> pid_to_shared_footprint;
  for (const auto& global_to_shared_edges : global_node_to_shared_owners) {
    Node* node = global_to_shared_edges.first;
    const auto& edges = global_to_shared_edges.second.edges;

    const Node::Entry& size_entry =
        node->entries()->find(kSizeEntryName)->second;
    PERFETTO_DCHECK(size_entry.type == Node::Entry::kUInt64);

    uint64_t size_per_process = size_entry.value_uint64 / edges.size();
    for (auto* edge : edges) {
      base::PlatformProcessId pid = edge->source()->node_graph()->pid();
      pid_to_shared_footprint[pid] += size_per_process;
    }
  }

  return pid_to_shared_footprint;
}

// static
void GraphProcessor::CollectAllocatorNodes(const RawProcessMemoryNode& source,
                                           GlobalNodeGraph* global_graph,
                                           Process* process_graph) {
  // Turn each node into a node in the graph of nodes in the appropriate
  // process node or global node.
  for (const auto& path_to_node : source.allocator_nodes()) {
    const std::string& path = path_to_node.first;
    const RawMemoryGraphNode& raw_node = *path_to_node.second;

    // All global nodes (i.e. those starting with global/) should be redirected
    // to the shared graph.
    bool is_global = base::StartsWith(path, "global/");
    Process* process =
        is_global ? global_graph->shared_memory_graph() : process_graph;

    Node* node;
    auto node_iterator = global_graph->nodes_by_id().find(raw_node.id());
    if (node_iterator == global_graph->nodes_by_id().end()) {
      // Storing whether the process is weak here will allow for later
      // computations on whether or not the node should be removed.
      bool is_weak = raw_node.flags() & RawMemoryGraphNode::Flags::kWeak;
      node = process->CreateNode(raw_node.id(), path, is_weak);
    } else {
      node = node_iterator->second;

      PERFETTO_DCHECK(node == process->FindNode(path));

      PERFETTO_DCHECK(is_global);
    }

    // Copy any entries not already present into the node.
    for (auto& entry : raw_node.entries()) {
      switch (entry.entry_type) {
        case RawMemoryGraphNode::MemoryNodeEntry::EntryType::kUint64:
          node->AddEntry(entry.name, EntryUnitsFromString(entry.units),
                         entry.value_uint64);
          break;
        case RawMemoryGraphNode::MemoryNodeEntry::EntryType::kString:
          node->AddEntry(entry.name, entry.value_string);
          break;
      }
    }
  }
}

// static
void GraphProcessor::AddEdges(const RawProcessMemoryNode& source,
                              GlobalNodeGraph* global_graph) {
  const auto& nodes_by_id = global_graph->nodes_by_id();
  for (const auto& id_to_edge : source.allocator_nodes_edges()) {
    auto& edge = id_to_edge.second;

    // Find the source and target nodes in the global map by id.
    auto source_it = nodes_by_id.find(edge->source);
    auto target_it = nodes_by_id.find(edge->target);

    if (source_it == nodes_by_id.end()) {
      // If the source is missing then simply pretend the edge never existed
      // leading to the memory being allocated to the target (if it exists).
      continue;
    } else if (target_it == nodes_by_id.end()) {
      // If the target is lost but the source is present, then also ignore
      // this edge for now.
      // TODO(lalitm): see crbug.com/770712 for the permanent fix for this
      // issue.
      continue;
    } else {
      // Add an edge indicating the source node owns the memory of the
      // target node with the given importance of the edge.
      global_graph->AddNodeOwnershipEdge(source_it->second, target_it->second,
                                         edge->importance);
    }
  }
}

// static
void GraphProcessor::MarkImplicitWeakParentsRecursively(Node* node) {
  // Ensure that we aren't in a bad state where we have an implicit node
  // which doesn't have any children (which is not the root node).
  PERFETTO_DCHECK(node->is_explicit() || !node->children()->empty() ||
                  !node->parent());

  // Check that at this stage, any node which is weak is only so because
  // it was explicitly created as such.
  PERFETTO_DCHECK(!node->is_weak() || node->is_explicit());

  // If a node is already weak then all children will be marked weak at a
  // later stage.
  if (node->is_weak())
    return;

  // Recurse into each child and find out if all the children of this node are
  // weak.
  bool all_children_weak = true;
  for (const auto& path_to_child : *node->children()) {
    MarkImplicitWeakParentsRecursively(path_to_child.second);
    all_children_weak = all_children_weak && path_to_child.second->is_weak();
  }

  // If all the children are weak and the parent is only an implicit one then we
  // consider the parent as weak as well and we will later remove it.
  node->set_weak(!node->is_explicit() && all_children_weak);
}

// static
void GraphProcessor::MarkWeakOwnersAndChildrenRecursively(
    Node* node,
    std::set<const Node*>* visited) {
  // If we've already visited this node then nothing to do.
  if (visited->count(node) != 0)
    return;

  // If we haven't visited the node which this node owns then wait for that.
  if (node->owns_edge() && visited->count(node->owns_edge()->target()) == 0)
    return;

  // If we haven't visited the node's parent then wait for that.
  if (node->parent() && visited->count(node->parent()) == 0)
    return;

  // If either the node we own or our parent is weak, then mark this node
  // as weak.
  if ((node->owns_edge() && node->owns_edge()->target()->is_weak()) ||
      (node->parent() && node->parent()->is_weak())) {
    node->set_weak(true);
  }
  visited->insert(node);

  // Recurse into each owner node to mark any other nodes.
  for (auto* owned_by_edge : *node->owned_by_edges()) {
    MarkWeakOwnersAndChildrenRecursively(owned_by_edge->source(), visited);
  }

  // Recurse into each child and find out if all the children of this node are
  // weak.
  for (const auto& path_to_child : *node->children()) {
    MarkWeakOwnersAndChildrenRecursively(path_to_child.second, visited);
  }
}

// static
void GraphProcessor::RemoveWeakNodesRecursively(Node* node) {
  auto* children = node->children();
  for (auto child_it = children->begin(); child_it != children->end();) {
    Node* child = child_it->second;

    // If the node is weak, remove it. This automatically makes all
    // descendents unreachable from the parents. If this node owned
    // by another, it will have been marked earlier in
    // |MarkWeakOwnersAndChildrenRecursively| and so will be removed
    // by this method at some point.
    if (child->is_weak()) {
      child_it = children->erase(child_it);
      continue;
    }

    // We should never be in a situation where we're about to
    // keep a node which owns a weak node (which will be/has been
    // removed).
    PERFETTO_DCHECK(!child->owns_edge() ||
                    !child->owns_edge()->target()->is_weak());

    // Descend and remove all weak child nodes.
    RemoveWeakNodesRecursively(child);

    // Remove all edges with owner nodes which are weak.
    std::vector<Edge*>* owned_by_edges = child->owned_by_edges();
    auto new_end =
        std::remove_if(owned_by_edges->begin(), owned_by_edges->end(),
                       [](Edge* edge) { return edge->source()->is_weak(); });
    owned_by_edges->erase(new_end, owned_by_edges->end());

    ++child_it;
  }
}

// static
void GraphProcessor::AssignTracingOverhead(const std::string& allocator,
                                           GlobalNodeGraph* global_graph,
                                           Process* process) {
  // This method should only be called if the allocator node exists.
  PERFETTO_DCHECK(process->FindNode(allocator));

  // Check that the tracing node exists and isn't already owning another node.
  Node* tracing_node = process->FindNode("tracing");
  if (!tracing_node)
    return;

  // This should be first edge associated with the tracing node.
  PERFETTO_DCHECK(!tracing_node->owns_edge());

  // Create the node under the allocator to which tracing overhead can be
  // assigned.
  std::string child_name = allocator + "/allocated_objects/tracing_overhead";
  Node* child_node = process->CreateNode(MemoryAllocatorNodeId(), child_name,
                                         false /* weak */);

  // Assign the overhead of tracing to the tracing node.
  global_graph->AddNodeOwnershipEdge(tracing_node, child_node,
                                     0 /* importance */);
}

// static
Node::Entry GraphProcessor::AggregateNumericWithNameForNode(
    Node* node,
    const std::string& name) {
  bool first = true;
  Node::Entry::ScalarUnits units = Node::Entry::ScalarUnits::kObjects;
  uint64_t aggregated = 0;
  for (auto& path_to_child : *node->children()) {
    auto* entries = path_to_child.second->entries();

    // Retrieve the entry with the given column name.
    auto name_to_entry_it = entries->find(name);
    if (name_to_entry_it == entries->end())
      continue;

    // Extract the entry from the iterator.
    const Node::Entry& entry = name_to_entry_it->second;

    // Ensure that the entry is numeric.
    PERFETTO_DCHECK(entry.type == Node::Entry::Type::kUInt64);

    // Check that the units of every child's entry with the given name is the
    // same (i.e. we don't get a number for one child and size for another
    // child). We do this by having a DCHECK that the units match the first
    // child's units.
    PERFETTO_DCHECK(first || units == entry.units);
    units = entry.units;
    aggregated += entry.value_uint64;
    first = false;
  }
  return Node::Entry(units, aggregated);
}

// static
void GraphProcessor::AggregateNumericsRecursively(Node* node) {
  std::set<std::string> numeric_names;

  for (const auto& path_to_child : *node->children()) {
    AggregateNumericsRecursively(path_to_child.second);
    for (const auto& name_to_entry : *path_to_child.second->entries()) {
      const std::string& name = name_to_entry.first;
      if (name_to_entry.second.type == Node::Entry::Type::kUInt64 &&
          name != kSizeEntryName && name != kEffectiveSizeEntryName) {
        numeric_names.insert(name);
      }
    }
  }

  for (auto& name : numeric_names) {
    node->entries()->emplace(name, AggregateNumericWithNameForNode(node, name));
  }
}

// static
void GraphProcessor::PropagateNumericsAndDiagnosticsRecursively(Node* node) {
  for (const auto& name_to_entry : *node->entries()) {
    for (auto* edge : *node->owned_by_edges()) {
      edge->source()->entries()->insert(name_to_entry);
    }
  }
  for (const auto& path_to_child : *node->children()) {
    PropagateNumericsAndDiagnosticsRecursively(path_to_child.second);
  }
}

// static
std::optional<uint64_t> GraphProcessor::AggregateSizeForDescendantNode(
    Node* root,
    Node* descendant) {
  Edge* owns_edge = descendant->owns_edge();
  if (owns_edge && owns_edge->target()->IsDescendentOf(*root))
    return std::make_optional(0UL);

  if (descendant->children()->empty())
    return GetSizeEntryOfNode(descendant).value_or(0ul);

  std::optional<uint64_t> size;
  for (const auto& path_to_child : *descendant->children()) {
    auto c_size = AggregateSizeForDescendantNode(root, path_to_child.second);
    if (size) {
      *size += c_size.value_or(0);
    } else {
      size = std::move(c_size);
    }
  }
  return size;
}

// Assumes that this function has been called on all children and owner nodes.
// static
void GraphProcessor::CalculateSizeForNode(Node* node) {
  // Get the size at the root node if it exists.
  std::optional<uint64_t> node_size = GetSizeEntryOfNode(node);

  // Aggregate the size of all the child nodes.
  std::optional<uint64_t> aggregated_size;
  for (const auto& path_to_child : *node->children()) {
    auto c_size = AggregateSizeForDescendantNode(node, path_to_child.second);
    if (aggregated_size) {
      *aggregated_size += c_size.value_or(0ul);
    } else {
      aggregated_size = std::move(c_size);
    }
  }

  // Check that if both aggregated and node sizes exist that the node size
  // is bigger than the aggregated.
  // TODO(lalitm): the following condition is triggered very often even though
  // it is a warning in JS code. Find a way to add the warning to display in UI
  // or to fix all instances where this is violated and then enable this check.
  // PERFETTO_DCHECK(!node_size || !aggregated_size || *node_size >=
  // *aggregated_size);

  // Calculate the maximal size of an owner node.
  std::optional<uint64_t> max_owner_size;
  for (auto* edge : *node->owned_by_edges()) {
    auto o_size = GetSizeEntryOfNode(edge->source());
    if (max_owner_size) {
      *max_owner_size = std::max(o_size.value_or(0ul), *max_owner_size);
    } else {
      max_owner_size = std::move(o_size);
    }
  }

  // Check that if both owner and node sizes exist that the node size
  // is bigger than the owner.
  // TODO(lalitm): the following condition is triggered very often even though
  // it is a warning in JS code. Find a way to add the warning to display in UI
  // or to fix all instances where this is violated and then enable this check.
  // PERFETTO_DCHECK(!node_size || !max_owner_size || *node_size >=
  // *max_owner_size);

  // Clear out any existing size entry which may exist.
  node->entries()->erase(kSizeEntryName);

  // If no inference about size can be made then simply return.
  if (!node_size && !aggregated_size && !max_owner_size)
    return;

  // Update the node with the new size entry.
  uint64_t aggregated_size_value = aggregated_size.value_or(0ul);
  uint64_t process_size =
      std::max({node_size.value_or(0ul), aggregated_size_value,
                max_owner_size.value_or(0ul)});
  node->AddEntry(kSizeEntryName, Node::Entry::ScalarUnits::kBytes,
                 process_size);

  // If this is an intermediate node then add a ghost node which stores
  // all sizes not accounted for by the children.
  uint64_t unaccounted = process_size - aggregated_size_value;
  if (unaccounted > 0 && !node->children()->empty()) {
    Node* unspecified = node->CreateChild("<unspecified>");
    unspecified->AddEntry(kSizeEntryName, Node::Entry::ScalarUnits::kBytes,
                          unaccounted);
  }
}

// Assumes that this function has been called on all children and owner nodes.
// static
void GraphProcessor::CalculateNodeSubSizes(Node* node) {
  // Completely skip nodes with undefined size.
  std::optional<uint64_t> size_opt = GetSizeEntryOfNode(node);
  if (!size_opt)
    return;

  // If the node is a leaf node, then both sub-sizes are equal to the size.
  if (node->children()->empty()) {
    node->add_not_owning_sub_size(*size_opt);
    node->add_not_owned_sub_size(*size_opt);
    return;
  }

  // Calculate this node's not-owning sub-size by summing up the not-owning
  // sub-sizes of children which do not own another node.
  for (const auto& path_to_child : *node->children()) {
    if (path_to_child.second->owns_edge())
      continue;
    node->add_not_owning_sub_size(path_to_child.second->not_owning_sub_size());
  }

  // Calculate this node's not-owned sub-size.
  for (const auto& path_to_child : *node->children()) {
    Node* child = path_to_child.second;

    // If the child node is not owned, then add its not-owned sub-size.
    if (child->owned_by_edges()->empty()) {
      node->add_not_owned_sub_size(child->not_owned_sub_size());
      continue;
    }

    // If the child node is owned, then add the difference between its size
    // and the largest owner.
    uint64_t largest_owner_size = 0;
    for (Edge* edge : *child->owned_by_edges()) {
      uint64_t source_size = GetSizeEntryOfNode(edge->source()).value_or(0);
      largest_owner_size = std::max(largest_owner_size, source_size);
    }
    uint64_t child_size = GetSizeEntryOfNode(child).value_or(0);
    node->add_not_owned_sub_size(child_size - largest_owner_size);
  }
}

// static
void GraphProcessor::CalculateNodeOwnershipCoefficient(Node* node) {
  // Completely skip nodes with undefined size.
  std::optional<uint64_t> size_opt = GetSizeEntryOfNode(node);
  if (!size_opt)
    return;

  // We only need to consider owned nodes.
  if (node->owned_by_edges()->empty())
    return;

  // Sort the owners in decreasing order of ownership priority and
  // increasing order of not-owning sub-size (in case of equal priority).
  std::vector<Edge*> owners = *node->owned_by_edges();
  std::sort(owners.begin(), owners.end(), [](Edge* a, Edge* b) {
    if (a->priority() == b->priority()) {
      return a->source()->not_owning_sub_size() <
             b->source()->not_owning_sub_size();
    }
    return b->priority() < a->priority();
  });

  // Loop over the list of owners and distribute the owned node's not-owned
  // sub-size among them according to their ownership priority and
  // not-owning sub-size.
  uint64_t already_attributed_sub_size = 0;
  for (auto current_it = owners.begin(); current_it != owners.end();) {
    // Find the position of the first owner with lower priority.
    int current_priority = (*current_it)->priority();
    auto next_it =
        std::find_if(current_it, owners.end(), [current_priority](Edge* edge) {
          return edge->priority() < current_priority;
        });

    // Compute the number of nodes which have the same priority as current.
    size_t difference = static_cast<size_t>(std::distance(current_it, next_it));

    // Visit the owners with the same priority in increasing order of
    // not-owned sub-size, split the owned memory among them appropriately,
    // and calculate their owning coefficients.
    double attributed_not_owning_sub_size = 0;
    for (; current_it != next_it; current_it++) {
      uint64_t not_owning_sub_size =
          (*current_it)->source()->not_owning_sub_size();
      if (not_owning_sub_size > already_attributed_sub_size) {
        attributed_not_owning_sub_size +=
            static_cast<double>(not_owning_sub_size -
                                already_attributed_sub_size) /
            static_cast<double>(difference);
        already_attributed_sub_size = not_owning_sub_size;
      }

      if (not_owning_sub_size != 0) {
        double coeff = attributed_not_owning_sub_size /
                       static_cast<double>(not_owning_sub_size);
        (*current_it)->source()->set_owning_coefficient(coeff);
      }
      difference--;
    }

    // At the end of this loop, we should move to a node with a lower priority.
    PERFETTO_DCHECK(current_it == next_it);
  }

  // Attribute the remainder of the owned node's not-owned sub-size to
  // the node itself and calculate its owned coefficient.
  uint64_t not_owned_sub_size = node->not_owned_sub_size();
  if (not_owned_sub_size != 0) {
    double remainder_sub_size =
        static_cast<double>(not_owned_sub_size - already_attributed_sub_size);
    node->set_owned_coefficient(remainder_sub_size /
                                static_cast<double>(not_owned_sub_size));
  }
}

// static
void GraphProcessor::CalculateNodeCumulativeOwnershipCoefficient(Node* node) {
  // Completely skip nodes with undefined size.
  std::optional<uint64_t> size_opt = GetSizeEntryOfNode(node);
  if (!size_opt)
    return;

  double cumulative_owned_coefficient = node->owned_coefficient();
  if (node->parent()) {
    cumulative_owned_coefficient *=
        node->parent()->cumulative_owned_coefficient();
  }
  node->set_cumulative_owned_coefficient(cumulative_owned_coefficient);

  if (node->owns_edge()) {
    node->set_cumulative_owning_coefficient(
        node->owning_coefficient() *
        node->owns_edge()->target()->cumulative_owning_coefficient());
  } else if (node->parent()) {
    node->set_cumulative_owning_coefficient(
        node->parent()->cumulative_owning_coefficient());
  } else {
    node->set_cumulative_owning_coefficient(1);
  }
}

// static
void GraphProcessor::CalculateNodeEffectiveSize(Node* node) {
  // Completely skip nodes with undefined size. As a result, each node will
  // have defined effective size if and only if it has defined size.
  std::optional<uint64_t> size_opt = GetSizeEntryOfNode(node);
  if (!size_opt) {
    node->entries()->erase(kEffectiveSizeEntryName);
    return;
  }

  uint64_t effective_size = 0;
  if (node->children()->empty()) {
    // Leaf node.
    effective_size = static_cast<uint64_t>(
        static_cast<double>(*size_opt) * node->cumulative_owning_coefficient() *
        node->cumulative_owned_coefficient());
  } else {
    // Non-leaf node.
    for (const auto& path_to_child : *node->children()) {
      Node* child = path_to_child.second;
      if (!GetSizeEntryOfNode(child))
        continue;
      effective_size +=
          child->entries()->find(kEffectiveSizeEntryName)->second.value_uint64;
    }
  }
  node->AddEntry(kEffectiveSizeEntryName, Node::Entry::ScalarUnits::kBytes,
                 effective_size);
}

}  // namespace trace_processor
}  // namespace perfetto
