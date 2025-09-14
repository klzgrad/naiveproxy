/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/unix_task_runner.h"
#include "perfetto/ext/traced/traced.h"
#include "src/android_stats/statsd_logging_helper.h"
#include "src/perfetto_cmd/trigger_producer.h"

namespace perfetto {
namespace {

int PrintUsage(const char* argv0) {
  PERFETTO_ELOG(R"(
Usage: %s TRIGGER...
  -h|--help  Show this message
)",
                argv0);
  return 1;
}

}  // namespace

int PERFETTO_EXPORT_ENTRYPOINT TriggerPerfettoMain(int argc, char** argv) {
  static const option long_options[] = {{"help", no_argument, nullptr, 'h'},
                                        {nullptr, 0, nullptr, 0}};

  // Set opterror to zero to disable |getopt_long| from printing an error and
  // exiting when it encounters an unknown option. Instead, |getopt_long|
  // will return '?' which we silently ignore.
  //
  // We prefer ths behaviour rather than erroring on unknown options because
  // trigger_perfetto can be called by apps so it's command line API needs to
  // be backward and forward compatible. If we introduce an option here which
  // apps will use in the future, we don't want to cause errors on older
  // platforms where the command line flag did not exist.
  //
  // This behaviour was introduced in Android S.
  opterr = 0;

  std::vector<std::string> triggers_to_activate;
  bool seen_unknown_arg = false;

  for (;;) {
    int option = getopt_long(argc, argv, "h", long_options, nullptr);

    if (option == 'h')
      return PrintUsage(argv[0]);

    if (option == '?') {
      seen_unknown_arg = true;
    }

    if (option == -1)
      break;  // EOF.
  }

  // See above for rationale on why we just ignore unknown args instead of
  // exiting.
  if (seen_unknown_arg) {
    PERFETTO_ELOG("Ignoring unknown arguments. See --help for usage.");
  }

  for (int i = optind; i < argc; i++)
    triggers_to_activate.push_back(std::string(argv[i]));

  if (triggers_to_activate.empty()) {
    PERFETTO_ELOG("At least one trigger must the specified.");
    return PrintUsage(argv[0]);
  }

  bool finished_with_success = false;
  base::UnixTaskRunner task_runner;
  TriggerProducer producer(
      &task_runner,
      [&task_runner, &finished_with_success](bool success) {
        finished_with_success = success;
        task_runner.Quit();
      },
      &triggers_to_activate);
  task_runner.Run();

  if (!finished_with_success) {
    return 1;
  }
  return 0;
}

}  // namespace perfetto
