// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_source.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/values.h"
#include "net/log/net_log_capture_mode.h"

namespace net {

namespace {

std::unique_ptr<base::Value> SourceEventParametersCallback(
    const NetLogSource source,
    NetLogCaptureMode /* capture_mode */) {
  if (!source.IsValid())
    return std::unique_ptr<base::Value>();
  std::unique_ptr<base::DictionaryValue> event_params(
      new base::DictionaryValue());
  source.AddToEventParameters(event_params.get());
  return std::move(event_params);
}

}  // namespace

// LoadTimingInfo requires this be 0.
const uint32_t NetLogSource::kInvalidId = 0;

NetLogSource::NetLogSource() : type(NetLogSourceType::NONE), id(kInvalidId) {}

NetLogSource::NetLogSource(NetLogSourceType type, uint32_t id)
    : type(type), id(id) {}

bool NetLogSource::IsValid() const {
  return id != kInvalidId;
}

void NetLogSource::AddToEventParameters(
    base::DictionaryValue* event_params) const {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("type", static_cast<int>(type));
  dict->SetInteger("id", static_cast<int>(id));
  event_params->Set("source_dependency", std::move(dict));
}

NetLogParametersCallback NetLogSource::ToEventParametersCallback() const {
  return base::Bind(&SourceEventParametersCallback, *this);
}

// static
bool NetLogSource::FromEventParameters(base::Value* event_params,
                                       NetLogSource* source) {
  base::DictionaryValue* dict = NULL;
  base::DictionaryValue* source_dict = NULL;
  int source_id = -1;
  int source_type = static_cast<int>(NetLogSourceType::COUNT);
  if (!event_params || !event_params->GetAsDictionary(&dict) ||
      !dict->GetDictionary("source_dependency", &source_dict) ||
      !source_dict->GetInteger("id", &source_id) ||
      !source_dict->GetInteger("type", &source_type)) {
    *source = NetLogSource();
    return false;
  }

  DCHECK_GE(source_id, 0);
  DCHECK_LT(source_type, static_cast<int>(NetLogSourceType::COUNT));
  *source = NetLogSource(static_cast<NetLogSourceType>(source_type), source_id);
  return true;
}

}  // namespace net
