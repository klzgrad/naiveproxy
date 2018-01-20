// Copyright 2024 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_COMMAND_LINE_H_
#define NET_TOOLS_NAIVE_NAIVE_COMMAND_LINE_H_

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/values.h"

class DuplicateSwitchCollector : public base::DuplicateSwitchHandler {
 public:
  DuplicateSwitchCollector();
  ~DuplicateSwitchCollector() override;

  void ResolveDuplicate(std::string_view key,
                        base::CommandLine::StringViewType new_value,
                        base::CommandLine::StringType& out_value) override;

  const std::vector<base::CommandLine::StringType>& GetValuesByKey(
      std::string_view key);

  static void InitInstance();
  static DuplicateSwitchCollector& GetInstance();

 private:
  std::map<std::string, std::vector<base::CommandLine::StringType>>
      values_by_key_;
};

base::Value::Dict GetSwitchesAsValue(const base::CommandLine& cmdline);
#endif  // NET_TOOLS_NAIVE_NAIVE_COMMAND_LINE_H_
