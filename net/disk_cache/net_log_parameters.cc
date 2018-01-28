// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/net_log_parameters.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_source.h"

namespace {

std::unique_ptr<base::Value> NetLogEntryCreationCallback(
    const disk_cache::Entry* entry,
    bool created,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("key", entry->GetKey());
  dict->SetBoolean("created", created);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogReadWriteDataCallback(
    int index,
    int offset,
    int buf_len,
    bool truncate,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("index", index);
  dict->SetInteger("offset", offset);
  dict->SetInteger("buf_len", buf_len);
  if (truncate)
    dict->SetBoolean("truncate", truncate);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogReadWriteCompleteCallback(
    int bytes_copied,
    net::NetLogCaptureMode /* capture_mode */) {
  DCHECK_NE(bytes_copied, net::ERR_IO_PENDING);
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  if (bytes_copied < 0) {
    dict->SetInteger("net_error", bytes_copied);
  } else {
    dict->SetInteger("bytes_copied", bytes_copied);
  }
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogSparseOperationCallback(
    int64_t offset,
    int buf_len,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  // Values can only be created with at most 32-bit integers.  Using a string
  // instead circumvents that restriction.
  dict->SetString("offset", base::Int64ToString(offset));
  dict->SetInteger("buf_len", buf_len);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogSparseReadWriteCallback(
    const net::NetLogSource& source,
    int child_len,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  source.AddToEventParameters(dict.get());
  dict->SetInteger("child_len", child_len);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogGetAvailableRangeResultCallback(
    int64_t start,
    int result,
    net::NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  if (result > 0) {
    dict->SetInteger("length", result);
    dict->SetString("start",  base::Int64ToString(start));
  } else {
    dict->SetInteger("net_error", result);
  }
  return std::move(dict);
}

}  // namespace

namespace disk_cache {

net::NetLogParametersCallback CreateNetLogEntryCreationCallback(
    const Entry* entry,
    bool created) {
  DCHECK(entry);
  return base::Bind(&NetLogEntryCreationCallback, entry, created);
}

net::NetLogParametersCallback CreateNetLogReadWriteDataCallback(int index,
                                                                int offset,
                                                                int buf_len,
                                                                bool truncate) {
  return base::Bind(&NetLogReadWriteDataCallback,
                    index, offset, buf_len, truncate);
}

net::NetLogParametersCallback CreateNetLogReadWriteCompleteCallback(
    int bytes_copied) {
  return base::Bind(&NetLogReadWriteCompleteCallback, bytes_copied);
}

net::NetLogParametersCallback CreateNetLogSparseOperationCallback(
    int64_t offset,
    int buf_len) {
  return base::Bind(&NetLogSparseOperationCallback, offset, buf_len);
}

net::NetLogParametersCallback CreateNetLogSparseReadWriteCallback(
    const net::NetLogSource& source,
    int child_len) {
  return base::Bind(&NetLogSparseReadWriteCallback, source, child_len);
}

net::NetLogParametersCallback CreateNetLogGetAvailableRangeResultCallback(
    int64_t start,
    int result) {
  return base::Bind(&NetLogGetAvailableRangeResultCallback, start, result);
}

}  // namespace disk_cache
