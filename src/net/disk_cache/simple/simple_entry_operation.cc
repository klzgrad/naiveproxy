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

SimpleEntryOperation::SimpleEntryOperation(SimpleEntryOperation&& other)
    : entry_(std::move(other.entry_)),
      buf_(std::move(other.buf_)),
      callback_(std::move(other.callback_)),
      out_entry_(other.out_entry_),
      offset_(other.offset_),
      sparse_offset_(other.sparse_offset_),
      length_(other.length_),
      out_start_(other.out_start_),
      type_(other.type_),
      have_index_(other.have_index_),
      index_(other.index_),
      truncate_(other.truncate_),
      optimistic_(other.optimistic_) {}

SimpleEntryOperation::~SimpleEntryOperation() = default;

// static
SimpleEntryOperation SimpleEntryOperation::OpenOperation(
    SimpleEntryImpl* entry,
    bool have_index,
    net::CompletionOnceCallback callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry, NULL, std::move(callback), out_entry, 0, 0,
                              0, NULL, TYPE_OPEN, have_index, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CreateOperation(
    SimpleEntryImpl* entry,
    bool have_index,
    net::CompletionOnceCallback callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry, NULL, std::move(callback), out_entry, 0, 0,
                              0, NULL, TYPE_CREATE, have_index, 0, false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CloseOperation(
    SimpleEntryImpl* entry) {
  return SimpleEntryOperation(entry, NULL, CompletionOnceCallback(), NULL, 0, 0,
                              0, NULL, TYPE_CLOSE, false, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadOperation(
    SimpleEntryImpl* entry,
    int index,
    int offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), NULL, offset, 0,
                              length, NULL, TYPE_READ, false, index, false,
                              false);
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
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), NULL, offset, 0,
                              length, NULL, TYPE_WRITE, false, index, truncate,
                              optimistic);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), NULL, 0,
                              sparse_offset, length, NULL, TYPE_READ_SPARSE,
                              false, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::WriteSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), NULL, 0,
                              sparse_offset, length, NULL, TYPE_WRITE_SPARSE,
                              false, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::GetAvailableRangeOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    int64_t* out_start,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, NULL, std::move(callback), NULL, 0,
                              sparse_offset, length, out_start,
                              TYPE_GET_AVAILABLE_RANGE, false, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::DoomOperation(
    SimpleEntryImpl* entry,
    net::CompletionOnceCallback callback) {
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
  return SimpleEntryOperation(
      entry, buf, std::move(callback), out_entry, offset, sparse_offset, length,
      out_start, TYPE_DOOM, have_index, index, truncate, optimistic);
}

SimpleEntryOperation::SimpleEntryOperation(SimpleEntryImpl* entry,
                                           net::IOBuffer* buf,
                                           net::CompletionOnceCallback callback,
                                           Entry** out_entry,
                                           int offset,
                                           int64_t sparse_offset,
                                           int length,
                                           int64_t* out_start,
                                           EntryOperationType type,
                                           bool have_index,
                                           int index,
                                           bool truncate,
                                           bool optimistic)
    : entry_(entry),
      buf_(buf),
      callback_(std::move(callback)),
      out_entry_(out_entry),
      offset_(offset),
      sparse_offset_(sparse_offset),
      length_(length),
      out_start_(out_start),
      type_(type),
      have_index_(have_index),
      index_(index),
      truncate_(truncate),
      optimistic_(optimistic) {}

}  // namespace disk_cache
