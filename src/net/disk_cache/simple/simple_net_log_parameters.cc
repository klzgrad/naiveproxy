// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_net_log_parameters.h"

#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/simple/simple_entry_impl.h"
#include "net/log/net_log_capture_mode.h"

namespace {

base::Value NetLogSimpleEntryConstructionCallback(
    const disk_cache::SimpleEntryImpl* entry,
    net::NetLogCaptureMode capture_mode) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetStringKey("entry_hash",
                    base::StringPrintf("%#016" PRIx64, entry->entry_hash()));
  return dict;
}

base::Value NetLogSimpleEntryCreationCallback(
    const disk_cache::SimpleEntryImpl* entry,
    int net_error,
    net::NetLogCaptureMode /* capture_mode */) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetIntKey("net_error", net_error);
  if (net_error == net::OK)
    dict.SetStringKey("key", entry->key());
  return dict;
}

}  // namespace

namespace disk_cache {

net::NetLogParametersCallback CreateNetLogSimpleEntryConstructionCallback(
    const SimpleEntryImpl* entry) {
  DCHECK(entry);
  return base::Bind(&NetLogSimpleEntryConstructionCallback,
                    base::Unretained(entry));
}

net::NetLogParametersCallback CreateNetLogSimpleEntryCreationCallback(
    const SimpleEntryImpl* entry,
    int net_error) {
  DCHECK(entry);
  return base::Bind(&NetLogSimpleEntryCreationCallback, base::Unretained(entry),
                    net_error);
}

}  // namespace disk_cache
