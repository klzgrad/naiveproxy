// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_experiment.h"

#include <map>
#include <string>

#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_param_associator.h"
#include "base/strings/string_number_conversions.h"

namespace disk_cache {

const base::Feature kSimpleSizeExperiment = {"SimpleSizeExperiment",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const char kSizeMultiplierParam[] = "SizeMultiplier";

namespace {

struct ExperimentDescription {
  disk_cache::SimpleExperimentType experiment_type;
  const base::Feature* feature;
  const char* param_name;
};

// List of experimens to be checked for.
const ExperimentDescription experiments[] = {
    {disk_cache::SimpleExperimentType::SIZE, &kSimpleSizeExperiment,
     kSizeMultiplierParam},
};

}  // namespace

// Returns the experiment for the given |cache_type|.
SimpleExperiment GetSimpleExperiment(net::CacheType cache_type) {
  SimpleExperiment experiment;
  if (cache_type != net::DISK_CACHE)
    return experiment;

  for (size_t i = 0; i < arraysize(experiments); i++) {
    if (!base::FeatureList::IsEnabled(*experiments[i].feature))
      continue;

    base::FieldTrial* trial =
        base::FeatureList::GetFieldTrial(*experiments[i].feature);
    if (!trial)
      continue;

    std::map<std::string, std::string> params;
    base::FieldTrialParamAssociator::GetInstance()->GetFieldTrialParams(
        trial->trial_name(), &params);
    auto iter = params.find(experiments[i].param_name);
    if (iter == params.end())
      continue;

    uint32_t param;
    if (!base::StringToUint(iter->second, &param))
      continue;

    experiment.type = experiments[i].experiment_type;
    experiment.param = param;
    return experiment;
  }

  return experiment;
}

}  // namespace disk_cache
