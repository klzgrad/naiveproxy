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

#include <set>
#include <vector>

#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/status_or.h"
#include "src/protovm/compiler/compiler.h"
#include "src/protozero/multifile_error_collector.h"

namespace {

perfetto::base::StatusOr<std::string> LoadDescriptors(
    const std::vector<std::string>& proto_paths,
    const std::vector<std::string>& proto_files) {
  google::protobuf::compiler::DiskSourceTree source_tree;
  for (const auto& path : proto_paths) {
    source_tree.MapPath("", path);
  }

  protozero::MultiFileErrorCollectorImpl error_collector;
  google::protobuf::compiler::Importer importer(&source_tree, &error_collector);

  std::unordered_set<std::string> added_files;
  google::protobuf::FileDescriptorSet fds;

  auto add_file_and_deps =
      [&](auto& self, const google::protobuf::FileDescriptor* desc) -> void {
    if (!added_files.insert(std::string(desc->name())).second)
      return;
    for (int i = 0; i < desc->dependency_count(); ++i) {
      self(self, desc->dependency(i));
    }
    desc->CopyTo(fds.add_file());
  };

  for (const auto& file : proto_files) {
    const auto* file_desc = importer.Import(file);
    if (!file_desc) {
      return perfetto::base::ErrStatus("Failed to import %s", file.c_str());
    }

    add_file_and_deps(add_file_and_deps, file_desc);
  }

  return fds.SerializeAsString();
}

}  // namespace

// Implements the ProtoVM compiler CLI
//
// Reads a CompileConfig textproto from stdin and outputs the compiled binary
// VmProgram to stdout.
int main(int argc, char** argv) {
  static const option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"proto_path", required_argument, nullptr, 'I'},
      {"descriptor", required_argument, nullptr, 'd'},
      {nullptr, 0, nullptr, 0}};

  std::vector<std::string> proto_paths;
  std::vector<std::string> proto_files;
  std::vector<std::string> descriptor_files;

  for (;;) {
    int option = getopt_long(argc, argv, "hI:d:", long_options, nullptr);
    if (option == -1)
      break;

    if (option == 'h') {
      std::printf(
          "Usage: %s [-I <proto_path>] [-d <descriptor>] <file1.proto> ...\n"
          "Reads ProtoVM CompileConfig textproto from stdin, compiles it and "
          "outputs a VmProgram binary on stdout.\n",
          argv[0]);
      return 0;
    }
    if (option == 'I') {
      proto_paths.emplace_back(optarg);
      continue;
    }
    if (option == 'd') {
      descriptor_files.emplace_back(optarg);
      continue;
    }
    return 1;
  }

  for (int i = optind; i < argc; ++i) {
    proto_files.emplace_back(argv[i]);
  }

  std::string descriptors;
  for (const auto& file : descriptor_files) {
    std::string content;
    if (!perfetto::base::ReadFile(file, &content)) {
      PERFETTO_ELOG("Failed to read descriptor file %s", file.c_str());
      return 1;
    }
    descriptors += content;
  }

  if (!proto_files.empty()) {
    auto status_or_descriptors = LoadDescriptors(proto_paths, proto_files);
    if (!status_or_descriptors.ok()) {
      PERFETTO_ELOG("%s", status_or_descriptors.status().c_message());
      return 1;
    }
    descriptors += *status_or_descriptors;
  }

  if (descriptors.empty()) {
    PERFETTO_ELOG("At least one .proto file or -d/--descriptor is required");
    return 1;
  }

  std::string textproto;
  if (!perfetto::base::ReadFileStream(stdin, &textproto)) {
    PERFETTO_ELOG("Failed to read from stdin");
    return 1;
  }

  auto compiler = perfetto::protovm::Compiler{};
  auto status_or_program = compiler.Compile(textproto, descriptors);
  if (!status_or_program.ok()) {
    PERFETTO_ELOG("Error: %s", status_or_program.status().c_message());
    return 1;
  }

  std::fwrite(status_or_program->data(), 1, status_or_program->size(), stdout);

  return 0;
}
