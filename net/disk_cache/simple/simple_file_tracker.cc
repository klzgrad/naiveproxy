// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_file_tracker.h"

#include <memory>
#include <utility>

#include "base/files/file.h"
#include "base/synchronization/lock.h"
#include "net/disk_cache/simple/simple_synchronous_entry.h"

namespace disk_cache {

SimpleFileTracker::SimpleFileTracker() = default;

SimpleFileTracker::~SimpleFileTracker() {
  DCHECK(tracked_files_.empty());
}

void SimpleFileTracker::Register(const SimpleSynchronousEntry* owner,
                                 SubFile subfile,
                                 std::unique_ptr<base::File> file) {
  base::AutoLock hold_lock(lock_);

  // Make sure the list exists.
  auto insert_status = tracked_files_.insert(std::make_pair(
      owner->entry_file_key().entry_hash, std::vector<TrackedFiles>()));

  std::vector<TrackedFiles>& candidates = insert_status.first->second;

  // See if entry already exists, if not append.
  TrackedFiles* owners_files = nullptr;
  for (TrackedFiles& candidate : candidates) {
    if (candidate.owner == owner) {
      owners_files = &candidate;
      break;
    }
  }

  if (!owners_files) {
    candidates.emplace_back();
    owners_files = &candidates.back();
    owners_files->owner = owner;
    owners_files->key = owner->entry_file_key();
  }

  int file_index = static_cast<int>(subfile);
  DCHECK_EQ(TrackedFiles::TF_NO_REGISTRATION, owners_files->state[file_index]);
  owners_files->files[file_index] = std::move(file);
  owners_files->state[file_index] = TrackedFiles::TF_REGISTERED;
}

SimpleFileTracker::FileHandle SimpleFileTracker::Acquire(
    const SimpleSynchronousEntry* owner,
    SubFile subfile) {
  base::AutoLock hold_lock(lock_);
  std::vector<TrackedFiles>::iterator owners_files = Find(owner);
  int file_index = static_cast<int>(subfile);

  DCHECK_EQ(TrackedFiles::TF_REGISTERED, owners_files->state[file_index]);
  owners_files->state[file_index] = TrackedFiles::TF_ACQUIRED;
  return FileHandle(this, owner, subfile,
                    owners_files->files[file_index].get());
}

bool SimpleFileTracker::TrackedFiles::Empty() const {
  for (State s : state)
    if (s != TF_NO_REGISTRATION)
      return false;
  return true;
}

void SimpleFileTracker::Release(const SimpleSynchronousEntry* owner,
                                SubFile subfile) {
  std::unique_ptr<base::File> file_to_close;

  {
    base::AutoLock hold_lock(lock_);
    std::vector<TrackedFiles>::iterator owners_files = Find(owner);
    int file_index = static_cast<int>(subfile);

    DCHECK(owners_files->state[file_index] == TrackedFiles::TF_ACQUIRED ||
           owners_files->state[file_index] ==
               TrackedFiles::TF_ACQUIRED_PENDING_CLOSE);

    // Prepare to executed deferred close, if any.
    if (owners_files->state[file_index] ==
        TrackedFiles::TF_ACQUIRED_PENDING_CLOSE) {
      file_to_close = PrepareClose(owners_files, file_index);
    } else {
      owners_files->state[file_index] = TrackedFiles::TF_REGISTERED;
    }
  }

  // The destructor of file_to_close will close it if needed.
}

void SimpleFileTracker::Close(const SimpleSynchronousEntry* owner,
                              SubFile subfile) {
  std::unique_ptr<base::File> file_to_close;

  {
    base::AutoLock hold_lock(lock_);
    std::vector<TrackedFiles>::iterator owners_files = Find(owner);
    int file_index = static_cast<int>(subfile);

    DCHECK(owners_files->state[file_index] == TrackedFiles::TF_ACQUIRED ||
           owners_files->state[file_index] == TrackedFiles::TF_REGISTERED);

    if (owners_files->state[file_index] == TrackedFiles::TF_ACQUIRED) {
      // The FD is currently acquired, so we can't clean up the TrackedFiles,
      // just yet; even if this is the last close, so delay the close until it
      // gets released.
      owners_files->state[file_index] = TrackedFiles::TF_ACQUIRED_PENDING_CLOSE;
    } else {
      file_to_close = PrepareClose(owners_files, file_index);
    }
  }

  // The destructor of file_to_close will close it if needed. Thing to watch
  // for impl with stealing: race between bookkeeping above and actual
  // close --- the FD is still alive for it.
}

bool SimpleFileTracker::IsEmptyForTesting() {
  base::AutoLock hold_lock(lock_);
  return tracked_files_.empty();
}

std::vector<SimpleFileTracker::TrackedFiles>::iterator SimpleFileTracker::Find(
    const SimpleSynchronousEntry* owner) {
  auto candidates = tracked_files_.find(owner->entry_file_key().entry_hash);
  DCHECK(candidates != tracked_files_.end());
  for (std::vector<TrackedFiles>::iterator i = candidates->second.begin();
       i != candidates->second.end(); ++i) {
    if (i->owner == owner) {
      return i;
    }
  }
  LOG(DFATAL) << "SimpleFileTracker operation on non-found entry";
  return candidates->second.end();
}

std::unique_ptr<base::File> SimpleFileTracker::PrepareClose(
    std::vector<TrackedFiles>::iterator owners_files,
    int file_index) {
  std::unique_ptr<base::File> file_out =
      std::move(owners_files->files[file_index]);
  owners_files->state[file_index] = TrackedFiles::TF_NO_REGISTRATION;
  if (owners_files->Empty()) {
    auto iter = tracked_files_.find(owners_files->key.entry_hash);
    iter->second.erase(owners_files);
    if (iter->second.empty())
      tracked_files_.erase(iter);
  }
  return file_out;
}

SimpleFileTracker::FileHandle::FileHandle()
    : file_tracker_(nullptr), entry_(nullptr), file_(nullptr) {}

SimpleFileTracker::FileHandle::FileHandle(SimpleFileTracker* file_tracker,
                                          const SimpleSynchronousEntry* entry,
                                          SimpleFileTracker::SubFile subfile,
                                          base::File* file)
    : file_tracker_(file_tracker),
      entry_(entry),
      subfile_(subfile),
      file_(file) {}

SimpleFileTracker::FileHandle::FileHandle(FileHandle&& other) {
  *this = std::move(other);
}

SimpleFileTracker::FileHandle::~FileHandle() {
  if (entry_)
    file_tracker_->Release(entry_, subfile_);
}

SimpleFileTracker::FileHandle& SimpleFileTracker::FileHandle::operator=(
    FileHandle&& other) {
  file_tracker_ = other.file_tracker_;
  entry_ = other.entry_;
  subfile_ = other.subfile_;
  file_ = other.file_;
  other.file_tracker_ = nullptr;
  other.entry_ = nullptr;
  other.file_ = nullptr;
  return *this;
}

base::File* SimpleFileTracker::FileHandle::operator->() const {
  return file_;
}

base::File* SimpleFileTracker::FileHandle::get() const {
  return file_;
}

bool SimpleFileTracker::FileHandle::IsOK() const {
  return file_ && file_->IsValid();
}

}  // namespace disk_cache
