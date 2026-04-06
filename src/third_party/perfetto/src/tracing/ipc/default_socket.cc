/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/tracing/default_socket.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/ipc/basic_types.h"
#include "perfetto/ext/tracing/core/basic_types.h"

#include <stdlib.h>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <unistd.h>
#endif

namespace perfetto {
namespace {

const char* kRunPerfettoBaseDir = "/run/perfetto/";

// On Linux and CrOS, check /run/perfetto/ before using /tmp/ as the socket
// base directory.
bool UseRunPerfettoBaseDir() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)
  // Note that the trailing / in |kRunPerfettoBaseDir| ensures we are checking
  // against a directory, not a file.
  int res = PERFETTO_EINTR(access(kRunPerfettoBaseDir, X_OK));
  if (!res)
    return true;

  // If the path doesn't exist (ENOENT), fail silently to the caller. Otherwise,
  // fail with an explicit error message.
  if (errno != ENOENT
#if PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
      // access(2) won't return EPERM, but Chromium sandbox returns EPERM if the
      // sandbox doesn't allow the call (e.g. in the child processes).
      && errno != EPERM
#endif
  ) {
    PERFETTO_PLOG("%s exists but cannot be accessed. Falling back on /tmp/ ",
                  kRunPerfettoBaseDir);
  }
  return false;
#else
  base::ignore_result(kRunPerfettoBaseDir);
  return false;
#endif
}

}  // anonymous namespace

const char* GetProducerSocket() {
  const char* name = getenv("PERFETTO_PRODUCER_SOCK_NAME");
  if (name == nullptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    name = "127.0.0.1:32278";
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    name = "/dev/socket/traced_producer";
#else
    // Use /run/perfetto if it exists. Then fallback to /tmp.
    static const char* producer_socket =
        UseRunPerfettoBaseDir() ? "/run/perfetto/traced-producer.sock"
                                : "/tmp/perfetto-producer";
    name = producer_socket;
#endif
  }
  base::ignore_result(UseRunPerfettoBaseDir);  // Silence unused func warnings.
  return name;
}

std::string GetRelaySocket() {
  // The relay socket is optional and is connected only when the env var is set.
  // In Android, if the env var isn't set then we check the
  // |traced_relay.relay_port| system property.
  const char* name = getenv("PERFETTO_RELAY_SOCK_NAME");
  if (name != nullptr)
    return std::string(name);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return base::GetAndroidProp("traced_relay.relay_port");
#else
  return std::string();
#endif
}

std::vector<std::string> TokenizeProducerSockets(
    const char* producer_socket_names) {
  return base::SplitString(producer_socket_names, ",");
}

const char* GetConsumerSocket() {
  const char* name = getenv("PERFETTO_CONSUMER_SOCK_NAME");
  if (name == nullptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    name = "127.0.0.1:32279";
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    name = "/dev/socket/traced_consumer";
#else
    // Use /run/perfetto if it exists. Then fallback to /tmp.
    static const char* consumer_socket =
        UseRunPerfettoBaseDir() ? "/run/perfetto/traced-consumer.sock"
                                : "/tmp/perfetto-consumer";
    name = consumer_socket;
#endif
  }
  return name;
}

}  // namespace perfetto
