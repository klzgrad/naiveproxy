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

#ifndef SRC_TRACE_PROCESSOR_CORE_PLUGIN_PLUGIN_H_
#define SRC_TRACE_PROCESSOR_CORE_PLUGIN_PLUGIN_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/trace_processor/core/util/type_set.h"

struct sqlite3_module;

namespace perfetto::trace_processor::core::dataframe {
class Dataframe;
}  // namespace perfetto::trace_processor::core::dataframe

namespace perfetto::trace_processor {

class StaticTableFunction;
class TraceProcessorContext;
class TraceReaderRegistry;
class TraceStorage;
struct ProtoImporterModuleContext;

// Lightweight struct for plugin dataframe registration.
struct PluginDataframe {
  core::dataframe::Dataframe* dataframe;
  std::string name;
};

namespace sqlite {
class ModuleStateManagerBase;
}  // namespace sqlite

// Registration entry for a sqlite virtual table module.
struct SqliteModuleRegistration {
  using Destructor = void (*)(void*);

  std::string name;
  const sqlite3_module* module = nullptr;
  void* context = nullptr;
  Destructor destructor = nullptr;
  bool is_state_manager = false;
};

// Helper to create a SqliteModuleRegistration with a non-owning context.
template <typename Module>
SqliteModuleRegistration MakeSqliteModule(std::string name,
                                          typename Module::Context* ctx) {
  SqliteModuleRegistration reg;
  reg.name = std::move(name);
  reg.module = &Module::kModule;
  reg.context = ctx;
  reg.is_state_manager = std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                           typename Module::Context>;
  return reg;
}

// Helper to create a SqliteModuleRegistration with an owning context.
template <typename Module>
SqliteModuleRegistration MakeSqliteModule(
    std::string name,
    std::unique_ptr<typename Module::Context> ctx) {
  SqliteModuleRegistration reg;
  reg.name = std::move(name);
  reg.module = &Module::kModule;
  reg.context = ctx.release();
  reg.destructor = [](void* p) {
    delete static_cast<typename Module::Context*>(p);
  };
  reg.is_state_manager = std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                           typename Module::Context>;
  return reg;
}

// Compile-time tag for plugin identity. Each Plugin subclass gets a unique
// runtime ID via the address of a per-instantiation static member.
template <typename T>
struct PluginTag {
  static constexpr char kTag = 0;
  static constexpr const void* Id() { return &kTag; }
};

// Non-templated base class for plugins.
//
// The plugin IS the context: it carries its resolved dependency pointers and
// a back-pointer to TraceProcessorContext. The framework sets these fields
// after construction; lifecycle methods access them via `this`.
//
// How the plugin organizes its internal state is an implementation detail.
// Subclasses should not override the PluginBase virtuals directly; instead,
// inherit from Plugin<> which provides identity and typed dep access.
class PluginBase {
 public:
  virtual ~PluginBase();

  // Identity and dependency accessors for the framework.
  virtual const void* GetPluginId() const = 0;
  virtual const void* const* GetDepIds() const = 0;
  virtual size_t GetDepCount() const = 0;

  virtual void RegisterImporters(TraceReaderRegistry& registry);
  virtual void RegisterProtoImporterModules(
      ProtoImporterModuleContext* module_context);
  virtual void RegisterDataframes(std::vector<PluginDataframe>& tables);
  virtual void RegisterStaticTableFunctions(
      std::vector<std::unique_ptr<StaticTableFunction>>& fns);
  virtual void RegisterSqliteModules(
      std::vector<SqliteModuleRegistration>& modules);

  virtual uint64_t GetBoundsMutationCount();
  virtual std::pair<int64_t, int64_t> GetTimestampBounds();

  // --- Framework-managed fields. Set by TraceProcessorImpl after creation. ---
  TraceProcessorContext* trace_context_ = nullptr;
  std::vector<PluginBase*> resolved_deps_;
};

// Templated subclass that provides identity, dependency tracking, typed
// dependency access, and typed factory wrappers for lifecycle methods.
//
// Self: CRTP parameter (the concrete plugin class).
// Deps: prerequisite plugin classes. Declares the dependency graph for
//       topological sorting. Access deps at runtime via dependency<T>().
//
// The template parameters serve three purposes:
// 1. Compile-time: forces #include of dep headers -> forces GN dep
// 2. Link-time: if dep's .cc isn't compiled, GN dep missing -> build fails
// 3. Runtime: topological sort verifies all deps are registered
template <typename Self, typename... Deps>
class Plugin : public PluginBase {
 public:
  static constexpr const void* kPluginId = PluginTag<Self>::Id();
  static constexpr std::array<const void*, sizeof...(Deps)> kDepIds = {
      PluginTag<Deps>::Id()...};

  const void* GetPluginId() const final { return kPluginId; }
  const void* const* GetDepIds() const final { return kDepIds.data(); }
  size_t GetDepCount() const final { return kDepIds.size(); }

  // Returns a dependency plugin by type. T must be one of the Deps.
  // The index is resolved at compile time from the parameter pack position.
  template <typename T>
  T* dependency() {
    return static_cast<T*>(
        resolved_deps_[core::TypeSet<Deps...>::template GetTypeIndex<T>()]);
  }
};

// Registration entry in a global intrusive linked list.
struct PluginRegistration {
  using Factory = std::unique_ptr<PluginBase> (*)();

  PluginRegistration* next;
  Factory factory;
  const void* plugin_id;
  const void* const* dep_ids;
  size_t dep_count;

  PluginRegistration(Factory f,
                     const void* id,
                     const void* const* deps,
                     size_t n_deps);
};

// The result of topological sorting: plugin factories in dependency order
// with pre-resolved dep indices. Computed once (statically) and shared by
// all TraceProcessorImpl instances.
struct PluginSet {
  struct Entry {
    PluginRegistration::Factory factory;
    // Indices into the PluginSet::entries vector for this plugin's deps.
    std::vector<size_t> dep_indices;
  };
  std::vector<Entry> entries;
};

// Returns the singleton PluginSet. The topological sort and dep resolution
// happen once on first call; subsequent calls return the cached result.
const PluginSet& GetPluginSet();

// Suppresses -Wglobal-constructors which is Clang-specific. GCC and MSVC
// do not have this warning so the macros are no-ops on those compilers.
#ifdef __clang__
#define PERFETTO_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER \
  _Pragma("clang diagnostic push")                         \
      _Pragma("clang diagnostic ignored \"-Wglobal-constructors\"")
#define PERFETTO_END_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER \
  _Pragma("clang diagnostic pop")
#else
#define PERFETTO_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER
#define PERFETTO_END_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER
#endif

// Trailing `static_assert(true, "")` consumes the macro call site's `;`, so
// `PERFETTO_TP_REGISTER_PLUGIN(Foo);` reads like a normal statement and won't
// trigger -Wpedantic / -Wextra-semi on compilers that flag stray semicolons.
#define PERFETTO_TP_REGISTER_PLUGIN(ClassName)                                \
  PERFETTO_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER                          \
  static ::perfetto::trace_processor::PluginRegistration g_##ClassName##_reg( \
      []() -> std::unique_ptr<::perfetto::trace_processor::PluginBase> {      \
        return std::make_unique<ClassName>();                                 \
      },                                                                      \
      ClassName::kPluginId, ClassName::kDepIds.data(),                        \
      ClassName::kDepIds.size());                                             \
  PERFETTO_END_ALLOW_GLOBAL_CTORS_FOR_TP_PLUGIN_REGISTER                      \
  static_assert(true, "")

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_PLUGIN_PLUGIN_H_
