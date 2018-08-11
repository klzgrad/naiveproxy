// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_EXPERIMENT_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_EXPERIMENT_H_

#include <stdint.h>

#include "base/feature_list.h"
#include "net/base/cache_type.h"
#include "net/base/net_export.h"

namespace disk_cache {

NET_EXPORT_PRIVATE extern const base::Feature kSimpleSizeExperiment;
NET_EXPORT_PRIVATE extern const char kSizeMultiplierParam[];

// This lists the experiment groups for SimpleCache. Only add new groups at
// the end of the list, and always increase the number.
enum class SimpleExperimentType : uint32_t {
  NONE = 0,
  SIZE = 1,
  EVICT_WITH_SIZE = 2,  // deprecated
};

struct NET_EXPORT_PRIVATE SimpleExperiment {
  SimpleExperimentType type = SimpleExperimentType::NONE;
  uint32_t param = 0;
};

NET_EXPORT_PRIVATE SimpleExperiment
GetSimpleExperiment(net::CacheType cache_type);

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_EXPERIMENT_H_
