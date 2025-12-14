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

#include "src/trace_processor/importers/proto/memory_tracker_snapshot_parser.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/proc_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/graph.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/graph_processor.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/memory_allocator_node_id.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/memory_graph_edge.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/raw_memory_graph_node.h"
#include "perfetto/ext/trace_processor/importers/memory_tracker/raw_process_memory_node.h"
#include "protos/perfetto/trace/memory_graph.pbzero.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/memory_tables_py.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

MemoryTrackerSnapshotParser::MemoryTrackerSnapshotParser(
    TraceProcessorContext* context)
    : context_(context),
      level_of_detail_ids_{{context_->storage->InternString("background"),
                            context_->storage->InternString("light"),
                            context_->storage->InternString("detailed")}},
      unit_ids_{{context_->storage->InternString("objects"),
                 context_->storage->InternString("bytes")}},
      aggregate_raw_nodes_(),
      last_snapshot_timestamp_(-1),
      last_snapshot_level_of_detail_(LevelOfDetail::kFirst) {}

void MemoryTrackerSnapshotParser::ParseMemoryTrackerSnapshot(int64_t ts,
                                                             ConstBytes blob) {
  PERFETTO_DCHECK(last_snapshot_timestamp_ <= ts);
  if (!aggregate_raw_nodes_.empty() && ts != last_snapshot_timestamp_) {
    GenerateGraphFromRawNodesAndEmitRows();
  }
  ReadProtoSnapshot(blob, aggregate_raw_nodes_, last_snapshot_level_of_detail_);
  last_snapshot_timestamp_ = ts;
}

void MemoryTrackerSnapshotParser::NotifyEndOfFile() {
  if (!aggregate_raw_nodes_.empty()) {
    GenerateGraphFromRawNodesAndEmitRows();
  }
}

void MemoryTrackerSnapshotParser::ReadProtoSnapshot(
    ConstBytes blob,
    RawMemoryNodeMap& raw_nodes,
    LevelOfDetail& level_of_detail) {
  protos::pbzero::MemoryTrackerSnapshot::Decoder snapshot(blob);
  level_of_detail = LevelOfDetail::kDetailed;

  switch (snapshot.level_of_detail()) {
    case 0:  // FULL
      level_of_detail = LevelOfDetail::kDetailed;
      break;
    case 1:  // LIGHT
      level_of_detail = LevelOfDetail::kLight;
      break;
    case 2:  // BACKGROUND
      level_of_detail = LevelOfDetail::kBackground;
      break;
  }

  for (auto process_it = snapshot.process_memory_dumps(); process_it;
       ++process_it) {
    protos::pbzero::MemoryTrackerSnapshot::ProcessSnapshot::Decoder
        process_memory_dump(*process_it);

    auto pid = static_cast<base::PlatformProcessId>(process_memory_dump.pid());

    RawProcessMemoryNode::MemoryNodesMap nodes_map;
    RawProcessMemoryNode::AllocatorNodeEdgesMap edges_map;

    for (auto node_it = process_memory_dump.allocator_dumps(); node_it;
         ++node_it) {
      protos::pbzero::MemoryTrackerSnapshot::ProcessSnapshot::MemoryNode::
          Decoder node(*node_it);

      MemoryAllocatorNodeId node_id(node.id());
      const std::string absolute_name = node.absolute_name().ToStdString();
      int flags;
      if (node.weak()) {
        flags = RawMemoryGraphNode::kWeak;
      } else {
        flags = RawMemoryGraphNode::kDefault;
      }

      std::vector<RawMemoryGraphNode::MemoryNodeEntry> entries;

      if (node.has_size_bytes()) {
        entries.emplace_back("size", RawMemoryGraphNode::kUnitsBytes,
                             node.size_bytes());
      }

      for (auto entry_it = node.entries(); entry_it; ++entry_it) {
        protos::pbzero::MemoryTrackerSnapshot::ProcessSnapshot::MemoryNode::
            MemoryNodeEntry::Decoder entry(*entry_it);

        std::string unit;

        switch (entry.units()) {
          case 1:  // BYTES
            unit = RawMemoryGraphNode::kUnitsBytes;
            break;
          case 2:  // COUNT
            unit = RawMemoryGraphNode::kUnitsObjects;
            break;
        }
        if (entry.has_value_uint64()) {
          entries.emplace_back(entry.name().ToStdString(), unit,
                               entry.value_uint64());
        } else if (entry.has_value_string()) {
          entries.emplace_back(entry.name().ToStdString(), unit,
                               entry.value_string().ToStdString());
        } else {
          context_->storage->IncrementStats(
              stats::memory_snapshot_parser_failure);
        }
      }
      std::unique_ptr<RawMemoryGraphNode> raw_graph_node(new RawMemoryGraphNode(
          absolute_name, level_of_detail, node_id, std::move(entries)));
      raw_graph_node->set_flags(flags);
      nodes_map.insert(
          std::make_pair(absolute_name, std::move(raw_graph_node)));
    }

    for (auto edge_it = process_memory_dump.memory_edges(); edge_it;
         ++edge_it) {
      protos::pbzero::MemoryTrackerSnapshot::ProcessSnapshot::MemoryEdge::
          Decoder edge(*edge_it);

      std::unique_ptr<MemoryGraphEdge> graph_edge(new MemoryGraphEdge(
          MemoryAllocatorNodeId(edge.source_id()),
          MemoryAllocatorNodeId(edge.target_id()),
          static_cast<int>(edge.importance()), edge.overridable()));

      edges_map.insert(std::make_pair(MemoryAllocatorNodeId(edge.source_id()),
                                      std::move(graph_edge)));
    }
    std::unique_ptr<RawProcessMemoryNode> raw_node(new RawProcessMemoryNode(
        level_of_detail, std::move(edges_map), std::move(nodes_map)));
    raw_nodes.insert(std::make_pair(pid, std::move(raw_node)));
  }
}

std::unique_ptr<GlobalNodeGraph> MemoryTrackerSnapshotParser::GenerateGraph(
    RawMemoryNodeMap& raw_nodes) {
  auto graph = GraphProcessor::CreateMemoryGraph(raw_nodes);
  GraphProcessor::CalculateSizesForGraph(graph.get());
  return graph;
}

void MemoryTrackerSnapshotParser::EmitRows(int64_t ts,
                                           GlobalNodeGraph& graph,
                                           LevelOfDetail level_of_detail) {
  IdNodeMap id_node_map;

  // For now, we use the existing global instant event track for chrome events,
  // since memory dumps are global.
  TrackId track_id = context_->track_tracker->InternTrack(
      tracks::kLegacyGlobalInstantsBlueprint, tracks::Dimensions(),
      tracks::BlueprintName(), [this](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(
            context_->storage->InternString("source"),
            Variadic::String(context_->storage->InternString("chrome")));
      });

  tables::MemorySnapshotTable::Row snapshot_row(
      ts, track_id, level_of_detail_ids_[static_cast<size_t>(level_of_detail)]);
  tables::MemorySnapshotTable::Id snapshot_row_id =
      context_->storage->mutable_memory_snapshot_table()
          ->Insert(snapshot_row)
          .id;

  for (auto const& it_process : graph.process_node_graphs()) {
    tables::ProcessMemorySnapshotTable::Row process_row;
    process_row.upid = context_->process_tracker->GetOrCreateProcess(
        static_cast<uint32_t>(it_process.first));
    process_row.snapshot_id = snapshot_row_id;
    tables::ProcessMemorySnapshotTable::Id proc_snapshot_row_id =
        context_->storage->mutable_process_memory_snapshot_table()
            ->Insert(process_row)
            .id;
    EmitMemorySnapshotNodeRows(*(it_process.second->root()),
                               proc_snapshot_row_id, id_node_map);
  }

  // For each snapshot nodes from shared_memory_graph will be associated
  // with a fabricated process_memory_snapshot entry whose pid == 0.
  // TODO(mobica-google-contributors@mobica.com): Track the shared memory graph
  // in a separate table.
  tables::ProcessMemorySnapshotTable::Row fake_process_row;
  fake_process_row.upid = context_->process_tracker->GetOrCreateProcess(0u);
  fake_process_row.snapshot_id = snapshot_row_id;
  tables::ProcessMemorySnapshotTable::Id fake_proc_snapshot_row_id =
      context_->storage->mutable_process_memory_snapshot_table()
          ->Insert(fake_process_row)
          .id;
  EmitMemorySnapshotNodeRows(*(graph.shared_memory_graph()->root()),
                             fake_proc_snapshot_row_id, id_node_map);

  for (const auto& edge : graph.edges()) {
    tables::MemorySnapshotEdgeTable::Row edge_row;
    auto source_it = id_node_map.find(edge.source()->id());
    if (source_it == id_node_map.end())
      continue;
    edge_row.source_node_id =
        static_cast<tables::MemorySnapshotNodeTable::Id>(source_it->second);
    auto target_it = id_node_map.find(edge.target()->id());
    if (target_it == id_node_map.end())
      continue;
    edge_row.target_node_id =
        static_cast<tables::MemorySnapshotNodeTable::Id>(target_it->second);
    edge_row.importance = static_cast<uint32_t>(edge.priority());
    context_->storage->mutable_memory_snapshot_edge_table()->Insert(edge_row);
  }
}

void MemoryTrackerSnapshotParser::EmitMemorySnapshotNodeRows(
    GlobalNodeGraph::Node& root_node_graph,
    ProcessMemorySnapshotId& proc_snapshot_row_id,
    IdNodeMap& id_node_map) {
  EmitMemorySnapshotNodeRowsRecursively(root_node_graph, std::string(),
                                        std::nullopt, proc_snapshot_row_id,
                                        id_node_map);
}

void MemoryTrackerSnapshotParser::EmitMemorySnapshotNodeRowsRecursively(
    GlobalNodeGraph::Node& node,
    const std::string& path,
    std::optional<tables::MemorySnapshotNodeTable::Id> parent_node_row_id,
    ProcessMemorySnapshotId& proc_snapshot_row_id,
    IdNodeMap& id_node_map) {
  std::optional<tables::MemorySnapshotNodeTable::Id> node_id;
  // Skip emitting the root node into the tables - it is not a real node.
  if (!path.empty()) {
    node_id = EmitNode(node, path, parent_node_row_id, proc_snapshot_row_id,
                       id_node_map);
  }

  for (const auto& name_and_child : *node.children()) {
    std::string child_path = path;
    if (!child_path.empty())
      child_path += "/";
    child_path += name_and_child.first;

    EmitMemorySnapshotNodeRowsRecursively(*(name_and_child.second), child_path,
                                          /*parent_node_row_id=*/node_id,
                                          proc_snapshot_row_id, id_node_map);
  }
}

std::optional<tables::MemorySnapshotNodeTable::Id>
MemoryTrackerSnapshotParser::EmitNode(
    const GlobalNodeGraph::Node& node,
    const std::string& path,
    std::optional<tables::MemorySnapshotNodeTable::Id> parent_node_row_id,
    ProcessMemorySnapshotId& proc_snapshot_row_id,
    IdNodeMap& id_node_map) {
  tables::MemorySnapshotNodeTable::Row node_row;
  node_row.process_snapshot_id = proc_snapshot_row_id;
  node_row.parent_node_id = parent_node_row_id;
  node_row.path = context_->storage->InternString(base::StringView(path));

  tables::MemorySnapshotNodeTable::Id node_row_id =
      context_->storage->mutable_memory_snapshot_node_table()
          ->Insert(node_row)
          .id;

  auto* node_table = context_->storage->mutable_memory_snapshot_node_table();
  auto rr = *node_table->FindById(node_row_id);
  ArgsTracker args_tracker(context_);
  ArgsTracker::BoundInserter args = args_tracker.AddArgsTo(node_row_id);

  for (const auto& entry : node.const_entries()) {
    switch (entry.second.type) {
      case GlobalNodeGraph::Node::Entry::Type::kUInt64: {
        int64_t value_int = static_cast<int64_t>(entry.second.value_uint64);

        if (entry.first == "size") {
          rr.set_size(value_int);
        } else if (entry.first == "effective_size") {
          rr.set_effective_size(value_int);
        } else {
          args.AddArg(context_->storage->InternString(
                          base::StringView(entry.first + ".value")),
                      Variadic::Integer(value_int));
          if (entry.second.units < unit_ids_.size()) {
            args.AddArg(context_->storage->InternString(
                            base::StringView(entry.first + ".unit")),
                        Variadic::String(unit_ids_[entry.second.units]));
          }
        }
        break;
      }
      case GlobalNodeGraph::Node::Entry::Type::kString: {
        args.AddArg(context_->storage->InternString(
                        base::StringView(entry.first + ".value")),
                    Variadic::String(context_->storage->InternString(
                        base::StringView(entry.second.value_string))));
        break;
      }
    }
  }
  id_node_map.emplace(std::make_pair(node.id(), node_row_id));
  return node_row_id;
}

void MemoryTrackerSnapshotParser::GenerateGraphFromRawNodesAndEmitRows() {
  std::unique_ptr<GlobalNodeGraph> graph = GenerateGraph(aggregate_raw_nodes_);
  EmitRows(last_snapshot_timestamp_, *graph, last_snapshot_level_of_detail_);
  aggregate_raw_nodes_.clear();
}

}  // namespace perfetto::trace_processor
