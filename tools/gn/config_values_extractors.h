// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CONFIG_VALUES_EXTRACTORS_H_
#define TOOLS_GN_CONFIG_VALUES_EXTRACTORS_H_

#include <stddef.h>

#include <ostream>
#include <string>
#include <vector>

#include "tools/gn/config.h"
#include "tools/gn/config_values.h"
#include "tools/gn/target.h"

struct EscapeOptions;

// Provides a way to iterate through all ConfigValues applying to a given
// target. This is more complicated than normal because the target has a list
// of configs applying to it, and also config values on the target itself.
//
// This iterator allows one to iterate through all of these in a defined order
// in one convenient loop. The order is defined to be the ConfigValues on the
// target itself first, then the applying configs, in order.
//
// Example:
//   for (ConfigValueIterator iter(target); !iter.done(); iter.Next())
//     DoSomething(iter.cur());
class ConfigValuesIterator {
 public:
  explicit ConfigValuesIterator(const Target* target)
      : target_(target),
        cur_index_(-1) {
  }

  bool done() const {
    return cur_index_ >= static_cast<int>(target_->configs().size());
  }

  const ConfigValues& cur() const {
    if (cur_index_ == -1)
      return target_->config_values();
    return target_->configs()[cur_index_].ptr->resolved_values();
  }

  // Returns the origin of who added this config, if any. This will always be
  // null for the config values of a target itself.
  const ParseNode* origin() const {
    if (cur_index_ == -1)
      return nullptr;
    return target_->configs()[cur_index_].origin;
  }

  void Next() {
    cur_index_++;
  }

  // Returns the config holding the current config values, or NULL for those
  // config values associated with the target itself.
  const Config* GetCurrentConfig() const {
    if (cur_index_ == -1)
      return nullptr;
    return target_->configs()[cur_index_].ptr;
  }

 private:
  const Target* target_;

  // Represents an index into the target_'s configs() or, when -1, the config
  // values on the target itself.
  int cur_index_;
};

template<typename T, class Writer>
inline void ConfigValuesToStream(
    const ConfigValues& values,
    const std::vector<T>& (ConfigValues::* getter)() const,
    const Writer& writer,
    std::ostream& out) {
  const std::vector<T>& v = (values.*getter)();
  for (size_t i = 0; i < v.size(); i++)
    writer(v[i], out);
}

// Writes a given config value that applies to a given target. This collects
// all values from the target itself and all configs that apply, and writes
// then in order.
template<typename T, class Writer>
inline void RecursiveTargetConfigToStream(
    const Target* target,
    const std::vector<T>& (ConfigValues::* getter)() const,
    const Writer& writer,
    std::ostream& out) {
  for (ConfigValuesIterator iter(target); !iter.done(); iter.Next())
    ConfigValuesToStream(iter.cur(), getter, writer, out);
}

// Writes the values out as strings with no transformation.
void RecursiveTargetConfigStringsToStream(
    const Target* target,
    const std::vector<std::string>& (ConfigValues::* getter)() const,
    const EscapeOptions& escape_options,
    std::ostream& out);

#endif  // TOOLS_GN_CONFIG_VALUES_EXTRACTORS_H_
