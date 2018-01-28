// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CONFIG_H_
#define TOOLS_GN_CONFIG_H_

#include "base/logging.h"
#include "base/macros.h"
#include "tools/gn/config_values.h"
#include "tools/gn/item.h"
#include "tools/gn/label_ptr.h"
#include "tools/gn/unique_vector.h"

// Represents a named config in the dependency graph.
//
// A config can list other configs. We track both the data assigned directly
// on the config, this list of sub-configs, and (when the config is resolved)
// the resulting values of everything merged together. The flatten step
// means we can avoid doing a recursive config walk for every target to compute
// flags.
class Config : public Item {
 public:
  Config(const Settings* settings, const Label& label);
  ~Config() override;

  // Item implementation.
  Config* AsConfig() override;
  const Config* AsConfig() const override;
  bool OnResolved(Err* err) override;

  // The values set directly on this config. This will not contain data from
  // sub-configs.
  ConfigValues& own_values() { return own_values_; }
  const ConfigValues& own_values() const { return own_values_; }

  // The values that represent this config and all sub-configs combined into
  // one. This is only valid after the config is resolved (when we know the
  // contents of the sub-configs).
  const ConfigValues& resolved_values() const {
    DCHECK(resolved_);
    if (configs_.empty())  // No sub configs, just use the regular values.
      return own_values_;
    return composite_values_;
  }

  // List of sub-configs.
  const UniqueVector<LabelConfigPair>& configs() const { return configs_; }
  UniqueVector<LabelConfigPair>& configs() { return configs_; }

 private:
  ConfigValues own_values_;

  // Contains the own_values combined with sub-configs. Most configs don't have
  // sub-configs. So as an optimization, this is not populated if there are no
  // items in configs_. The resolved_values() getter handles this.
  bool resolved_;
  ConfigValues composite_values_;

  UniqueVector<LabelConfigPair> configs_;

  DISALLOW_COPY_AND_ASSIGN(Config);
};

#endif  // TOOLS_GN_CONFIG_H_
