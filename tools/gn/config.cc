// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/config.h"

#include "tools/gn/err.h"
#include "tools/gn/input_file_manager.h"
#include "tools/gn/scheduler.h"

Config::Config(const Settings* settings, const Label& label)
    : Item(settings, label), resolved_(false) {}

Config::~Config() {
}

Config* Config::AsConfig() {
  return this;
}

const Config* Config::AsConfig() const {
  return this;
}

bool Config::OnResolved(Err* err) {
  DCHECK(!resolved_);
  resolved_ = true;

  if (!configs_.empty()) {
    // Subconfigs, flatten.
    //
    // Implementation note for the future: Flattening these here means we
    // lose the ability to de-dupe subconfigs. If a subconfig is listed as
    // a separate config or a subconfig that also applies to the target, the
    // subconfig's flags will be duplicated.
    //
    // If we want to be able to de-dupe these, here's one idea. As a config is
    // resolved, inline any sub-sub configs so the configs_ vector is a flat
    // list, much the same way that libs and lib_dirs are pushed through
    // targets. Do the same for Target.configs_ when a target is resolved. This
    // will naturally de-dupe and also prevents recursive config walking to
    // compute every possible flag, although it will expand the configs list on
    // a target nontrivially (depending on build configuration).
    composite_values_ = own_values_;
    for (const auto& pair : configs_)
      composite_values_.AppendValues(pair.ptr->resolved_values());
  }
  return true;
}
