// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_SCOPED_FEATURE_LIST_H_
#define BASE_TEST_SCOPED_FEATURE_LIST_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/feature_list.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/field_trial.h"

namespace base {
namespace test {

// ScopedFeatureList resets the global FeatureList instance to a new empty
// instance and restores the original instance upon destruction.
// Note: Re-using the same object is not allowed. To reset the feature
// list and initialize it anew, destroy an existing scoped list and init
// a new one.
//
// ScopedFeatureList needs to be initialized (via one of Init... methods)
// before running code that inspects the state of features.  In practice this
// means:
// - In browser tests, one of Init... methods should be called from the
//   overriden ::testing::Test::SetUp method. For example:
//     void SetUp() override {
//       scoped_feature_list_.InitAndEnableFeature(features::kMyFeatureHere);
//       InProcessBrowserTest::SetUp();
//     }
class ScopedFeatureList final {
 public:
  ScopedFeatureList();
  ~ScopedFeatureList();

  // WARNING: This method will reset any globally configured features to their
  // default values, which can hide feature interaction bugs. Please use
  // sparingly.  https://crbug.com/713390
  // Initializes and registers a FeatureList instance with no overrides.
  void Init();

  // WARNING: This method will reset any globally configured features to their
  // default values, which can hide feature interaction bugs. Please use
  // sparingly.  https://crbug.com/713390
  // Initializes and registers the given FeatureList instance.
  void InitWithFeatureList(std::unique_ptr<FeatureList> feature_list);

  // WARNING: This method will reset any globally configured features to their
  // default values, which can hide feature interaction bugs. Please use
  // sparingly.  https://crbug.com/713390
  // Initializes and registers a FeatureList instance with only the given
  // enabled and disabled features (comma-separated names).
  void InitFromCommandLine(const std::string& enable_features,
                           const std::string& disable_features);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with the given enabled and disabled features.
  // Any feature overrides already present in the global FeatureList will
  // continue to apply, unless they conflict with the overrides passed into this
  // method. This is important for testing potentially unexpected feature
  // interactions.
  void InitWithFeatures(const std::vector<Feature>& enabled_features,
                        const std::vector<Feature>& disabled_features);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with single enabled feature.
  void InitAndEnableFeature(const Feature& feature);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with single enabled feature and associated field
  // trial parameters.
  // Note: this creates a scoped global field trial list if there is not
  // currently one.
  void InitAndEnableFeatureWithParameters(
      const Feature& feature,
      const std::map<std::string, std::string>& feature_parameters);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with single disabled feature.
  void InitAndDisableFeature(const Feature& feature);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overriden with a single feature either enabled or
  // disabled depending on |enabled|.
  void InitWithFeatureState(const Feature& feature, bool enabled);

 private:
  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with the given enabled and disabled features.
  // Any feature overrides already present in the global FeatureList will
  // continue to apply, unless they conflict with the overrides passed into this
  // method.
  // Field trials will apply to the enabled features, in the same order. The
  // number of trials must be less (or equal) than the number of enabled
  // features.
  // Trials are expected to outlive the ScopedFeatureList.
  void InitWithFeaturesAndFieldTrials(
      const std::vector<Feature>& enabled_features,
      const std::vector<FieldTrial*>& trials_for_enabled_features,
      const std::vector<Feature>& disabled_features);

  // Initializes and registers a FeatureList instance based on present
  // FeatureList and overridden with single enabled feature and associated field
  // trial override.
  // |trial| is expected to outlive the ScopedFeatureList.
  void InitAndEnableFeatureWithFieldTrialOverride(const Feature& feature,
                                                  FieldTrial* trial);

  bool init_called_ = false;
  std::unique_ptr<FeatureList> original_feature_list_;
  scoped_refptr<FieldTrial> field_trial_override_;
  std::unique_ptr<base::FieldTrialList> field_trial_list_;

  DISALLOW_COPY_AND_ASSIGN(ScopedFeatureList);
};

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_SCOPED_FEATURE_LIST_H_
