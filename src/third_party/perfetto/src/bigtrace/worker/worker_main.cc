/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <cstdint>
#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/getopt.h"
#include "src/bigtrace/worker/repository_policies/gcs_trace_processor_loader.h"
#include "src/bigtrace/worker/repository_policies/local_trace_processor_loader.h"
#include "src/bigtrace/worker/repository_policies/trace_processor_loader.h"
#include "src/bigtrace/worker/worker_impl.h"

namespace perfetto::bigtrace {
namespace {

struct CommandLineOptions {
  std::string socket;
};

CommandLineOptions ParseCommandLineOptions(int argc, char** argv) {
  CommandLineOptions command_line_options;
  static option long_options[] = {{"socket", required_argument, nullptr, 's'},
                                  {nullptr, 0, nullptr, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "s:", long_options, nullptr)) != -1) {
    switch (c) {
      case 's':
        command_line_options.socket = optarg;
        break;
      default:
        PERFETTO_ELOG("Usage: %s --socket=address:port", argv[0]);
        break;
    }
  }
  return command_line_options;
}

base::Status WorkerMain(int argc, char** argv) {
  // Setup the Worker Server
  CommandLineOptions options = ParseCommandLineOptions(argc, argv);
  std::string socket =
      options.socket.empty() ? "127.0.0.1:5052" : options.socket;

  std::unordered_map<std::string, std::unique_ptr<TraceProcessorLoader>>
      registry;
  registry["/gcs"] = std::make_unique<GcsTraceProcessorLoader>();
  registry["/local"] = std::make_unique<LocalTraceProcessorLoader>();

  auto service = std::make_unique<WorkerImpl>(std::move(registry));
  grpc::ServerBuilder builder;
  builder.RegisterService(service.get());
  builder.AddListeningPort(socket, grpc::InsecureServerCredentials());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  PERFETTO_LOG("Worker server listening on %s", socket.c_str());

  server->Wait();

  return base::OkStatus();
}

}  // namespace
}  // namespace perfetto::bigtrace

int main(int argc, char** argv) {
  auto status = perfetto::bigtrace::WorkerMain(argc, argv);
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
    return 1;
  }
  return 0;
}
