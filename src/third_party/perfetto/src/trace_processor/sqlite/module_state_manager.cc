/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/sqlite/module_state_manager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"

namespace perfetto::trace_processor::sqlite {

void ModuleStateManagerBase::OnCommit() {
  std::vector<std::string> to_erase;
  for (auto it = state_by_name_.GetIterator(); it; ++it) {
    if (auto& state = *it.value(); state.active_state) {
      committed_store_->Store(it.key(), state.active_state);
      state.savepoint_states.clear();
    } else {
      committed_store_->Erase(it.key());
      to_erase.push_back(it.key());
    }
  }
  for (const auto& name : to_erase) {
    state_by_name_.Erase(name);
  }
}

void ModuleStateManagerBase::OnRollback() {
  std::vector<std::string> to_erase;
  for (auto it = state_by_name_.GetIterator(); it; ++it) {
    if (auto committed = committed_store_->Load(it.key()); committed) {
      auto& state = *it.value();
      state.active_state = std::move(committed);
      state.savepoint_states.clear();
    } else {
      to_erase.push_back(it.key());
    }
  }
  for (const auto& name : to_erase) {
    state_by_name_.Erase(name);
  }
}

ModuleStateManagerBase::PerVtabState* ModuleStateManagerBase::OnCreate(
    int,
    const char* const* argv,
    std::unique_ptr<void, void (*)(void*)> state) {
  auto [it, inserted] = state_by_name_.Insert(argv[2], nullptr);
  if (inserted) {
    *it = std::make_unique<PerVtabState>();
    auto* s_ptr = it->get();
    s_ptr->manager = this;
    s_ptr->name = argv[2];
  }
  auto* s_ptr = it->get();
  PERFETTO_CHECK(!s_ptr->active_state);
  s_ptr->active_state = std::move(state);
  return s_ptr;
}

ModuleStateManagerBase::PerVtabState* ModuleStateManagerBase::OnConnect(
    int,
    const char* const* argv) {
  if (auto* ptr = state_by_name_.Find(argv[2]); ptr) {
    return ptr->get();
  }
  // Cold attach: a peer connection sharing the same |CommittedStateManager|
  // may have committed state for this vtab. If so, materialise a local
  // |PerVtabState| pointing at the committed state.
  auto committed = committed_store_->Load(argv[2]);
  if (!committed) {
    return nullptr;
  }
  auto [it, inserted] = state_by_name_.Insert(argv[2], nullptr);
  PERFETTO_CHECK(inserted);
  *it = std::make_unique<PerVtabState>();
  auto* s_ptr = it->get();
  s_ptr->manager = this;
  s_ptr->name = argv[2];
  s_ptr->active_state = std::move(committed);
  return s_ptr;
}

void ModuleStateManagerBase::OnDestroy(PerVtabState* state) {
  auto* ptr = state->manager->state_by_name_.Find(state->name);
  PERFETTO_CHECK(ptr);
  PERFETTO_CHECK(ptr->get() == state);
  state->active_state.reset();
}

void ModuleStateManagerBase::OnSavepoint(PerVtabState* s, int idx) {
  auto new_size = static_cast<uint32_t>(idx + 1);
  std::shared_ptr<void> fill = s->savepoint_states.empty()
                                   ? s->manager->committed_store_->Load(s->name)
                                   : s->savepoint_states.back();
  s->savepoint_states.resize(new_size, std::move(fill));
  s->savepoint_states[new_size - 1] = s->active_state;
}

void ModuleStateManagerBase::OnRelease(PerVtabState* s, int idx) {
  auto release_idx = static_cast<uint32_t>(idx);
  PERFETTO_CHECK(release_idx <= s->savepoint_states.size());
  s->savepoint_states.resize(release_idx);
}

void ModuleStateManagerBase::OnRollbackTo(PerVtabState* s, int idx) {
  auto new_size = static_cast<uint32_t>(idx + 1);
  PERFETTO_CHECK(new_size <= s->savepoint_states.size());
  s->active_state = new_size == 0 ? s->manager->committed_store_->Load(s->name)
                                  : s->savepoint_states[new_size - 1];
  s->savepoint_states.resize(new_size);
}

void* ModuleStateManagerBase::GetState(PerVtabState* s) {
  return s->active_state.get();
}

void* ModuleStateManagerBase::GetStateByName(const std::string& name) {
  auto* ptr = state_by_name_.Find(name);
  if (!ptr) {
    return nullptr;
  }
  return GetState(ptr->get());
}

}  // namespace perfetto::trace_processor::sqlite
