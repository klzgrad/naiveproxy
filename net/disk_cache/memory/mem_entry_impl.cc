// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/memory/mem_entry_impl.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/memory/mem_backend_impl.h"
#include "net/disk_cache/net_log_parameters.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"

using base::Time;

namespace disk_cache {

namespace {

const int kSparseData = 1;

// Maximum size of a sparse entry is 2 to the power of this number.
const int kMaxSparseEntryBits = 12;

// Sparse entry has maximum size of 4KB.
const int kMaxSparseEntrySize = 1 << kMaxSparseEntryBits;

// This enum is used for histograms, so only append to the end.
enum WriteResult {
  WRITE_RESULT_SUCCESS = 0,
  WRITE_RESULT_INVALID_ARGUMENT = 1,
  WRITE_RESULT_OVER_MAX_ENTRY_SIZE = 2,
  WRITE_RESULT_EXCEEDED_CACHE_STORAGE_SIZE = 3,
  WRITE_RESULT_MAX = 4,
};

void RecordWriteResult(WriteResult result) {
  UMA_HISTOGRAM_ENUMERATION("MemCache.WriteResult", result, WRITE_RESULT_MAX);
}

// Convert global offset to child index.
int ToChildIndex(int64_t offset) {
  return static_cast<int>(offset >> kMaxSparseEntryBits);
}

// Convert global offset to offset in child entry.
int ToChildOffset(int64_t offset) {
  return static_cast<int>(offset & (kMaxSparseEntrySize - 1));
}

// Returns a name for a child entry given the base_name of the parent and the
// child_id.  This name is only used for logging purposes.
// If the entry is called entry_name, child entries will be named something
// like Range_entry_name:YYY where YYY is the number of the particular child.
std::string GenerateChildName(const std::string& base_name, int child_id) {
  return base::StringPrintf("Range_%s:%i", base_name.c_str(), child_id);
}

// Returns NetLog parameters for the creation of a MemEntryImpl. A separate
// function is needed because child entries don't store their key().
std::unique_ptr<base::Value> NetLogEntryCreationCallback(
    const MemEntryImpl* entry,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  std::string key;
  switch (entry->type()) {
    case MemEntryImpl::PARENT_ENTRY:
      key = entry->key();
      break;
    case MemEntryImpl::CHILD_ENTRY:
      key = GenerateChildName(entry->parent()->key(), entry->child_id());
      break;
  }
  dict->SetString("key", key);
  dict->SetBoolean("created", true);
  return std::move(dict);
}

}  // namespace

MemEntryImpl::MemEntryImpl(MemBackendImpl* backend,
                           const std::string& key,
                           net::NetLog* net_log)
    : MemEntryImpl(backend,
                   key,
                   0,        // child_id
                   nullptr,  // parent
                   net_log) {
  Open();
  // Just creating the entry (without any data) could cause the storage to
  // grow beyond capacity, but we allow such infractions.
  backend_->ModifyStorageSize(GetStorageSize());
}

MemEntryImpl::MemEntryImpl(MemBackendImpl* backend,
                           int child_id,
                           MemEntryImpl* parent,
                           net::NetLog* net_log)
    : MemEntryImpl(backend,
                   std::string(),  // key
                   child_id,
                   parent,
                   net_log) {
  (*parent_->children_)[child_id] = this;
}

void MemEntryImpl::Open() {
  // Only a parent entry can be opened.
  DCHECK_EQ(PARENT_ENTRY, type());
  ++ref_count_;
  DCHECK_GE(ref_count_, 1);
  DCHECK(!doomed_);
}

bool MemEntryImpl::InUse() const {
  if (type() == CHILD_ENTRY)
    return parent_->InUse();

  return ref_count_ > 0;
}

int MemEntryImpl::GetStorageSize() const {
  int storage_size = static_cast<int32_t>(key_.size());
  for (const auto& i : data_)
    storage_size += i.size();
  return storage_size;
}

void MemEntryImpl::UpdateStateOnUse(EntryModified modified_enum) {
  if (!doomed_)
    backend_->OnEntryUpdated(this);

  last_used_ = Time::Now();
  if (modified_enum == ENTRY_WAS_MODIFIED)
    last_modified_ = last_used_;
}

void MemEntryImpl::Doom() {
  if (!doomed_) {
    doomed_ = true;
    backend_->OnEntryDoomed(this);
    net_log_.AddEvent(net::NetLogEventType::ENTRY_DOOM);
  }
  if (!ref_count_)
    delete this;
}

void MemEntryImpl::Close() {
  DCHECK_EQ(PARENT_ENTRY, type());
  --ref_count_;
  DCHECK_GE(ref_count_, 0);
  if (!ref_count_ && doomed_)
    delete this;
}

std::string MemEntryImpl::GetKey() const {
  // A child entry doesn't have key so this method should not be called.
  DCHECK_EQ(PARENT_ENTRY, type());
  return key_;
}

Time MemEntryImpl::GetLastUsed() const {
  return last_used_;
}

Time MemEntryImpl::GetLastModified() const {
  return last_modified_;
}

int32_t MemEntryImpl::GetDataSize(int index) const {
  if (index < 0 || index >= kNumStreams)
    return 0;
  return data_[index].size();
}

int MemEntryImpl::ReadData(int index, int offset, IOBuffer* buf, int buf_len,
                           const CompletionCallback& callback) {
  if (net_log_.IsCapturing()) {
    net_log_.BeginEvent(
        net::NetLogEventType::ENTRY_READ_DATA,
        CreateNetLogReadWriteDataCallback(index, offset, buf_len, false));
  }

  int result = InternalReadData(index, offset, buf, buf_len);

  if (net_log_.IsCapturing()) {
    net_log_.EndEvent(net::NetLogEventType::ENTRY_READ_DATA,
                      CreateNetLogReadWriteCompleteCallback(result));
  }
  return result;
}

int MemEntryImpl::WriteData(int index, int offset, IOBuffer* buf, int buf_len,
                            const CompletionCallback& callback, bool truncate) {
  if (net_log_.IsCapturing()) {
    net_log_.BeginEvent(
        net::NetLogEventType::ENTRY_WRITE_DATA,
        CreateNetLogReadWriteDataCallback(index, offset, buf_len, truncate));
  }

  int result = InternalWriteData(index, offset, buf, buf_len, truncate);

  if (net_log_.IsCapturing()) {
    net_log_.EndEvent(net::NetLogEventType::ENTRY_WRITE_DATA,
                      CreateNetLogReadWriteCompleteCallback(result));
  }
  return result;
}

int MemEntryImpl::ReadSparseData(int64_t offset,
                                 IOBuffer* buf,
                                 int buf_len,
                                 const CompletionCallback& callback) {
  if (net_log_.IsCapturing()) {
    net_log_.BeginEvent(net::NetLogEventType::SPARSE_READ,
                        CreateNetLogSparseOperationCallback(offset, buf_len));
  }
  int result = InternalReadSparseData(offset, buf, buf_len);
  if (net_log_.IsCapturing())
    net_log_.EndEvent(net::NetLogEventType::SPARSE_READ);
  return result;
}

int MemEntryImpl::WriteSparseData(int64_t offset,
                                  IOBuffer* buf,
                                  int buf_len,
                                  const CompletionCallback& callback) {
  if (net_log_.IsCapturing()) {
    net_log_.BeginEvent(net::NetLogEventType::SPARSE_WRITE,
                        CreateNetLogSparseOperationCallback(offset, buf_len));
  }
  int result = InternalWriteSparseData(offset, buf, buf_len);
  if (net_log_.IsCapturing())
    net_log_.EndEvent(net::NetLogEventType::SPARSE_WRITE);
  return result;
}

int MemEntryImpl::GetAvailableRange(int64_t offset,
                                    int len,
                                    int64_t* start,
                                    const CompletionCallback& callback) {
  if (net_log_.IsCapturing()) {
    net_log_.BeginEvent(net::NetLogEventType::SPARSE_GET_RANGE,
                        CreateNetLogSparseOperationCallback(offset, len));
  }
  int result = InternalGetAvailableRange(offset, len, start);
  if (net_log_.IsCapturing()) {
    net_log_.EndEvent(
        net::NetLogEventType::SPARSE_GET_RANGE,
        CreateNetLogGetAvailableRangeResultCallback(*start, result));
  }
  return result;
}

bool MemEntryImpl::CouldBeSparse() const {
  DCHECK_EQ(PARENT_ENTRY, type());
  return (children_.get() != nullptr);
}

int MemEntryImpl::ReadyForSparseIO(const CompletionCallback& callback) {
  return net::OK;
}

size_t MemEntryImpl::EstimateMemoryUsage() const {
  // Subtlety: the entries in children_ are not double counted, as the entry
  // pointers won't be followed by EstimateMemoryUsage.
  return base::trace_event::EstimateMemoryUsage(data_) +
         base::trace_event::EstimateMemoryUsage(key_) +
         base::trace_event::EstimateMemoryUsage(children_);
}

// ------------------------------------------------------------------------

MemEntryImpl::MemEntryImpl(MemBackendImpl* backend,
                           const ::std::string& key,
                           int child_id,
                           MemEntryImpl* parent,
                           net::NetLog* net_log)
    : key_(key),
      ref_count_(0),
      child_id_(child_id),
      child_first_pos_(0),
      parent_(parent),
      last_modified_(Time::Now()),
      last_used_(last_modified_),
      backend_(backend),
      doomed_(false) {
  backend_->OnEntryInserted(this);
  net_log_ = net::NetLogWithSource::Make(
      net_log, net::NetLogSourceType::MEMORY_CACHE_ENTRY);
  net_log_.BeginEvent(net::NetLogEventType::DISK_CACHE_MEM_ENTRY_IMPL,
                      base::Bind(&NetLogEntryCreationCallback, this));
}

MemEntryImpl::~MemEntryImpl() {
  backend_->ModifyStorageSize(-GetStorageSize());

  if (type() == PARENT_ENTRY) {
    if (children_) {
      EntryMap children;
      children_->swap(children);

      for (auto& it : children) {
        // Since |this| is stored in the map, it should be guarded against
        // double dooming, which will result in double destruction.
        if (it.second != this)
          it.second->Doom();
      }
    }
  } else {
    parent_->children_->erase(child_id_);
  }
  net_log_.EndEvent(net::NetLogEventType::DISK_CACHE_MEM_ENTRY_IMPL);
}

int MemEntryImpl::InternalReadData(int index, int offset, IOBuffer* buf,
                                   int buf_len) {
  DCHECK(type() == PARENT_ENTRY || index == kSparseData);

  if (index < 0 || index >= kNumStreams || buf_len < 0)
    return net::ERR_INVALID_ARGUMENT;

  int entry_size = data_[index].size();
  if (offset >= entry_size || offset < 0 || !buf_len)
    return 0;

  if (offset + buf_len > entry_size)
    buf_len = entry_size - offset;

  UpdateStateOnUse(ENTRY_WAS_NOT_MODIFIED);
  std::copy(data_[index].begin() + offset,
            data_[index].begin() + offset + buf_len, buf->data());
  return buf_len;
}

int MemEntryImpl::InternalWriteData(int index, int offset, IOBuffer* buf,
                                    int buf_len, bool truncate) {
  DCHECK(type() == PARENT_ENTRY || index == kSparseData);

  if (index < 0 || index >= kNumStreams) {
    RecordWriteResult(WRITE_RESULT_INVALID_ARGUMENT);
    return net::ERR_INVALID_ARGUMENT;
  }

  if (offset < 0 || buf_len < 0) {
    RecordWriteResult(WRITE_RESULT_INVALID_ARGUMENT);
    return net::ERR_INVALID_ARGUMENT;
  }

  int max_file_size = backend_->MaxFileSize();

  // offset of buf_len could be negative numbers.
  if (offset > max_file_size || buf_len > max_file_size ||
      offset + buf_len > max_file_size) {
    RecordWriteResult(WRITE_RESULT_OVER_MAX_ENTRY_SIZE);
    return net::ERR_FAILED;
  }

  int old_data_size = data_[index].size();
  if (truncate || old_data_size < offset + buf_len) {
    int delta = offset + buf_len - old_data_size;
    backend_->ModifyStorageSize(delta);
    if (backend_->HasExceededStorageSize()) {
      backend_->ModifyStorageSize(-delta);
      RecordWriteResult(WRITE_RESULT_EXCEEDED_CACHE_STORAGE_SIZE);
      return net::ERR_INSUFFICIENT_RESOURCES;
    }

    data_[index].resize(offset + buf_len);

    // Zero fill any hole.
    if (old_data_size < offset) {
      std::fill(data_[index].begin() + old_data_size,
                data_[index].begin() + offset, 0);
    }
  }

  UpdateStateOnUse(ENTRY_WAS_MODIFIED);
  RecordWriteResult(WRITE_RESULT_SUCCESS);

  if (!buf_len)
    return 0;

  std::copy(buf->data(), buf->data() + buf_len, data_[index].begin() + offset);
  return buf_len;
}

int MemEntryImpl::InternalReadSparseData(int64_t offset,
                                         IOBuffer* buf,
                                         int buf_len) {
  DCHECK_EQ(PARENT_ENTRY, type());

  if (!InitSparseInfo())
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (offset < 0 || buf_len < 0)
    return net::ERR_INVALID_ARGUMENT;

  // We will keep using this buffer and adjust the offset in this buffer.
  scoped_refptr<net::DrainableIOBuffer> io_buf(
      new net::DrainableIOBuffer(buf, buf_len));

  // Iterate until we have read enough.
  while (io_buf->BytesRemaining()) {
    MemEntryImpl* child = GetChild(offset + io_buf->BytesConsumed(), false);

    // No child present for that offset.
    if (!child)
      break;

    // We then need to prepare the child offset and len.
    int child_offset = ToChildOffset(offset + io_buf->BytesConsumed());

    // If we are trying to read from a position that the child entry has no data
    // we should stop.
    if (child_offset < child->child_first_pos_)
      break;
    if (net_log_.IsCapturing()) {
      net_log_.BeginEvent(
          net::NetLogEventType::SPARSE_READ_CHILD_DATA,
          CreateNetLogSparseReadWriteCallback(child->net_log_.source(),
                                              io_buf->BytesRemaining()));
    }
    int ret = child->ReadData(kSparseData, child_offset, io_buf.get(),
                              io_buf->BytesRemaining(), CompletionCallback());
    if (net_log_.IsCapturing()) {
      net_log_.EndEventWithNetErrorCode(
          net::NetLogEventType::SPARSE_READ_CHILD_DATA, ret);
    }

    // If we encounter an error in one entry, return immediately.
    if (ret < 0)
      return ret;
    else if (ret == 0)
      break;

    // Increment the counter by number of bytes read in the child entry.
    io_buf->DidConsume(ret);
  }

  UpdateStateOnUse(ENTRY_WAS_NOT_MODIFIED);
  return io_buf->BytesConsumed();
}

int MemEntryImpl::InternalWriteSparseData(int64_t offset,
                                          IOBuffer* buf,
                                          int buf_len) {
  DCHECK_EQ(PARENT_ENTRY, type());

  if (!InitSparseInfo())
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (offset < 0 || buf_len < 0)
    return net::ERR_INVALID_ARGUMENT;

  scoped_refptr<net::DrainableIOBuffer> io_buf(
      new net::DrainableIOBuffer(buf, buf_len));

  // This loop walks through child entries continuously starting from |offset|
  // and writes blocks of data (of maximum size kMaxSparseEntrySize) into each
  // child entry until all |buf_len| bytes are written. The write operation can
  // start in the middle of an entry.
  while (io_buf->BytesRemaining()) {
    MemEntryImpl* child = GetChild(offset + io_buf->BytesConsumed(), true);
    int child_offset = ToChildOffset(offset + io_buf->BytesConsumed());

    // Find the right amount to write, this evaluates the remaining bytes to
    // write and remaining capacity of this child entry.
    int write_len = std::min(static_cast<int>(io_buf->BytesRemaining()),
                             kMaxSparseEntrySize - child_offset);

    // Keep a record of the last byte position (exclusive) in the child.
    int data_size = child->GetDataSize(kSparseData);

    if (net_log_.IsCapturing()) {
      net_log_.BeginEvent(net::NetLogEventType::SPARSE_WRITE_CHILD_DATA,
                          CreateNetLogSparseReadWriteCallback(
                              child->net_log_.source(), write_len));
    }

    // Always writes to the child entry. This operation may overwrite data
    // previously written.
    // TODO(hclam): if there is data in the entry and this write is not
    // continuous we may want to discard this write.
    int ret = child->WriteData(kSparseData, child_offset, io_buf.get(),
                               write_len, CompletionCallback(), true);
    if (net_log_.IsCapturing()) {
      net_log_.EndEventWithNetErrorCode(
          net::NetLogEventType::SPARSE_WRITE_CHILD_DATA, ret);
    }
    if (ret < 0)
      return ret;
    else if (ret == 0)
      break;

    // Keep a record of the first byte position in the child if the write was
    // not aligned nor continuous. This is to enable witting to the middle
    // of an entry and still keep track of data off the aligned edge.
    if (data_size != child_offset)
      child->child_first_pos_ = child_offset;

    // Adjust the offset in the IO buffer.
    io_buf->DidConsume(ret);
  }

  UpdateStateOnUse(ENTRY_WAS_MODIFIED);
  return io_buf->BytesConsumed();
}

int MemEntryImpl::InternalGetAvailableRange(int64_t offset,
                                            int len,
                                            int64_t* start) {
  DCHECK_EQ(PARENT_ENTRY, type());
  DCHECK(start);

  if (!InitSparseInfo())
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (offset < 0 || len < 0 || !start)
    return net::ERR_INVALID_ARGUMENT;

  MemEntryImpl* current_child = nullptr;

  // Find the first child and record the number of empty bytes.
  int empty = FindNextChild(offset, len, &current_child);
  if (current_child && empty < len) {
    *start = offset + empty;
    len -= empty;

    // Counts the number of continuous bytes.
    int continuous = 0;

    // This loop scan for continuous bytes.
    while (len && current_child) {
      // Number of bytes available in this child.
      int data_size = current_child->GetDataSize(kSparseData) -
                      ToChildOffset(*start + continuous);
      if (data_size > len)
        data_size = len;

      // We have found more continuous bytes so increment the count. Also
      // decrement the length we should scan.
      continuous += data_size;
      len -= data_size;

      // If the next child is discontinuous, break the loop.
      if (FindNextChild(*start + continuous, len, &current_child))
        break;
    }
    return continuous;
  }
  *start = offset;
  return 0;
}

bool MemEntryImpl::InitSparseInfo() {
  DCHECK_EQ(PARENT_ENTRY, type());

  if (!children_) {
    // If we already have some data in sparse stream but we are being
    // initialized as a sparse entry, we should fail.
    if (GetDataSize(kSparseData))
      return false;
    children_.reset(new EntryMap());

    // The parent entry stores data for the first block, so save this object to
    // index 0.
    (*children_)[0] = this;
  }
  return true;
}

MemEntryImpl* MemEntryImpl::GetChild(int64_t offset, bool create) {
  DCHECK_EQ(PARENT_ENTRY, type());
  int index = ToChildIndex(offset);
  EntryMap::iterator i = children_->find(index);
  if (i != children_->end())
    return i->second;
  if (create)
    return new MemEntryImpl(backend_, index, this, net_log_.net_log());
  return nullptr;
}

int MemEntryImpl::FindNextChild(int64_t offset, int len, MemEntryImpl** child) {
  DCHECK(child);
  *child = nullptr;
  int scanned_len = 0;

  // This loop tries to find the first existing child.
  while (scanned_len < len) {
    // This points to the current offset in the child.
    int current_child_offset = ToChildOffset(offset + scanned_len);
    MemEntryImpl* current_child = GetChild(offset + scanned_len, false);
    if (current_child) {
      int child_first_pos = current_child->child_first_pos_;

      // This points to the first byte that we should be reading from, we need
      // to take care of the filled region and the current offset in the child.
      int first_pos =  std::max(current_child_offset, child_first_pos);

      // If the first byte position we should read from doesn't exceed the
      // filled region, we have found the first child.
      if (first_pos < current_child->GetDataSize(kSparseData)) {
         *child = current_child;

         // We need to advance the scanned length.
         scanned_len += first_pos - current_child_offset;
         break;
      }
    }
    scanned_len += kMaxSparseEntrySize - current_child_offset;
  }
  return scanned_len;
}

}  // namespace disk_cache
