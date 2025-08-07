// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path_watcher.h"

#include <memory>

#include "base/files/file_path_watcher_kqueue.h"
#include "base/memory/ptr_util.h"
#include "build/build_config.h"

#if !BUILDFLAG(IS_IOS)
#include "base/files/file_path_watcher_fsevents.h"
#endif

namespace base {

namespace {

class FilePathWatcherImpl : public FilePathWatcher::PlatformDelegate {
 public:
  FilePathWatcherImpl() = default;
  FilePathWatcherImpl(const FilePathWatcherImpl&) = delete;
  FilePathWatcherImpl& operator=(const FilePathWatcherImpl&) = delete;
  ~FilePathWatcherImpl() override = default;

  bool Watch(const FilePath& path,
             Type type,
             const FilePathWatcher::Callback& callback) override {
    // Use kqueue for non-recursive watches and FSEvents for recursive ones.
    DCHECK(!impl_.get());
    if (type == Type::kRecursive) {
      if (!FilePathWatcher::RecursiveWatchAvailable()) {
        return false;
      }
#if !BUILDFLAG(IS_IOS)
      impl_ = std::make_unique<FilePathWatcherFSEvents>();
#endif  // BUILDFLAG(IS_IOS)
    } else {
      impl_ = std::make_unique<FilePathWatcherKQueue>();
    }
    DCHECK(impl_.get());
    return impl_->Watch(path, type, callback);
  }

  void Cancel() override {
    if (impl_.get()) {
      impl_->Cancel();
    }
    set_cancelled();
  }

 private:
  std::unique_ptr<PlatformDelegate> impl_;
};

}  // namespace

FilePathWatcher::FilePathWatcher()
    : FilePathWatcher(std::make_unique<FilePathWatcherImpl>()) {}

}  // namespace base
