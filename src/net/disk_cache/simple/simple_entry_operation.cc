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

SimpleEntryOperation::SimpleEntryOperation(SimpleEntryOperation&& other) =
    default;

SimpleEntryOperation::~SimpleEntryOperation() = default;

// static
SimpleEntryOperation SimpleEntryOperation::OpenOperation(
    SimpleEntryImpl* entry,
    net::CompletionOnceCallback callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry, nullptr, std::move(callback), out_entry,
                              nullptr, 0, 0, 0, nullptr, TYPE_OPEN,
                              INDEX_NOEXIST, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CreateOperation(
    SimpleEntryImpl* entry,
    net::CompletionOnceCallback callback,
    Entry** out_entry) {
  return SimpleEntryOperation(entry, nullptr, std::move(callback), out_entry,
                              nullptr, 0, 0, 0, nullptr, TYPE_CREATE,
                              INDEX_NOEXIST, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::OpenOrCreateOperation(
    SimpleEntryImpl* entry,
    OpenEntryIndexEnum index_state,
    net::CompletionOnceCallback callback,
    EntryWithOpened* entry_struct) {
  return SimpleEntryOperation(
      entry, nullptr, std::move(callback), nullptr, entry_struct, 0, 0, 0,
      nullptr, TYPE_OPEN_OR_CREATE, index_state, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::CloseOperation(
    SimpleEntryImpl* entry) {
  return SimpleEntryOperation(entry, nullptr, CompletionOnceCallback(), nullptr,
                              nullptr, 0, 0, 0, nullptr, TYPE_CLOSE,
                              INDEX_NOEXIST, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadOperation(
    SimpleEntryImpl* entry,
    int index,
    int offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), nullptr, nullptr,
                              offset, 0, length, nullptr, TYPE_READ,
                              INDEX_NOEXIST, index, false, false);
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
  return SimpleEntryOperation(entry, buf, std::move(callback), nullptr, nullptr,
                              offset, 0, length, nullptr, TYPE_WRITE,
                              INDEX_NOEXIST, index, truncate, optimistic);
}

// static
SimpleEntryOperation SimpleEntryOperation::ReadSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, buf, std::move(callback), nullptr, nullptr,
                              0, sparse_offset, length, nullptr,
                              TYPE_READ_SPARSE, INDEX_NOEXIST, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::WriteSparseOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    net::IOBuffer* buf,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(
      entry, buf, std::move(callback), nullptr, nullptr, 0, sparse_offset,
      length, nullptr, TYPE_WRITE_SPARSE, INDEX_NOEXIST, 0, false, false);
}

// static
SimpleEntryOperation SimpleEntryOperation::GetAvailableRangeOperation(
    SimpleEntryImpl* entry,
    int64_t sparse_offset,
    int length,
    int64_t* out_start,
    CompletionOnceCallback callback) {
  return SimpleEntryOperation(entry, nullptr, std::move(callback), nullptr,
                              nullptr, 0, sparse_offset, length, out_start,
                              TYPE_GET_AVAILABLE_RANGE, INDEX_NOEXIST, 0, false,
                              false);
}

// static
SimpleEntryOperation SimpleEntryOperation::DoomOperation(
    SimpleEntryImpl* entry,
    net::CompletionOnceCallback callback) {
  net::IOBuffer* const buf = nullptr;
  Entry** const out_entry = nullptr;
  EntryWithOpened* out_entry_struct = nullptr;
  const int offset = 0;
  const int64_t sparse_offset = 0;
  const int length = 0;
  int64_t* const out_start = nullptr;
  const OpenEntryIndexEnum index_state = INDEX_NOEXIST;
  const int index = 0;
  const bool truncate = false;
  const bool optimistic = false;
  return SimpleEntryOperation(entry, buf, std::move(callback), out_entry,
                              out_entry_struct, offset, sparse_offset, length,
                              out_start, TYPE_DOOM, index_state, index,
                              truncate, optimistic);
}

SimpleEntryOperation::SimpleEntryOperation(SimpleEntryImpl* entry,
                                           net::IOBuffer* buf,
                                           net::CompletionOnceCallback callback,
                                           Entry** out_entry,
                                           EntryWithOpened* entry_struct,
                                           int offset,
                                           int64_t sparse_offset,
                                           int length,
                                           int64_t* out_start,
                                           EntryOperationType type,
                                           OpenEntryIndexEnum index_state,
                                           int index,
                                           bool truncate,
                                           bool optimistic)
    : entry_(entry),
      buf_(buf),
      callback_(std::move(callback)),
      out_entry_(out_entry),
      entry_struct_(entry_struct),
      offset_(offset),
      sparse_offset_(sparse_offset),
      length_(length),
      out_start_(out_start),
      type_(type),
      index_state_(index_state),
      index_(index),
      truncate_(truncate),
      optimistic_(optimistic) {}

}  // namespace disk_cache
