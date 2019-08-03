// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_BLOCKFILE_IN_FLIGHT_BACKEND_IO_H_
#define NET_DISK_CACHE_BLOCKFILE_IN_FLIGHT_BACKEND_IO_H_

#include <stdint.h>

#include <list>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/io_buffer.h"
#include "net/disk_cache/blockfile/in_flight_io.h"
#include "net/disk_cache/blockfile/rankings.h"

namespace base {
class Location;
}

namespace disk_cache {

class BackendImpl;
class Entry;
class EntryImpl;
struct EntryWithOpened;

// This class represents a single asynchronous disk cache IO operation while it
// is being bounced between threads.
class BackendIO : public BackgroundIO {
 public:
  BackendIO(InFlightIO* controller,
            BackendImpl* backend,
            net::CompletionOnceCallback callback);

  // Runs the actual operation on the background thread.
  void ExecuteOperation();

  // Callback implementation.
  void OnIOComplete(int result);

  // Called when we are finishing this operation. If |cancel| is true, the user
  // callback will not be invoked.
  void OnDone(bool cancel);

  // Returns true if this operation is directed to an entry (vs. the backend).
  bool IsEntryOperation();

  bool has_callback() const { return !callback_.is_null(); }
  void RunCallback(int result);

  // The operations we proxy:
  void Init();
  void OpenOrCreateEntry(const std::string& key, EntryWithOpened* entry_struct);
  void OpenEntry(const std::string& key, Entry** entry);
  void CreateEntry(const std::string& key, Entry** entry);
  void DoomEntry(const std::string& key);
  void DoomAllEntries();
  void DoomEntriesBetween(const base::Time initial_time,
                          const base::Time end_time);
  void DoomEntriesSince(const base::Time initial_time);
  void CalculateSizeOfAllEntries();
  void OpenNextEntry(Rankings::Iterator* iterator, Entry** next_entry);
  void EndEnumeration(std::unique_ptr<Rankings::Iterator> iterator);
  void OnExternalCacheHit(const std::string& key);
  void CloseEntryImpl(EntryImpl* entry);
  void DoomEntryImpl(EntryImpl* entry);
  void FlushQueue();  // Dummy operation.
  void RunTask(base::OnceClosure task);
  void ReadData(EntryImpl* entry, int index, int offset, net::IOBuffer* buf,
                int buf_len);
  void WriteData(EntryImpl* entry, int index, int offset, net::IOBuffer* buf,
                 int buf_len, bool truncate);
  void ReadSparseData(EntryImpl* entry,
                      int64_t offset,
                      net::IOBuffer* buf,
                      int buf_len);
  void WriteSparseData(EntryImpl* entry,
                       int64_t offset,
                       net::IOBuffer* buf,
                       int buf_len);
  void GetAvailableRange(EntryImpl* entry,
                         int64_t offset,
                         int len,
                         int64_t* start);
  void CancelSparseIO(EntryImpl* entry);
  void ReadyForSparseIO(EntryImpl* entry);

 private:
  // There are two types of operations to proxy: regular backend operations are
  // executed sequentially (queued by the message loop). On the other hand,
  // operations targeted to a given entry can be long lived and support multiple
  // simultaneous users (multiple reads or writes to the same entry), and they
  // are subject to throttling, so we keep an explicit queue.
  enum Operation {
    OP_NONE = 0,
    OP_INIT,
    OP_OPEN_OR_CREATE,
    OP_OPEN,
    OP_CREATE,
    OP_DOOM,
    OP_DOOM_ALL,
    OP_DOOM_BETWEEN,
    OP_DOOM_SINCE,
    OP_SIZE_ALL,
    OP_OPEN_NEXT,
    OP_END_ENUMERATION,
    OP_ON_EXTERNAL_CACHE_HIT,
    OP_CLOSE_ENTRY,
    OP_DOOM_ENTRY,
    OP_FLUSH_QUEUE,
    OP_RUN_TASK,
    OP_MAX_BACKEND,
    OP_READ,
    OP_WRITE,
    OP_READ_SPARSE,
    OP_WRITE_SPARSE,
    OP_GET_RANGE,
    OP_CANCEL_IO,
    OP_IS_READY
  };

  ~BackendIO() override;

  // Returns true if this operation returns an entry.
  bool ReturnsEntry();
  bool ReturnsEntryWithOpened();

  // Returns the time that has passed since the operation was created.
  base::TimeDelta ElapsedTime() const;

  void ExecuteBackendOperation();
  void ExecuteEntryOperation();

  BackendImpl* backend_;
  net::CompletionOnceCallback callback_;
  Operation operation_;

  // The arguments of all the operations we proxy:
  std::string key_;
  Entry** entry_ptr_;
  EntryWithOpened* entry_with_opened_ptr_;
  base::Time initial_time_;
  base::Time end_time_;
  Rankings::Iterator* iterator_;
  std::unique_ptr<Rankings::Iterator> scoped_iterator_;
  EntryImpl* entry_;
  int index_;
  int offset_;
  scoped_refptr<net::IOBuffer> buf_;
  int buf_len_;
  bool truncate_;
  int64_t offset64_;
  int64_t* start_;
  base::TimeTicks start_time_;
  base::OnceClosure task_;

  DISALLOW_COPY_AND_ASSIGN(BackendIO);
};

// The specialized controller that keeps track of current operations.
class InFlightBackendIO : public InFlightIO {
 public:
  InFlightBackendIO(
      BackendImpl* backend,
      const scoped_refptr<base::SingleThreadTaskRunner>& background_thread);
  ~InFlightBackendIO() override;

  // Proxied operations.
  void Init(net::CompletionOnceCallback callback);
  void OpenOrCreateEntry(const std::string& key,
                         EntryWithOpened* entry_struct,
                         net::CompletionOnceCallback callback);
  void OpenEntry(const std::string& key,
                 Entry** entry,
                 net::CompletionOnceCallback callback);
  void CreateEntry(const std::string& key,
                   Entry** entry,
                   net::CompletionOnceCallback callback);
  void DoomEntry(const std::string& key, net::CompletionOnceCallback callback);
  void DoomAllEntries(net::CompletionOnceCallback callback);
  void DoomEntriesBetween(const base::Time initial_time,
                          const base::Time end_time,
                          net::CompletionOnceCallback callback);
  void DoomEntriesSince(const base::Time initial_time,
                        net::CompletionOnceCallback callback);
  void CalculateSizeOfAllEntries(net::CompletionOnceCallback callback);
  void OpenNextEntry(Rankings::Iterator* iterator,
                     Entry** next_entry,
                     net::CompletionOnceCallback callback);
  void EndEnumeration(std::unique_ptr<Rankings::Iterator> iterator);
  void OnExternalCacheHit(const std::string& key);
  void CloseEntryImpl(EntryImpl* entry);
  void DoomEntryImpl(EntryImpl* entry);
  void FlushQueue(net::CompletionOnceCallback callback);
  void RunTask(base::OnceClosure task, net::CompletionOnceCallback callback);
  void ReadData(EntryImpl* entry,
                int index,
                int offset,
                net::IOBuffer* buf,
                int buf_len,
                net::CompletionOnceCallback callback);
  void WriteData(EntryImpl* entry,
                 int index,
                 int offset,
                 net::IOBuffer* buf,
                 int buf_len,
                 bool truncate,
                 net::CompletionOnceCallback callback);
  void ReadSparseData(EntryImpl* entry,
                      int64_t offset,
                      net::IOBuffer* buf,
                      int buf_len,
                      net::CompletionOnceCallback callback);
  void WriteSparseData(EntryImpl* entry,
                       int64_t offset,
                       net::IOBuffer* buf,
                       int buf_len,
                       net::CompletionOnceCallback callback);
  void GetAvailableRange(EntryImpl* entry,
                         int64_t offset,
                         int len,
                         int64_t* start,
                         net::CompletionOnceCallback callback);
  void CancelSparseIO(EntryImpl* entry);
  void ReadyForSparseIO(EntryImpl* entry, net::CompletionOnceCallback callback);

  // Blocks until all operations are cancelled or completed.
  void WaitForPendingIO();

  scoped_refptr<base::SingleThreadTaskRunner> background_thread() {
    return background_thread_;
  }

  // Returns true if the current sequence is the background thread.
  bool BackgroundIsCurrentSequence() {
    return background_thread_->RunsTasksInCurrentSequence();
  }

  base::WeakPtr<InFlightBackendIO> GetWeakPtr();

 protected:
  void OnOperationComplete(BackgroundIO* operation, bool cancel) override;

 private:
  void PostOperation(const base::Location& from_here, BackendIO* operation);
  BackendImpl* backend_;
  scoped_refptr<base::SingleThreadTaskRunner> background_thread_;
  base::WeakPtrFactory<InFlightBackendIO> ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(InFlightBackendIO);
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_BLOCKFILE_IN_FLIGHT_BACKEND_IO_H_
