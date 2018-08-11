// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_ENTRY_OPERATION_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_ENTRY_OPERATION_H_

#include <stdint.h>

#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"

namespace net {
class IOBuffer;
}

namespace disk_cache {

class Entry;
class SimpleEntryImpl;

// SimpleEntryOperation stores the information regarding operations in
// SimpleEntryImpl, between the moment they are issued by users of the backend,
// and the moment when they are executed.
class SimpleEntryOperation {
 public:
  typedef net::CompletionCallback CompletionCallback;

  enum EntryOperationType {
    TYPE_OPEN = 0,
    TYPE_CREATE = 1,
    TYPE_CLOSE = 2,
    TYPE_READ = 3,
    TYPE_WRITE = 4,
    TYPE_READ_SPARSE = 5,
    TYPE_WRITE_SPARSE = 6,
    TYPE_GET_AVAILABLE_RANGE = 7,
    TYPE_DOOM = 8,
  };

  SimpleEntryOperation(const SimpleEntryOperation& other);
  ~SimpleEntryOperation();

  static SimpleEntryOperation OpenOperation(SimpleEntryImpl* entry,
                                            bool have_index,
                                            const CompletionCallback& callback,
                                            Entry** out_entry);
  static SimpleEntryOperation CreateOperation(
      SimpleEntryImpl* entry,
      bool have_index,
      const CompletionCallback& callback,
      Entry** out_entry);
  static SimpleEntryOperation CloseOperation(SimpleEntryImpl* entry);
  static SimpleEntryOperation ReadOperation(SimpleEntryImpl* entry,
                                            int index,
                                            int offset,
                                            int length,
                                            net::IOBuffer* buf,
                                            const CompletionCallback& callback,
                                            bool alone_in_queue);
  static SimpleEntryOperation WriteOperation(
      SimpleEntryImpl* entry,
      int index,
      int offset,
      int length,
      net::IOBuffer* buf,
      bool truncate,
      bool optimistic,
      const CompletionCallback& callback);
  static SimpleEntryOperation ReadSparseOperation(
      SimpleEntryImpl* entry,
      int64_t sparse_offset,
      int length,
      net::IOBuffer* buf,
      const CompletionCallback& callback);
  static SimpleEntryOperation WriteSparseOperation(
      SimpleEntryImpl* entry,
      int64_t sparse_offset,
      int length,
      net::IOBuffer* buf,
      const CompletionCallback& callback);
  static SimpleEntryOperation GetAvailableRangeOperation(
      SimpleEntryImpl* entry,
      int64_t sparse_offset,
      int length,
      int64_t* out_start,
      const CompletionCallback& callback);
  static SimpleEntryOperation DoomOperation(
      SimpleEntryImpl* entry,
      const CompletionCallback& callback);

  bool ConflictsWith(const SimpleEntryOperation& other_op) const;
  // Releases all references. After calling this operation, SimpleEntryOperation
  // will only hold POD members.
  void ReleaseReferences();

  EntryOperationType type() const {
    return static_cast<EntryOperationType>(type_);
  }
  const CompletionCallback& callback() const { return callback_; }
  Entry** out_entry() { return out_entry_; }
  bool have_index() const { return have_index_; }
  int index() const { return index_; }
  int offset() const { return offset_; }
  int64_t sparse_offset() const { return sparse_offset_; }
  int length() const { return length_; }
  int64_t* out_start() { return out_start_; }
  net::IOBuffer* buf() { return buf_.get(); }
  bool truncate() const { return truncate_; }
  bool optimistic() const { return optimistic_; }
  bool alone_in_queue() const { return alone_in_queue_; }

 private:
  SimpleEntryOperation(SimpleEntryImpl* entry,
                       net::IOBuffer* buf,
                       const CompletionCallback& callback,
                       Entry** out_entry,
                       int offset,
                       int64_t sparse_offset,
                       int length,
                       int64_t* out_start,
                       EntryOperationType type,
                       bool have_index,
                       int index,
                       bool truncate,
                       bool optimistic,
                       bool alone_in_queue);

  // This ensures entry will not be deleted until the operation has ran.
  scoped_refptr<SimpleEntryImpl> entry_;
  scoped_refptr<net::IOBuffer> buf_;
  CompletionCallback callback_;

  // Used in open and create operations.
  Entry** out_entry_;

  // Used in write and read operations.
  const int offset_;
  const int64_t sparse_offset_;
  const int length_;

  // Used in get available range operations.
  int64_t* const out_start_;

  const EntryOperationType type_;
  // Used in open and create operations.
  const bool have_index_;
  // Used in write and read operations.
  const unsigned int index_;
  // Used only in write operations.
  const bool truncate_;
  const bool optimistic_;
  // Used only in SimpleCache.ReadIsParallelizable histogram.
  const bool alone_in_queue_;
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_ENTRY_OPERATION_H_
