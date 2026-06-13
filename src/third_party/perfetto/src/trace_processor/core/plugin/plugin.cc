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

#include "src/trace_processor/core/plugin/plugin.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/no_destructor.h"

namespace perfetto::trace_processor {

namespace {

// Global linked list head for plugin registrations.
PluginRegistration* g_plugin_head = nullptr;

// Topologically sorts registrations and pre-resolves dependency indices.
// Called once and cached.
PluginSet BuildPluginSet() {
  // Collect all registrations.
  std::vector<PluginRegistration*> regs;
  for (auto* r = g_plugin_head; r; r = r->next) {
    regs.push_back(r);
  }

  // Build a map from plugin_id -> index in |regs|.
  base::FlatHashMap<const void*, size_t> id_to_idx;
  for (size_t i = 0; i < regs.size(); ++i) {
    id_to_idx[regs[i]->plugin_id] = i;
  }

  // Verify all dependencies exist.
  for (auto* r : regs) {
    for (size_t d = 0; d < r->dep_count; ++d) {
      PERFETTO_CHECK(id_to_idx.Find(r->dep_ids[d]) != nullptr);
    }
  }

  // Kahn's algorithm for topological sort.
  size_t n = regs.size();
  std::vector<size_t> in_degree(n, 0);
  std::vector<std::vector<size_t>> dependents(n);
  for (size_t i = 0; i < n; ++i) {
    for (size_t d = 0; d < regs[i]->dep_count; ++d) {
      size_t dep_idx = *id_to_idx.Find(regs[i]->dep_ids[d]);
      dependents[dep_idx].push_back(i);
      in_degree[i]++;
    }
  }

  std::vector<size_t> queue;
  for (size_t i = 0; i < n; ++i) {
    if (in_degree[i] == 0) {
      queue.push_back(i);
    }
  }

  std::vector<size_t> sorted_order;
  sorted_order.reserve(n);
  while (!queue.empty()) {
    size_t idx = queue.back();
    queue.pop_back();
    sorted_order.push_back(idx);
    for (size_t dep : dependents[idx]) {
      if (--in_degree[dep] == 0) {
        queue.push_back(dep);
      }
    }
  }
  PERFETTO_CHECK(sorted_order.size() == n);

  // Build a map from original reg index -> position in sorted output,
  // so we can resolve dep indices into the final array.
  base::FlatHashMap<const void*, size_t> id_to_sorted_idx;
  for (size_t i = 0; i < sorted_order.size(); ++i) {
    id_to_sorted_idx[regs[sorted_order[i]]->plugin_id] = i;
  }

  // Build PluginSet with pre-resolved dep indices.
  PluginSet result;
  result.entries.reserve(n);
  for (size_t orig_idx : sorted_order) {
    PluginSet::Entry entry;
    entry.factory = regs[orig_idx]->factory;
    for (size_t d = 0; d < regs[orig_idx]->dep_count; ++d) {
      size_t* dep_sorted = id_to_sorted_idx.Find(regs[orig_idx]->dep_ids[d]);
      PERFETTO_CHECK(dep_sorted != nullptr);
      entry.dep_indices.push_back(*dep_sorted);
    }
    result.entries.push_back(std::move(entry));
  }
  return result;
}

}  // namespace

PluginRegistration::PluginRegistration(Factory f,
                                       const void* id,
                                       const void* const* deps,
                                       size_t n_deps)
    : next(g_plugin_head),
      factory(f),
      plugin_id(id),
      dep_ids(deps),
      dep_count(n_deps) {
  g_plugin_head = this;
}

// Default no-op implementations.
PluginBase::~PluginBase() = default;
void PluginBase::RegisterImporters(TraceReaderRegistry&) {}
void PluginBase::RegisterProtoImporterModules(ProtoImporterModuleContext*) {}
void PluginBase::RegisterDataframes(std::vector<PluginDataframe>&) {}
void PluginBase::RegisterStaticTableFunctions(
    std::vector<std::unique_ptr<StaticTableFunction>>&) {}
void PluginBase::RegisterSqliteModules(std::vector<SqliteModuleRegistration>&) {
}
uint64_t PluginBase::GetBoundsMutationCount() {
  return 0;
}
std::pair<int64_t, int64_t> PluginBase::GetTimestampBounds() {
  return {std::numeric_limits<int64_t>::max(), 0};
}

const PluginSet& GetPluginSet() {
  static base::NoDestructor<PluginSet> set(BuildPluginSet());
  return set.ref();
}

}  // namespace perfetto::trace_processor
