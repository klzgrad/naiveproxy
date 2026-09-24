/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/shell/sql_packages.h"

#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

std::string ParsePackagePath(const std::string& arg, std::string* out_package) {
  size_t at_pos = arg.rfind('@');
  if (at_pos != std::string::npos) {
    *out_package = arg.substr(at_pos + 1);
    return arg.substr(0, at_pos);
  }
  *out_package = "";
  return arg;
}

base::Status IncludeSqlPackage(TraceProcessor* trace_processor,
                               const std::string& path_arg,
                               bool allow_override) {
  std::string explicit_package;
  std::string root = ParsePackagePath(path_arg, &explicit_package);

  // Remove trailing slash
  if (!root.empty() && root.back() == '/')
    root.resize(root.length() - 1);

  if (!base::FileExists(root))
    return base::ErrStatus("Directory %s does not exist.", root.c_str());

  // Get package name: use explicit package if provided, otherwise dirname
  std::string package_name;
  if (!explicit_package.empty()) {
    package_name = explicit_package;
  } else {
    size_t last_slash = root.rfind('/');
    if (last_slash == std::string::npos) {
      return base::ErrStatus("Package path must point to a directory: %s",
                             root.c_str());
    }
    package_name = root.substr(last_slash + 1);
  }

  std::vector<std::string> paths;
  RETURN_IF_ERROR(base::ListFilesRecursive(root, paths));
  sql_modules::NameToPackage modules;
  for (const auto& path : paths) {
    if (base::GetFileExtension(path) != ".sql") {
      continue;
    }

    std::string path_no_extension = path.substr(0, path.rfind('.'));
    if (path_no_extension.find('.') != std::string_view::npos) {
      PERFETTO_ELOG("Skipping module %s as it contains a dot in its path.",
                    path_no_extension.c_str());
      continue;
    }

    std::string filename = root + "/" + path;
    std::string file_contents;
    if (!base::ReadFile(filename, &file_contents)) {
      return base::ErrStatus("Cannot read file %s", filename.c_str());
    }

    std::string import_key =
        package_name + "." + sql_modules::GetIncludeKey(path);
    modules.Insert(package_name, {})
        .first->push_back({import_key, file_contents});
  }
  for (auto module_it = modules.GetIterator(); module_it; ++module_it) {
    RETURN_IF_ERROR(trace_processor->RegisterSqlPackage(
        {/*name=*/module_it.key(),
         /*files=*/module_it.value(),
         /*allow_override=*/allow_override}));
  }
  return base::OkStatus();
}

base::Status LoadOverridenStdlib(TraceProcessor* trace_processor,
                                 std::string root) {
  // Remove trailing slash
  if (root.back() == '/') {
    root.resize(root.length() - 1);
  }

  if (!base::FileExists(root)) {
    return base::ErrStatus("Directory '%s' does not exist.", root.c_str());
  }

  std::vector<std::string> paths;
  RETURN_IF_ERROR(base::ListFilesRecursive(root, paths));
  sql_modules::NameToPackage packages;
  for (const auto& path : paths) {
    if (base::GetFileExtension(path) != ".sql") {
      continue;
    }
    std::string filename = root + "/" + path;
    std::string module_file;
    if (!base::ReadFile(filename, &module_file)) {
      return base::ErrStatus("Cannot read file '%s'", filename.c_str());
    }
    std::string module_name = sql_modules::GetIncludeKey(path);
    std::string package_name = sql_modules::GetPackageName(module_name);
    packages.Insert(package_name, {})
        .first->push_back({module_name, module_file});
  }
  for (auto package = packages.GetIterator(); package; ++package) {
    trace_processor->RegisterSqlPackage({/*name=*/package.key(),
                                         /*files=*/package.value(),
                                         /*allow_override=*/true});
  }

  return base::OkStatus();
}

base::Status RegisterAllFilesInFolder(const std::string& path,
                                      TraceProcessor& tp) {
  std::vector<std::string> files;
  RETURN_IF_ERROR(base::ListFilesRecursive(path, files));
  for (const std::string& file : files) {
    std::string file_full_path = path + "/" + file;
    base::ScopedMmap mmap = base::ReadMmapWholeFile(file_full_path);
    if (!mmap.IsValid()) {
      return base::ErrStatus("Failed to mmap file: %s", file_full_path.c_str());
    }
    RETURN_IF_ERROR(tp.RegisterFileContent(
        file_full_path, TraceBlob::FromMmap(std::move(mmap))));
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
