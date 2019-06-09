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
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_source.h"

namespace {

base::Value NetLogParametersEntryCreationCallback(
    const disk_cache::Entry* entry,
    bool created,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetStringKey("key", entry->GetKey());
  dict.SetBoolKey("created", created);
  return dict;
}

base::Value NetLogReadWriteDataCallback(
    int index,
    int offset,
    int buf_len,
    bool truncate,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetIntKey("index", index);
  dict.SetIntKey("offset", offset);
  dict.SetIntKey("buf_len", buf_len);
  if (truncate)
    dict.SetBoolKey("truncate", truncate);
  return dict;
}

base::Value NetLogReadWriteCompleteCallback(
    int bytes_copied,
    net::NetLogCaptureMode /* capture_mode */) {
  DCHECK_NE(bytes_copied, net::ERR_IO_PENDING);
  base::Value dict(base::Value::Type::DICTIONARY);
  if (bytes_copied < 0) {
    dict.SetIntKey("net_error", bytes_copied);
  } else {
    dict.SetIntKey("bytes_copied", bytes_copied);
  }
  return dict;
}

base::Value NetLogSparseOperationCallback(
    int64_t offset,
    int buf_len,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetKey("offset", net::NetLogNumberValue(offset));
  dict.SetIntKey("buf_len", buf_len);
  return dict;
}

base::Value NetLogSparseReadWriteCallback(
    const net::NetLogSource& source,
    int child_len,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  source.AddToEventParameters(&dict);
  dict.SetIntKey("child_len", child_len);
  return dict;
}

base::Value NetLogGetAvailableRangeResultCallback(
    int64_t start,
    int result,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  if (result > 0) {
    dict.SetIntKey("length", result);
    dict.SetKey("start", net::NetLogNumberValue(start));
  } else {
    dict.SetIntKey("net_error", result);
  }
  return dict;
}

}  // namespace

namespace disk_cache {

net::NetLogParametersCallback CreateNetLogParametersEntryCreationCallback(
    const Entry* entry,
    bool created) {
  DCHECK(entry);
  return base::Bind(&NetLogParametersEntryCreationCallback, entry, created);
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
