// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/trust_tokens/types.h"
#include "base/time/time.h"
#include "base/value_conversions.h"
#include "base/values.h"
#include "url/origin.h"

namespace net {
namespace internal {

base::Optional<base::Time> StringToTime(base::StringPiece my_string) {
  base::Time ret;
  if (!base::GetValueAsTime(base::Value(my_string), &ret))
    return base::nullopt;
  return ret;
}

std::string TimeToString(base::Time my_time) {
  return base::CreateTimeValue(my_time).GetString();
}

}  // namespace internal
}  // namespace net
