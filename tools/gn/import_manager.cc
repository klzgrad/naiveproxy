// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/import_manager.h"

#include "tools/gn/err.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/scope_per_file_provider.h"
#include "tools/gn/trace.h"

namespace {

// Returns a newly-allocated scope on success, null on failure.
std::unique_ptr<Scope> UncachedImport(const Settings* settings,
                                      const SourceFile& file,
                                      const ParseNode* node_for_err,
                                      Err* err) {
  ScopedTrace load_trace(TraceItem::TRACE_IMPORT_LOAD, file.value());

  const ParseNode* node = g_scheduler->input_file_manager()->SyncLoadFile(
      node_for_err->GetRange(), settings->build_settings(), file, err);
  if (!node)
    return nullptr;

  std::unique_ptr<Scope> scope(new Scope(settings->base_config()));
  scope->set_source_dir(file.GetDir());

  // Don't allow ScopePerFileProvider to provide target-related variables.
  // These will be relative to the imported file, which is probably not what
  // people mean when they use these.
  ScopePerFileProvider per_file_provider(scope.get(), false);

  scope->SetProcessingImport();
  node->Execute(scope.get(), err);
  if (err->has_error()) {
    // If there was an error, append the caller location so the error message
    // displays a why the file was imported (esp. useful for failed asserts).
    err->AppendSubErr(Err(node_for_err, "whence it was imported."));
    return nullptr;
  }
  scope->ClearProcessingImport();

  return scope;
}

}  // namespace

struct ImportManager::ImportInfo {
  ImportInfo() {}
  ~ImportInfo() {}

  // This lock protects the unique_ptr. Once the scope is computed,
  // it is const and can be accessed read-only outside of the lock.
  base::Lock load_lock;

  std::unique_ptr<const Scope> scope;

  // The result of loading the import. If the load failed, the scope will be
  // null but this will be set to error. In this case the thread should not
  // attempt to load the file, even if the scope is null.
  Err load_result;
};

ImportManager::ImportManager() {
}

ImportManager::~ImportManager() {
}

bool ImportManager::DoImport(const SourceFile& file,
                             const ParseNode* node_for_err,
                             Scope* scope,
                             Err* err) {
  // See if we have a cached import, but be careful to actually do the scope
  // copying outside of the lock.
  ImportInfo* import_info = nullptr;
  {
    base::AutoLock lock(imports_lock_);
    std::unique_ptr<ImportInfo>& info_ptr = imports_[file];
    if (!info_ptr)
      info_ptr.reset(new ImportInfo);

    // Promote the ImportInfo to outside of the imports lock.
    import_info = info_ptr.get();
  }

  // Now use the per-import-file lock to block this thread if another thread
  // is already processing the import.
  const Scope* import_scope = nullptr;
  {
    base::TimeTicks import_block_begin = base::TimeTicks::Now();
    base::AutoLock lock(import_info->load_lock);

    if (!import_info->scope) {
      // Only load if the import hasn't already failed.
      if (!import_info->load_result.has_error()) {
        import_info->scope = UncachedImport(
            scope->settings(), file, node_for_err, &import_info->load_result);
      }
      if (import_info->load_result.has_error()) {
        *err = import_info->load_result;
        return false;
      }
    } else {
      // Add trace if this thread was blocked for a long period of time and did
      // not load the import itself.
      base::TimeTicks import_block_end = base::TimeTicks::Now();
      constexpr auto kImportBlockTraceThreshold =
          base::TimeDelta::FromMilliseconds(20);
      if (TracingEnabled() &&
          import_block_end - import_block_begin > kImportBlockTraceThreshold) {
        auto* import_block_trace =
            new TraceItem(TraceItem::TRACE_IMPORT_BLOCK, file.value(),
                          base::PlatformThread::CurrentId());
        import_block_trace->set_begin(import_block_begin);
        import_block_trace->set_end(import_block_end);
        AddTrace(import_block_trace);
      }
    }

    // Promote the now-read-only scope to outside the load lock.
    import_scope = import_info->scope.get();
  }

  Scope::MergeOptions options;
  options.skip_private_vars = true;
  options.mark_dest_used = true;  // Don't require all imported values be used.
  return import_scope->NonRecursiveMergeTo(scope, options, node_for_err,
                                           "import", err);
}

std::vector<SourceFile> ImportManager::GetImportedFiles() const {
  std::vector<SourceFile> imported_files;
  imported_files.resize(imports_.size());
  std::transform(imports_.begin(), imports_.end(), imported_files.begin(),
                 [](const ImportMap::value_type& val) { return val.first; });
  return imported_files;
}
