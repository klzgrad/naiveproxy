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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_MODULE_STATE_MANAGER_H_
#define SRC_TRACE_PROCESSOR_SQLITE_MODULE_STATE_MANAGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"

namespace perfetto::trace_processor::sqlite {

// Base class for ModuleStateManager. Used to reduce the binary size of
// ModuleStateManager and also provide a type-erased interface for the
// engines to hold onto (e.g. to call OnCommit, OnRollback, etc).
class ModuleStateManagerBase {
 public:
  // Per-vtab state. The pointer to this class should be stored in the Vtab.
  struct PerVtabState {
   private:
    // The below fields should only be accessed by the manager, use GetState to
    // access the state from outside this class.
    friend class ModuleStateManagerBase;

    // The "current" state of the vtab. This can be the same as the committed
    // state or can be a different state (if the state has changed since the
    // most recent commit) or null (indicating that the vtab has been dropped).
    std::shared_ptr<void> active_state;

    // The state of the vtab which has been "committed" by SQLite.
    std::shared_ptr<void> committed_state;

    // All the "saved" states of the vtab. This function will be modified by
    // savepoint/rollback to/release callbacks from SQLite.
    std::vector<std::shared_ptr<void>> savepoint_states;

    // The name of the vtab.
    std::string name;

    // A pointer to the manager object. Backreference for use by static
    // functions in this class.
    ModuleStateManagerBase* manager;
  };

  // Called by the engine when a transaction is committed.
  //
  // This is used to finalize all the destroys performed since a previous
  // rollback or commit.
  void OnCommit();

  // Called by the engine when a transaction is rolled back.
  //
  // This is used to undo the effects of all the destroys performed since a
  // previous rollback or commit.
  void OnRollback();

 protected:
  // Enforce that anyone who wants to use this class inherits from it.
  ModuleStateManagerBase() = default;

  // Type-erased counterparts of ModuleStateManager functions. See below for
  // documentation of these functions.
  [[nodiscard]] PerVtabState* OnCreate(
      int argc,
      const char* const* argv,
      std::unique_ptr<void, void (*)(void*)> state);
  [[nodiscard]] PerVtabState* OnConnect(int argc, const char* const* argv);
  static void OnDestroy(PerVtabState* state);

  static void OnSavepoint(PerVtabState* state, int);
  static void OnRelease(PerVtabState* state, int);
  static void OnRollbackTo(PerVtabState* state, int);

  static void* GetState(PerVtabState* s);
  void* GetStateByName(const std::string& name);

  using StateMap =
      base::FlatHashMap<std::string, std::unique_ptr<PerVtabState>>;

  StateMap state_by_name_;
};

// Helper class which abstracts away management of per-vtab state of an SQLite
// virtual table module.
//
// SQLite has some subtle semantics around lifecycle of vtabs which makes state
// management complex. This class attempts to encapsulate some of that
// complexity as a central place where we can document the quirks.
//
// Usage of this class:
// struct MyModule : sqlite::Module<MyModule> {
//   // Make the context object inherit from ModuleStateManager.
//   struct Context : ModuleStateManager<MyModule> {
//     ... (other fields)
//   }
//   struct Vtab : sqlite::Module<MyModule>::Vtab {
//     // Store the per-vtab-state pointer in the vtab object.
//     ModuleStateManager<MyModule>::PerVtabState* state;
//     ... (other fields)
//   }
//   static void OnCreate(...) {
//     ...
//     // Call OnCreate on the manager object and store the returned pointer.
//     tab->state = ctx->OnCreate(argv);
//     ...
//   }
//   static void OnDestroy(...) {
//     ...
//     // Call OnDestroy with the stored state pointer.
//     sqlite::ModuleStateManager<MyModule>::OnDestroy(tab->state);
//     ...
//   }
//   // Do the same in OnConnect as in OnCreate.
//   static void OnConnect(...)
// }
template <typename Module>
class ModuleStateManager : public ModuleStateManagerBase {
 public:
  // Lifecycle method to be called from Module::Create.
  [[nodiscard]] PerVtabState* OnCreate(
      int argc,
      const char* const* argv,
      std::unique_ptr<typename Module::State> state) {
    std::unique_ptr<void, void (*)(void*)> erased_state =
        std::unique_ptr<void, void (*)(void*)>(state.release(), [](void* ptr) {
          delete static_cast<typename Module::State*>(ptr);
        });
    return ModuleStateManagerBase::OnCreate(argc, argv,
                                            std::move(erased_state));
  }

  // Lifecycle method to be called from Module::Connect.
  [[nodiscard]] PerVtabState* OnConnect(int argc, const char* const* argv) {
    return ModuleStateManagerBase::OnConnect(argc, argv);
  }

  // Lifecycle method to be called from Module::Destroy.
  static void OnDestroy(PerVtabState* state) {
    ModuleStateManagerBase::OnDestroy(state);
  }

  // Lifecycle method to be called from Module::Savepoint.
  static void OnSavepoint(PerVtabState* state, int idx) {
    ModuleStateManagerBase::OnSavepoint(state, idx);
  }

  // Lifecycle method to be called from Module::Release.
  static void OnRelease(PerVtabState* state, int idx) {
    ModuleStateManagerBase::OnRelease(state, idx);
  }

  // Lifecycle method to be called from Module::RollbackTo.
  static void OnRollbackTo(PerVtabState* state, int idx) {
    ModuleStateManagerBase::OnRollbackTo(state, idx);
  }

  // Method to be called from module callbacks to extract the module state
  // from the manager state.
  static typename Module::State* GetState(PerVtabState* s) {
    return static_cast<typename Module::State*>(
        ModuleStateManagerBase::GetState(s));
  }

  // Looks up the state of a module by name.
  //
  // This function should only be called for speculative lookups from outside
  // the module implementation: use `GetState` inside the sqlite::Module
  // implementation.
  typename Module::State* GetStateByName(const std::string& name) {
    return static_cast<typename Module::State*>(
        ModuleStateManagerBase::GetStateByName(name));
  }

  // Returns all the states managed by this manager.
  //
  // This function should only be called for speculative lookups from outside
  // the module implementation: use `GetState` inside the sqlite::Module
  // implementation.
  std::vector<std::pair<std::string, typename Module::State*>> GetAllStates() {
    std::vector<std::pair<std::string, typename Module::State*>> states;
    states.reserve(state_by_name_.size());
    for (auto it = state_by_name_.GetIterator(); it; ++it) {
      if (auto* state = GetState(it.value().get()); state) {
        states.emplace_back(it.key(), state);
      }
    }
    return states;
  }

 protected:
  // Enforce that anyone who wants to use this class inherits from it.
  ModuleStateManager() = default;
};

}  // namespace perfetto::trace_processor::sqlite

#endif  // SRC_TRACE_PROCESSOR_SQLITE_MODULE_STATE_MANAGER_H_
