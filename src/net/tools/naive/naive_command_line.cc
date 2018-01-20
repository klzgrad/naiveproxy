// Copyright 2024 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_command_line.h"

#include <memory>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include "base/strings/string_util_win.h"
#endif

DuplicateSwitchCollector::DuplicateSwitchCollector() = default;
DuplicateSwitchCollector::~DuplicateSwitchCollector() = default;

void DuplicateSwitchCollector::ResolveDuplicate(
    std::string_view key,
    base::CommandLine::StringViewType new_value,
    base::CommandLine::StringType& out_value) {
  out_value = new_value;
  values_by_key_[std::string(key)].push_back(
      base::CommandLine::StringType(new_value));
}

const std::vector<base::CommandLine::StringType>&
DuplicateSwitchCollector::GetValuesByKey(std::string_view key) {
  return values_by_key_[std::string(key)];
}

namespace {
DuplicateSwitchCollector* g_duplicate_switch_collector;
}

void DuplicateSwitchCollector::InitInstance() {
  auto new_duplicate_switch_collector =
      std::make_unique<DuplicateSwitchCollector>();
  g_duplicate_switch_collector = new_duplicate_switch_collector.get();
  base::CommandLine::SetDuplicateSwitchHandler(
      std::move(new_duplicate_switch_collector));
}

DuplicateSwitchCollector& DuplicateSwitchCollector::GetInstance() {
  CHECK(g_duplicate_switch_collector != nullptr);
  return *g_duplicate_switch_collector;
}

base::Value::Dict GetSwitchesAsValue(const base::CommandLine& cmdline) {
  base::Value::Dict dict;
  for (const auto& [key, value] : cmdline.GetSwitches()) {
    const std::vector<base::CommandLine::StringType>& values =
        DuplicateSwitchCollector::GetInstance().GetValuesByKey(key);
    if (values.size() > 1) {
      base::Value::List list;
      for (const base::CommandLine::StringType& v : values) {
#if BUILDFLAG(IS_WIN)
        list.Append(base::AsStringPiece16(v));
#else
        list.Append(v);
#endif
      }
      dict.Set(key, std::move(list));
    } else {
#if BUILDFLAG(IS_WIN)
      dict.Set(key, base::AsStringPiece16(value));
#else
      dict.Set(key, value);
#endif
    }
  }
  return dict;
}
