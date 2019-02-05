// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/module_cache.h"

#include "base/no_destructor.h"

namespace base {

ModuleCache::Module::Module() : is_valid(false) {}

ModuleCache::Module::Module(uintptr_t base_address,
                            const std::string& id,
                            const FilePath& filename)
    : Module(base_address, id, filename, 0) {}

ModuleCache::Module::Module(uintptr_t base_address,
                            const std::string& id,
                            const FilePath& filename,
                            size_t size)
    : base_address(base_address),
      id(id),
      filename(filename),
      is_valid(true),
      size(size) {}

ModuleCache::Module::~Module() = default;

ModuleCache::ModuleCache() = default;
ModuleCache::~ModuleCache() = default;

const ModuleCache::Module& ModuleCache::GetModuleForAddress(uintptr_t address) {
  static NoDestructor<Module> invalid_module;
  auto it = modules_cache_map_.upper_bound(address);
  if (it != modules_cache_map_.begin()) {
    DCHECK(!modules_cache_map_.empty());
    --it;
    Module& module = it->second;
    if (address < module.base_address + module.size)
      return module;
  }

  auto module = CreateModuleForAddress(address);
  if (!module.is_valid)
    return *invalid_module;
  return modules_cache_map_.emplace(module.base_address, std::move(module))
      .first->second;
}

std::vector<const ModuleCache::Module*> ModuleCache::GetModules() const {
  std::vector<const Module*> result;
  result.reserve(modules_cache_map_.size());
  for (const auto& it : modules_cache_map_)
    result.push_back(&it.second);
  return result;
}

}  // namespace base
