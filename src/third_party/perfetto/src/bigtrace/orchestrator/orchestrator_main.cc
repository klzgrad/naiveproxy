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

#include <chrono>
#include <limits>
#include <memory>
#include <mutex>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "protos/perfetto/bigtrace/worker.grpc.pb.h"
#include "src/bigtrace/orchestrator/orchestrator_impl.h"

namespace perfetto::bigtrace {
namespace {

struct CommandLineOptions {
  std::string server_socket;
  std::string worker_address;
  uint16_t worker_port = 0;
  uint64_t worker_count = 0;
  std::string worker_address_list;
  std::string name_resolution_scheme;
  uint32_t pool_size = 0;
};

void PrintUsage(char** argv) {
  PERFETTO_ELOG(R"(
Orchestrator main executable.
Usage: %s [OPTIONS]
Options:
 -h, --help                             Prints this guide.
 -s, --server_socket ADDRESS:PORT       Input the socket for the
                                        gRPC service to run on
 -w, --worker_address ADDRESS           Input the address of the workers
                                        (for single address and
                                        incrementing port)
 -p  --worker_port PORT                 Input the starting port of
                                        the workers
 -n  --worker_count NUM_WORKERS         Input the number of workers
                                        (this will specify workers
                                        starting from the worker_port
                                        and counting up)
 -l  --worker_list SOCKET1,SOCKET2,...  Input a string of comma a separated
                                        worker sockets
                                        (use either -l or
                                         -w -p -n EXCLUSIVELY)
 -r --name_resolution_scheme SCHEME     Specify the name resolution
                                        scheme for gRPC (e.g. ipv4:, dns://)
 -t -max_query_concurrency              Specify the number of concurrent
  MAX_QUERY_CONCURRENCY                 queries/gRPCs from the Orchestrator
                                        )",
                argv[0]);
}

base::StatusOr<CommandLineOptions> ParseCommandLineOptions(int argc,
                                                           char** argv) {
  CommandLineOptions command_line_options;

  static option long_options[] = {
      {"help", optional_argument, nullptr, 'h'},
      {"server_socket", optional_argument, nullptr, 's'},
      {"worker_address", optional_argument, nullptr, 'w'},
      {"worker_port", optional_argument, nullptr, 'p'},
      {"worker_count", optional_argument, nullptr, 'n'},
      {"worker_list", optional_argument, nullptr, 'l'},
      {"name_resolution_scheme", optional_argument, nullptr, 'r'},
      {"thread_pool_size", optional_argument, nullptr, 't'},
      {nullptr, 0, nullptr, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "s:p:w:q:n:l:r:t:h", long_options,
                          nullptr)) != -1) {
    switch (c) {
      case 's':
        command_line_options.server_socket = optarg;
        break;
      case 'w':
        command_line_options.worker_address = optarg;
        break;
      case 'p':
        command_line_options.worker_port = static_cast<uint16_t>(atoi(optarg));
        break;
      case 'n':
        command_line_options.worker_count = static_cast<uint64_t>(atoi(optarg));
        break;
      case 'l':
        command_line_options.worker_address_list = optarg;
        break;
      case 'r':
        command_line_options.name_resolution_scheme = optarg;
        break;
      case 't':
        command_line_options.pool_size = static_cast<uint32_t>(atoi(optarg));
        break;
      default:
        PrintUsage(argv);
        exit(c == 'h' ? 0 : 1);
    }
  }

  bool has_worker_address_port_and_count =
      command_line_options.worker_count && command_line_options.worker_port &&
      !command_line_options.worker_address.empty();

  bool has_worker_list = !command_line_options.worker_address_list.empty();

  if (has_worker_address_port_and_count == has_worker_list) {
    return base::ErrStatus(
        "Error: You must specify a worker address, port and count OR a worker "
        "list");
  }

  if (command_line_options.worker_count <= 0 && !has_worker_list) {
    return base::ErrStatus(
        "Error: You must specify a worker count greater than 0 OR a worker "
        "list");
  }

  return command_line_options;
}

base::Status OrchestratorMain(int argc, char** argv) {
  ASSIGN_OR_RETURN(base::StatusOr<CommandLineOptions> options,
                   ParseCommandLineOptions(argc, argv));
  std::string server_socket = options->server_socket.empty()
                                  ? "127.0.0.1:5051"
                                  : options->server_socket;
  std::string worker_address =
      options->worker_address.empty() ? "127.0.0.1" : options->worker_address;
  uint16_t worker_port =
      options->worker_port == 0 ? 5052 : options->worker_port;
  std::string worker_address_list = options->worker_address_list;
  uint64_t worker_count = options->worker_count;

  std::string target_address = options->name_resolution_scheme.empty()
                                   ? "ipv4:"
                                   : options->name_resolution_scheme;

  uint32_t pool_size = options->pool_size == 0
                           ? std::thread::hardware_concurrency()
                           : options->pool_size;

  PERFETTO_DCHECK(pool_size);

  if (worker_address_list.empty()) {
    // Use a set of n workers incrementing from a starting port
    PERFETTO_DCHECK(worker_count > 0 && !worker_address.empty());
    std::vector<std::string> worker_addresses;
    for (uint64_t i = 0; i < worker_count; ++i) {
      std::string address =
          worker_address + ":" + std::to_string(worker_port + i);
      worker_addresses.push_back(address);
    }
    target_address += base::Join(worker_addresses, ",");
  } else {
    // Use a list of workers passed as an option
    target_address += worker_address_list;
  }
  grpc::ChannelArguments args;
  args.SetLoadBalancingPolicyName("round_robin");
  args.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  auto channel = grpc::CreateCustomChannel(
      target_address, grpc::InsecureChannelCredentials(), args);
  auto stub = protos::BigtraceWorker::NewStub(channel);
  auto service = std::make_unique<OrchestratorImpl>(std::move(stub), pool_size);

  // Setup the Orchestrator Server
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  builder.AddListeningPort(server_socket, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  PERFETTO_LOG("Orchestrator server listening on %s", server_socket.c_str());

  server->Wait();

  return base::OkStatus();
}

}  // namespace
}  // namespace perfetto::bigtrace

int main(int argc, char** argv) {
  auto status = perfetto::bigtrace::OrchestratorMain(argc, argv);
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
    return 1;
  }
  return 0;
}
