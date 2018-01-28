// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_entry_operation.h"

#include <limits.h>

#include "base/logging.h"
#include "net/base/io_buffer.h"
#include "net/disk_cache/disk_cache.h"
#include "net/disk_cache/simple/simple_entry_impl.h"

namespace disk_cache {

namespace {

bool IsReadWriteType(unsigned int type) {
  return type == SimpleEntryOperation::TYPE_READ ||
         type == SimpleEntryOperation::TYPE_WRITE ||
         type == SimpleEntryOperation::TYPE_READ_SPARSE ||
         type == SimpleEntryOperation::TYPE_WRITE_SPARSE;
}

bool IsReadType(unsigned type) {
  return type == SimpleEntryOperation::TYPE_READ ||
         type == SimpleEntryOperation::TYPE_READ_SPARSE;
}

bool IsSparseType(unsigned type) {
  return type == SimpleEntryOperation::TYPE_READ_SPARSE ||
         type == SimpleEntryOperation::TYPE_WRITE_SPARSE;
}

}  // anonymous namespace

SimpleEntryOperation::SimpleEntryOperation(const SimpleEntryOperation& other)
    : entry_(other.entry_.get()),
      buf_(other.buf_),
      callback_(other.callback_),
      out_entry_(other.out_entry_),
      offset_(other.offset_),
      sparse_offset_(other.sparse_offset_),
      length_(other.length_),
      out_start_(other.out_start_),
      type_(other.type_),
      have_index_(other.have_index_),
      index_(other.index_),
      truncate_(other.truncate_),
      optimistic_(other.optimistic_),
      alone_in_queue_(other.alone_in_queue_) {
}

SimpleEntryOperation::~SimpleEntryOperation() {}

// static
SimpleEntryOperation SimpleEntryOperation::OpenOperation(
    SimpleEntryImpl* entry,
    bool have_index,
    const CompletionCallback& callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry,
                              NULL,
                              callback,
                              out_entry,
                              0,
                              0,
                              0,
                              NULL,
                              TYPE_OPEN,
                              have_index,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CreateOperation(
    SimpleEntryImpl* entry,
    bool have_index,
    const CompletionCallback& callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry,
                              NULL,
                              callback,
                              out_entry,
                              0,
                              0,
                              0,
                              NULL,
                              TYPE_CREATE,
                              have_index,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CloseOperation(
    SimpleEntryImpl* entry) {
  return SimpleEntryOperation(entry,
                              NULL,
                              CompletionCallback(),
                              NULL,
                              0,
                              0,
                              0,
                              NULL,
                              TYPE_CLOSE,
                              false,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadOperation(
    SimpleEntryImpl* entry,
    int index,
    int offset,
    int length,
    net::IOBuffer* buf,
    const CompletionCallback& callback,
    bool alone_in_queue) {
  return SimpleEntryOperation(entry,
                              buf,
                              callback,
                              NULL,
                              offset,
                              0,
                              length,
                              NULL,
                              TYPE_READ,
                              false,
                              index,
                              false,
                              false,
                              alone_in_queue);
}

// static
SimpleEntryOperation SimpleEntryOperation::WriteOperation(
    SimpleEntryImpl* entry,
    int index,
    int offset,
    int length,
    net::IOBuffer* buf,
    bool truncate,
    bool optimistic,
    const CompletionCallback& callback) {
  return SimpleEntryOperation(entry,
                              buf,
                              callback,
                              NULL,
                              offset,
                              0,
                              length,
                              NULL,
                              TYPE_WRITE,
                              false,
                              index,
                              truncate,
                              optimistic,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    const CompletionCallback& callback) {
  return SimpleEntryOperation(entry,
                              buf,
                              callback,
                              NULL,
                              0,
                              sparse_offset,
                              length,
                              NULL,
                              TYPE_READ_SPARSE,
                              false,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::WriteSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    const CompletionCallback& callback) {
  return SimpleEntryOperation(entry,
                              buf,
                              callback,
                              NULL,
                              0,
                              sparse_offset,
                              length,
                              NULL,
                              TYPE_WRITE_SPARSE,
                              false,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::GetAvailableRangeOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    int64_t* out_start,
    const CompletionCallback& callback) {
  return SimpleEntryOperation(entry,
                              NULL,
                              callback,
                              NULL,
                              0,
                              sparse_offset,
                              length,
                              out_start,
                              TYPE_GET_AVAILABLE_RANGE,
                              false,
                              0,
                              false,
                              false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::DoomOperation(
    SimpleEntryImpl* entry,
    const CompletionCallback& callback) {
  net::IOBuffer* const buf = NULL;
  Entry** const out_entry = NULL;
  const int offset = 0;
  const int64_t sparse_offset = 0;
  const int length = 0;
  int64_t* const out_start = NULL;
  const bool have_index = false;
  const int index = 0;
  const bool truncate = false;
  const bool optimistic = false;
  const bool alone_in_queue = false;
  return SimpleEntryOperation(entry,
                              buf,
                              callback,
                              out_entry,
                              offset,
                              sparse_offset,
                              length,
                              out_start,
                              TYPE_DOOM,
                              have_index,
                              index,
                              truncate,
                              optimistic,
                              alone_in_queue);
}

bool SimpleEntryOperation::ConflictsWith(
    const SimpleEntryOperation& other_op) const {
  EntryOperationType other_type = other_op.type();

  // Non-read/write operations conflict with everything.
  if (!IsReadWriteType(type_) || !IsReadWriteType(other_type))
    return true;

  // Reads (sparse or otherwise) conflict with nothing.
  if (IsReadType(type_) && IsReadType(other_type))
    return false;

  // Sparse and non-sparse operations do not conflict with each other.
  if (IsSparseType(type_) != IsSparseType(other_type)) {
    return false;
  }

  // There must be two read/write operations, at least one must be a write, and
  // they must be either both non-sparse or both sparse.  Compare the streams
  // and offsets to see whether they overlap.

  if (IsSparseType(type_)) {
    int64_t end = sparse_offset_ + length_;
    int64_t other_op_end = other_op.sparse_offset() + other_op.length();
    return sparse_offset_ < other_op_end && other_op.sparse_offset() < end;
  }

  if (index_ != other_op.index_)
    return false;
  int end = (type_ == TYPE_WRITE && truncate_) ? INT_MAX : offset_ + length_;
  int other_op_end = (other_op.type() == TYPE_WRITE && other_op.truncate())
                         ? INT_MAX
                         : other_op.offset() + other_op.length();
  return offset_ < other_op_end && other_op.offset() < end;
}

void SimpleEntryOperation::ReleaseReferences() {
  callback_ = CompletionCallback();
  buf_ = NULL;
  entry_ = NULL;
}

SimpleEntryOperation::SimpleEntryOperation(SimpleEntryImpl* entry,
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
                                           bool alone_in_queue)
    : entry_(entry),
      buf_(buf),
      callback_(callback),
      out_entry_(out_entry),
      offset_(offset),
      sparse_offset_(sparse_offset),
      length_(length),
      out_start_(out_start),
      type_(type),
      have_index_(have_index),
      index_(index),
      truncate_(truncate),
      optimistic_(optimistic),
      alone_in_queue_(alone_in_queue) {}

}  // namespace disk_cache
