// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_netlog_params.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/log/net_log_capture_mode.h"
#include "url/gurl.h"

namespace net {

std::unique_ptr<base::Value> NetLogURLRequestConstructorCallback(
    const GURL* url,
    RequestPriority priority,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("url", url->possibly_invalid_spec());
  dict->SetString("priority", RequestPriorityToString(priority));
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogURLRequestStartCallback(
    const GURL* url,
    const std::string* method,
    int load_flags,
    int64_t upload_id,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("url", url->possibly_invalid_spec());
  dict->SetString("method", *method);
  dict->SetInteger("load_flags", load_flags);
  if (upload_id > -1)
    dict->SetString("upload_id", base::Int64ToString(upload_id));
  return std::move(dict);
}

}  // namespace net
